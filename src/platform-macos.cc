// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Platform specific code for MacOS goes here

#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mach/mach_init.h>

#include <AvailabilityMacros.h>

#ifdef MAC_OS_X_VERSION_10_5
# include <execinfo.h>  // backtrace, backtrace_symbols
#endif

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <stdlib.h>

#undef MAP_TYPE

#include "v8.h"

#include "platform.h"

namespace v8 { namespace internal {

// 0 is never a valid thread id on MacOSX since a ptread_t is
// a pointer.
static const pthread_t kNoThread = (pthread_t) 0;


double ceiling(double x) {
  // Correct Mac OS X Leopard 'ceil' behavior.
  if (-1.0 < x && x < 0.0) {
    return -0.0;
  } else {
    return ceil(x);
  }
}


void OS::Setup() {
  // Seed the random number generator.
  srandom(static_cast<unsigned int>(TimeCurrentMillis()));
}


int OS::GetUserTime(uint32_t* secs,  uint32_t* usecs) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) < 0) return -1;
  *secs = usage.ru_utime.tv_sec;
  *usecs = usage.ru_utime.tv_usec;
  return 0;
}


double OS::TimeCurrentMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) return 0.0;
  return (static_cast<double>(tv.tv_sec) * 1000) +
         (static_cast<double>(tv.tv_usec) / 1000);
}


int64_t OS::Ticks() {
  // Mac OS's gettimeofday has microsecond resolution.
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0)
    return 0;
  return (static_cast<int64_t>(tv.tv_sec) * 1000000) + tv.tv_usec;
}


char* OS::LocalTimezone(double time) {
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  return const_cast<char*>(t->tm_zone);
}


double OS::DaylightSavingsOffset(double time) {
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  return t->tm_isdst ? 3600 * msPerSecond : 0;
}


double OS::LocalTimeOffset() {
  // 1199174400 = Jan 1 2008 (UTC).
  // Random date where daylight savings time is not in effect.
  static const int kJan1st2008 = 1199174400;
  time_t tv = static_cast<time_t>(kJan1st2008);
  struct tm* t = localtime(&tv);
  ASSERT(t->tm_isdst <= 0);
  return static_cast<double>(t->tm_gmtoff * msPerSecond);
}


void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}


void OS::VPrint(const char* format, va_list args) {
  vprintf(format, args);
}


void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}


void OS::VPrintError(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}


int OS::SNPrintF(char* str, size_t size, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, size, format, args);
  va_end(args);
  return result;
}


int OS::VSNPrintF(char* str, size_t size, const char* format, va_list args) {
  return vsnprintf(str, size, format, args);  // forward to Mac OS X.
}


// We keep the lowest and highest addresses mapped as a quick way of
// determining that pointers are outside the heap (used mostly in assertions
// and verification).  The estimate is conservative, ie, not all addresses in
// 'allocated' space are actually allocated to our heap.  The range is
// [lowest, highest), inclusive on the low and and exclusive on the high end.
static void* lowest_ever_allocated = reinterpret_cast<void*>(-1);
static void* highest_ever_allocated = reinterpret_cast<void*>(0);


static void UpdateAllocatedSpaceLimits(void* address, int size) {
  lowest_ever_allocated = Min(lowest_ever_allocated, address);
  highest_ever_allocated =
      Max(highest_ever_allocated,
          reinterpret_cast<void*>(reinterpret_cast<char*>(address) + size));
}


bool OS::IsOutsideAllocatedSpace(void* address) {
  return address < lowest_ever_allocated || address >= highest_ever_allocated;
}


size_t OS::AllocateAlignment() {
  return kPointerSize;
}


void* OS::Allocate(const size_t requested, size_t* allocated) {
  *allocated = requested;
  void* mbase = malloc(requested);
  UpdateAllocatedSpaceLimits(mbase, requested);
  return mbase;
}


void OS::Free(void* buf, const size_t length) {
  free(buf);
  USE(length);
}


void OS::Sleep(int miliseconds) {
  usleep(1000 * miliseconds);
}


void OS::Abort() {
  // Redirect to std abort to signal abnormal program termination
  abort();
}


class PosixMemoryMappedFile : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, int size)
    : file_(file), memory_(memory), size_(size) { }
  virtual ~PosixMemoryMappedFile();
  virtual void* memory() { return memory_; }
 private:
  FILE* file_;
  void* memory_;
  int size_;
};


OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name, int size,
    void* initial) {
  FILE* file = fopen(name, "w+");
  if (file == NULL) return NULL;
  fwrite(initial, size, 1, file);
  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_) munmap(memory_, size_);
  fclose(file_);
}


void OS::LogSharedLibraryAddresses() {
  // TODO(1233579): Implement.
}


double OS::nan_value() { return NAN; }


int OS::StackWalk(StackFrame* frames, int frames_size) {
#ifndef MAC_OS_X_VERSION_10_5
  return 0;
#else
  void** addresses = NewArray<void*>(frames_size);
  int frames_count = backtrace(addresses, frames_size);

  char** symbols;
  symbols = backtrace_symbols(addresses, frames_count);
  if (symbols == NULL) {
    DeleteArray(addresses);
    return kStackWalkError;
  }

  for (int i = 0; i < frames_count; i++) {
    frames[i].address = addresses[i];
    // Format a text representation of the frame based on the information
    // available.
    SNPrintF(frames[i].text, kStackWalkMaxTextLen, "%s", symbols[i]);
    // Make sure line termination is in place.
    frames[i].text[kStackWalkMaxTextLen - 1] = '\0';
  }

  DeleteArray(addresses);
  free(symbols);

  return frames_count;
#endif
}


// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;


VirtualMemory::VirtualMemory(size_t size, void* address_hint) {
  address_ = mmap(address_hint, size, PROT_NONE,
                  MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
                  kMmapFd, kMmapFdOffset);
  size_ = size;
}


VirtualMemory::~VirtualMemory() {
  if (IsReserved()) {
    if (0 == munmap(address(), size())) address_ = MAP_FAILED;
  }
}


bool VirtualMemory::IsReserved() {
  return address_ != MAP_FAILED;
}


bool VirtualMemory::Commit(void* address, size_t size) {
  if (MAP_FAILED == mmap(address, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANON | MAP_FIXED,
                         kMmapFd, kMmapFdOffset)) {
    return false;
  }

  UpdateAllocatedSpaceLimits(address, size);
  return true;
}


bool VirtualMemory::Uncommit(void* address, size_t size) {
  return mmap(address, size, PROT_NONE,
              MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
              kMmapFd, kMmapFdOffset) != MAP_FAILED;
}

class ThreadHandle::PlatformData : public Malloced {
 public:
  explicit PlatformData(ThreadHandle::Kind kind) {
    Initialize(kind);
  }

  void Initialize(ThreadHandle::Kind kind) {
    switch (kind) {
      case ThreadHandle::SELF: thread_ = pthread_self(); break;
      case ThreadHandle::INVALID: thread_ = kNoThread; break;
    }
  }
  pthread_t thread_;  // Thread handle for pthread.
};



ThreadHandle::ThreadHandle(Kind kind) {
  data_ = new PlatformData(kind);
}


void ThreadHandle::Initialize(ThreadHandle::Kind kind) {
  data_->Initialize(kind);
}


ThreadHandle::~ThreadHandle() {
  delete data_;
}


bool ThreadHandle::IsSelf() const {
  return pthread_equal(data_->thread_, pthread_self());
}


bool ThreadHandle::IsValid() const {
  return data_->thread_ != kNoThread;
}


Thread::Thread() : ThreadHandle(ThreadHandle::INVALID) {
}


Thread::~Thread() {
}


static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  // This is also initialized by the first argument to pthread_create() but we
  // don't know which thread will run first (the original thread or the new
  // one) so we initialize it here too.
  thread->thread_handle_data()->thread_ = pthread_self();
  ASSERT(thread->IsValid());
  thread->Run();
  return NULL;
}


void Thread::Start() {
  pthread_create(&thread_handle_data()->thread_, NULL, ThreadEntry, this);
}


void Thread::Join() {
  pthread_join(thread_handle_data()->thread_, NULL);
}


Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  pthread_key_t key;
  int result = pthread_key_create(&key, NULL);
  USE(result);
  ASSERT(result == 0);
  return static_cast<LocalStorageKey>(key);
}


void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  int result = pthread_key_delete(pthread_key);
  USE(result);
  ASSERT(result == 0);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  return pthread_getspecific(pthread_key);
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  pthread_setspecific(pthread_key, value);
}


void Thread::YieldCPU() {
  sched_yield();
}


class MacOSMutex : public Mutex {
 public:

  MacOSMutex() {
    // For some reason the compiler doesn't allow you to write
    // "this->mutex_ = PTHREAD_..." directly on mac.
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m, &attr);
    mutex_ = m;
  }

  ~MacOSMutex() { pthread_mutex_destroy(&mutex_); }

  int Lock() { return pthread_mutex_lock(&mutex_); }

  int Unlock() { return pthread_mutex_unlock(&mutex_); }

 private:
  pthread_mutex_t mutex_;
};


Mutex* OS::CreateMutex() {
  return new MacOSMutex();
}


class MacOSSemaphore : public Semaphore {
 public:
  explicit MacOSSemaphore(int count) {
    semaphore_create(mach_task_self(), &semaphore_, SYNC_POLICY_FIFO, count);
  }

  ~MacOSSemaphore() {
    semaphore_destroy(mach_task_self(), semaphore_);
  }

  void Wait() { semaphore_wait(semaphore_); }

  void Signal() { semaphore_signal(semaphore_); }

 private:
  semaphore_t semaphore_;
};


Semaphore* OS::CreateSemaphore(int count) {
  return new MacOSSemaphore(count);
}


// TODO(1233584): Implement MacOS support.
Select::Select(int len, Semaphore** sems) {
  FATAL("Not implemented");
}


Select::~Select() {
  FATAL("Not implemented");
}


int Select::WaitSingle() {
  FATAL("Not implemented");
  return 0;
}


void Select::WaitAll() {
  FATAL("Not implemented");
}

#ifdef ENABLE_LOGGING_AND_PROFILING

static ProfileSampler* active_sampler_ = NULL;

static void ProfilerSignalHandler(int signal, siginfo_t* info, void* context) {
  USE(info);
  if (signal != SIGPROF) return;

  // Extracting the sample from the context is extremely machine dependent.
  TickSample sample;
  ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
  mcontext_t& mcontext = ucontext->uc_mcontext;
#if __DARWIN_UNIX03
  sample.pc = mcontext->__ss.__eip;
  sample.sp = mcontext->__ss.__esp;
#else  // !__DARWIN_UNIX03
  sample.pc = mcontext->ss.eip;
  sample.sp = mcontext->ss.esp;
#endif  // __DARWIN_UNIX03
  sample.state = Logger::state();

  if (active_sampler_ == NULL) return;
  active_sampler_->Tick(&sample);
}


class ProfileSampler::PlatformData  : public Malloced {
 public:
  PlatformData() {
    signal_handler_installed_ = false;
  }

  bool signal_handler_installed_;
  struct sigaction old_signal_handler_;
  struct itimerval old_timer_value_;
};


ProfileSampler::ProfileSampler(int interval) {
  data_ = new PlatformData();
  interval_ = interval;
  active_ = false;
}


ProfileSampler::~ProfileSampler() {
  delete data_;
}


void ProfileSampler::Start() {
  // There can only be one active sampler at the time on POSIX
  // platforms.
  if (active_sampler_ != NULL) return;

  // Request profiling signals.
  struct sigaction sa;
  sa.sa_sigaction = ProfilerSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGPROF, &sa, &data_->old_signal_handler_) != 0) return;
  data_->signal_handler_installed_ = true;

  // Set the itimer to generate a tick for each interval.
  itimerval itimer;
  itimer.it_interval.tv_sec = interval_ / 1000;
  itimer.it_interval.tv_usec = (interval_ % 1000) * 1000;
  itimer.it_value.tv_sec = itimer.it_interval.tv_sec;
  itimer.it_value.tv_usec = itimer.it_interval.tv_usec;
  setitimer(ITIMER_PROF, &itimer, &data_->old_timer_value_);

  // Set this sampler as the active sampler.
  active_sampler_ = this;
  active_ = true;
}


void ProfileSampler::Stop() {
  // Restore old signal handler
  if (data_->signal_handler_installed_) {
    setitimer(ITIMER_PROF, &data_->old_timer_value_, NULL);
    sigaction(SIGPROF, &data_->old_signal_handler_, 0);
    data_->signal_handler_installed_ = false;
  }

  // This sampler is no longer the active sampler.
  active_sampler_ = NULL;
  active_ = false;
}

#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal
