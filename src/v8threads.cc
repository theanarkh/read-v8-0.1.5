// Copyright 2008 Google Inc. All Rights Reserved.
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

#include "v8.h"

#include "api.h"
#include "debug.h"
#include "execution.h"
#include "v8threads.h"

namespace v8 {

static internal::Thread::LocalStorageKey thread_state_key =
    internal::Thread::CreateThreadLocalKey();

// Constructor for the Locker object.  Once the Locker is constructed the
// current thread will be guaranteed to have the big V8 lock.
Locker::Locker() : has_lock_(false), top_level_(true) {
  // Get the big lock if necessary.
  if (!internal::ThreadManager::IsLockedByCurrentThread()) {
    internal::ThreadManager::Lock();
    has_lock_ = true;
    // This may be a locker within an unlocker in which case we have to
    // get the saved state for this thread and restore it.
    if (internal::ThreadManager::RestoreThread()) {
      top_level_ = false;
    }
  }
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
}


#ifdef DEBUG
void Locker::AssertIsLocked() {
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
}
#endif


Locker::~Locker() {
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
  if (has_lock_) {
    if (!top_level_) {
      internal::ThreadManager::ArchiveThread();
    }
    internal::ThreadManager::Unlock();
  }
}


Unlocker::Unlocker() {
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
  internal::ThreadManager::ArchiveThread();
  internal::ThreadManager::Unlock();
}


Unlocker::~Unlocker() {
  ASSERT(!internal::ThreadManager::IsLockedByCurrentThread());
  internal::ThreadManager::Lock();
  internal::ThreadManager::RestoreThread();
}


void Locker::StartPreemption(int every_n_ms) {
  v8::internal::ContextSwitcher::StartPreemption(every_n_ms);
}


void Locker::StopPreemption() {
  v8::internal::ContextSwitcher::StopPreemption();
}


namespace internal {


bool ThreadManager::RestoreThread() {
  // First check whether the current thread has been 'lazily archived', ie
  // not archived at all.  If that is the case we put the state storage we
  // had prepared back in the free list, since we didn't need it after all.
  if (lazily_archived_thread_.IsSelf()) {
    lazily_archived_thread_.Initialize(ThreadHandle::INVALID);
    ASSERT(Thread::GetThreadLocal(thread_state_key) ==
           lazily_archived_thread_state_);
    lazily_archived_thread_state_->LinkInto(ThreadState::FREE_LIST);
    lazily_archived_thread_state_ = NULL;
    Thread::SetThreadLocal(thread_state_key, NULL);
    return true;
  }
  // If there is another thread that was lazily archived then we have to really
  // archive it now.
  if (lazily_archived_thread_.IsValid()) {
    EagerlyArchiveThread();
  }
  ThreadState* state =
      reinterpret_cast<ThreadState*>(Thread::GetThreadLocal(thread_state_key));
  if (state == NULL) {
    return false;
  }
  char* from = state->data();
  from = HandleScopeImplementer::RestoreThread(from);
  from = Top::RestoreThread(from);
  from = Debug::RestoreDebug(from);
  from = StackGuard::RestoreStackGuard(from);
  Thread::SetThreadLocal(thread_state_key, NULL);
  state->Unlink();
  state->LinkInto(ThreadState::FREE_LIST);
  return true;
}


void ThreadManager::Lock() {
  mutex_->Lock();
  mutex_owner_.Initialize(ThreadHandle::SELF);
  ASSERT(IsLockedByCurrentThread());
}


void ThreadManager::Unlock() {
  mutex_owner_.Initialize(ThreadHandle::INVALID);
  mutex_->Unlock();
}


static int ArchiveSpacePerThread() {
  return HandleScopeImplementer::ArchiveSpacePerThread() +
                            Top::ArchiveSpacePerThread() +
                          Debug::ArchiveSpacePerThread() +
                     StackGuard::ArchiveSpacePerThread();
}


ThreadState* ThreadState::free_anchor_ = new ThreadState();
ThreadState* ThreadState::in_use_anchor_ = new ThreadState();


ThreadState::ThreadState() : next_(this), previous_(this) {
}


void ThreadState::AllocateSpace() {
  data_ = NewArray<char>(ArchiveSpacePerThread());
}


void ThreadState::Unlink() {
  next_->previous_ = previous_;
  previous_->next_ = next_;
}


void ThreadState::LinkInto(List list) {
  ThreadState* flying_anchor =
      list == FREE_LIST ? free_anchor_ : in_use_anchor_;
  next_ = flying_anchor->next_;
  previous_ = flying_anchor;
  flying_anchor->next_ = this;
  next_->previous_ = this;
}


ThreadState* ThreadState::GetFree() {
  ThreadState* gotten = free_anchor_->next_;
  if (gotten == free_anchor_) {
    ThreadState* new_thread_state = new ThreadState();
    new_thread_state->AllocateSpace();
    return new_thread_state;
  }
  return gotten;
}


// Gets the first in the list of archived threads.
ThreadState* ThreadState::FirstInUse() {
  return in_use_anchor_->Next();
}


ThreadState* ThreadState::Next() {
  if (next_ == in_use_anchor_) return NULL;
  return next_;
}


Mutex* ThreadManager::mutex_ = OS::CreateMutex();
ThreadHandle ThreadManager::mutex_owner_(ThreadHandle::INVALID);
ThreadHandle ThreadManager::lazily_archived_thread_(ThreadHandle::INVALID);
ThreadState* ThreadManager::lazily_archived_thread_state_ = NULL;


void ThreadManager::ArchiveThread() {
  ASSERT(!lazily_archived_thread_.IsValid());
  ASSERT(Thread::GetThreadLocal(thread_state_key) == NULL);
  ThreadState* state = ThreadState::GetFree();
  state->Unlink();
  Thread::SetThreadLocal(thread_state_key, reinterpret_cast<void*>(state));
  lazily_archived_thread_.Initialize(ThreadHandle::SELF);
  lazily_archived_thread_state_ = state;
}


void ThreadManager::EagerlyArchiveThread() {
  ThreadState* state = lazily_archived_thread_state_;
  state->LinkInto(ThreadState::IN_USE_LIST);
  char* to = state->data();
  to = HandleScopeImplementer::ArchiveThread(to);
  to = Top::ArchiveThread(to);
  to = Debug::ArchiveDebug(to);
  to = StackGuard::ArchiveStackGuard(to);
  lazily_archived_thread_.Initialize(ThreadHandle::INVALID);
  lazily_archived_thread_state_ = NULL;
}


void ThreadManager::Iterate(ObjectVisitor* v) {
  // Expecting no threads during serialization/deserialization
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data = HandleScopeImplementer::Iterate(v, data);
    data = Top::Iterate(v, data);
  }
}


void ThreadManager::MarkCompactPrologue() {
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    Top::MarkCompactPrologue(data);
  }
}


void ThreadManager::MarkCompactEpilogue() {
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    Top::MarkCompactEpilogue(data);
  }
}


ContextSwitcher::ContextSwitcher(int every_n_ms)
  : preemption_semaphore_(OS::CreateSemaphore(0)),
    keep_going_(true),
    sleep_ms_(every_n_ms) {
}


static v8::internal::ContextSwitcher* switcher;


void ContextSwitcher::StartPreemption(int every_n_ms) {
  Locker::AssertIsLocked();
  if (switcher == NULL) {
    switcher = new ContextSwitcher(every_n_ms);
    switcher->Start();
  } else {
    switcher->sleep_ms_ = every_n_ms;
  }
}


void ContextSwitcher::StopPreemption() {
  Locker::AssertIsLocked();
  if (switcher != NULL) {
    switcher->Stop();
    delete(switcher);
    switcher = NULL;
  }
}


void ContextSwitcher::Run() {
  while (keep_going_) {
    OS::Sleep(sleep_ms_);
    StackGuard::Preempt();
    WaitForPreemption();
  }
}


void ContextSwitcher::Stop() {
  Locker::AssertIsLocked();
  keep_going_ = false;
  preemption_semaphore_->Signal();
  Join();
}


void ContextSwitcher::WaitForPreemption() {
  preemption_semaphore_->Wait();
}


void ContextSwitcher::PreemptionReceived() {
  Locker::AssertIsLocked();
  switcher->preemption_semaphore_->Signal();
}


}  // namespace internal
}  // namespace v8
