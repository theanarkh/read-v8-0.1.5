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

#ifndef V8_ZONE_H_
#define V8_ZONE_H_

namespace v8 { namespace internal {


// The Zone supports very fast allocation of small chunks of
// memory. The chunks cannot be deallocated individually, but instead
// the Zone supports deallocating all chunks in one fast
// operation. The Zone is used to hold temporary data structures like
// the abstract syntax tree, which is deallocated after compilation.

// Note: There is no need to initialize the Zone; the first time an
// allocation is attempted, a segment of memory will be requested
// through a call to malloc().

// Note: The implementation is inherently not thread safe. Do not use
// from multi-threaded code.

class Zone {
 public:
  // Allocate 'size' bytes of memory in the Zone; expands the Zone by
  // allocating new segments of memory on demand using malloc().
  // 分配size大小的内存
  static inline void* New(int size);

  // Delete all objects and free all memory allocated in the Zone.
  // 一次性释放所有内存
  static void DeleteAll();

 private:
  // All pointers returned from New() have this alignment.
  static const int kAlignment = kPointerSize;

  // Never allocate segments smaller than this size in bytes.
  static const int kMinimumSegmentSize = 8 * KB;

  // Never keep segments larger than this size in bytes around.
  static const int kMaximumKeptSegmentSize = 64 * KB;


  // The Zone is intentionally a singleton; you should not try to
  // allocate instances of the class.
  // 不能new 只能调用Zone::New
  Zone() { UNREACHABLE(); }


  // Expand the Zone to hold at least 'size' more bytes and allocate
  // the bytes. Returns the address of the newly allocated chunk of
  // memory in the Zone. Should only be called if there isn't enough
  // room in the Zone already.
  // 扩展内存
  static Address NewExpand(int size);


  // The free region in the current (front) segment is represented as
  // the half-open interval [position, limit). The 'position' variable
  // is guaranteed to be aligned as dictated by kAlignment.
  // 管理内存的首地址和大小限制
  static Address position_;
  static Address limit_;
};


// ZoneObject is an abstraction that helps define classes of objects
// allocated in the Zone. Use it as a base class; see ast.h.
// 对Zone的封装new ZoneObject();即Zone::New(size);
class ZoneObject {
 public:
  // Allocate a new ZoneObject of 'size' bytes in the Zone.
  void* operator new(size_t size) { return Zone::New(size); }

  // Ideally, the delete operator should be private instead of
  // public, but unfortuately the compiler sometimes synthesizes
  // (unused) destructors for classes derived from ZoneObject, which
  // require the operator to be visible. MSVC requires the delete
  // operator to be public.

  // ZoneObjects should never be deleted individually; use
  // Zone::DeleteAll() to delete all zone objects in one go.
  // 禁止delete该类的对象
  void operator delete(void*, size_t) { UNREACHABLE(); }
};

/*
  管理allow_allocation_字段，
  new AssertNoZoneAllocation的时候，保存当前的allow_allocation_,
  设置allow_allocation_为false，析构后，恢复allow_allocation_的值
*/ 
class AssertNoZoneAllocation {
 public:
  AssertNoZoneAllocation() : prev_(allow_allocation_) {
    allow_allocation_ = false;
  }
  ~AssertNoZoneAllocation() { allow_allocation_ = prev_; }
  static bool allow_allocation() { return allow_allocation_; }
 private:
  bool prev_;
  static bool allow_allocation_;
};


// The ZoneListAllocationPolicy is used to specialize the GenericList
// implementation to allocate ZoneLists and their elements in the
// Zone.
class ZoneListAllocationPolicy {
 public:
  // Allocate 'size' bytes of memory in the zone.
  static void* New(int size) {  return Zone::New(size); }

  // De-allocation attempts are silently ignored.
  static void Delete(void* p) { }
};


// ZoneLists are growable lists with constant-time access to the
// elements. The list itself and all its elements are allocated in the
// Zone. ZoneLists cannot be deleted individually; you can delete all
// objects in the Zone by calling Zone::DeleteAll().
template<typename T>
// ZoneList本质上是一个list，ZoneListAllocationPolicy是list里的内存管理器
class ZoneList: public List<T, ZoneListAllocationPolicy> {
 public:
  // Construct a new ZoneList with the given capacity; the length is
  // always zero. The capacity must be non-negative.
  explicit ZoneList(int capacity)
      : List<T, ZoneListAllocationPolicy>(capacity) { }
};


} }  // namespace v8::internal

#endif  // V8_ZONE_H_
