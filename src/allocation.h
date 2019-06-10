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

#ifndef V8_ALLOCATION_H_
#define V8_ALLOCATION_H_

namespace v8 { namespace internal {


// A class that controls whether allocation is allowed.  This is for
// the C++ heap only!
class NativeAllocationChecker {
 public:
  typedef enum { ALLOW, DISALLOW } NativeAllocationAllowed;
  explicit inline NativeAllocationChecker(NativeAllocationAllowed allowed)
      : allowed_(allowed) {
#ifdef DEBUG
    if (allowed == DISALLOW) {
      allocation_disallowed_++;
    }
#endif
  }
  ~NativeAllocationChecker() {
#ifdef DEBUG
    if (allowed_ == DISALLOW) {
      allocation_disallowed_--;
    }
#endif
    ASSERT(allocation_disallowed_ >= 0);
  }
  static inline bool allocation_allowed() {
    return allocation_disallowed_ == 0;
  }
 private:
  // This static counter ensures that NativeAllocationCheckers can be nested.
  static int allocation_disallowed_;
  // This flag applies to this particular instance.
  NativeAllocationAllowed allowed_;
};


// Superclass for classes managed with new & delete.
// 管理内存的类，对c函数进行了封装
class Malloced {
 public:
  void* operator new(size_t size) { return New(size); }
  void  operator delete(void* p) { Delete(p); }

  static void FatalProcessOutOfMemory();
  static void* New(size_t size);
  static void Delete(void* p);
};


// A macro is used for defining the base class used for embedded instances.
// The reason is some compilers allocate a minimum of one word for the
// superclass. The macro prevents the use of new & delete in debug mode.
// In release mode we are not willing to pay this overhead.

#ifdef DEBUG
// Superclass for classes with instances allocated inside stack
// activations or inside other objects.
class Embedded {
 public:
  void* operator new(size_t size);
  void  operator delete(void* p);
};
#define BASE_EMBEDDED : public Embedded
#else
#define BASE_EMBEDDED
#endif


// Superclass for classes only using statics.
// 类里只有静态成员
class AllStatic {
#ifdef DEBUG
 public:
  void* operator new(size_t size);
  void operator delete(void* p);
#endif
};

// 新建一个T类型的数组
template <typename T>
static T* NewArray(int size) {
  ASSERT(NativeAllocationChecker::allocation_allowed());
  T* result = new T[size];
  if (result == NULL) Malloced::FatalProcessOutOfMemory();
  return result;
}


template <typename T>
static void DeleteArray(T* array) {
  delete[] array;
}


// The normal strdup function uses malloc.  This version of StrDup
// uses new and calls the FatalProcessOutOfMemory handler if
// allocation fails.
// 复制字符串
char* StrDup(const char* str);


// Allocation policy for allocating in the C free store using malloc
// and free. Used as the default policy for lists.
class FreeStoreAllocationPolicy {
 public:
  INLINE(static void* New(size_t size)) { return Malloced::New(size); }
  INLINE(static void Delete(void* p)) { Malloced::Delete(p); }
};


// Allocation policy for allocating in preallocated space.
// Used as an allocation policy for ScopeInfo when generating
// stack traces.
// 内存管理 
class PreallocatedStorage : public AllStatic {
 public:
  // 管理内存的大小
  explicit PreallocatedStorage(size_t size);
  size_t size() { return size_; }
  static void* New(size_t size);
  static void Delete(void* p);

  // Preallocate a set number of bytes.
  static void Init(size_t size);

 private:
  size_t size_;
  // 链表
  PreallocatedStorage* previous_;
  PreallocatedStorage* next_;
  static bool preallocated_;
  // 已分配出去的内存链表
  static PreallocatedStorage in_use_list_;
  // 空闲链表
  static PreallocatedStorage free_list_;
  // 链表操作函数
  void LinkTo(PreallocatedStorage* other);
  void Unlink();
  DISALLOW_IMPLICIT_CONSTRUCTORS(PreallocatedStorage);
};


} }  // namespace v8::internal

#endif  // V8_ALLOCATION_H_
