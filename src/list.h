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

#ifndef V8_LIST_H_
#define V8_LIST_H_

namespace v8 { namespace internal {


// ----------------------------------------------------------------------------
// The list is a template for very light-weight lists. We are not
// using the STL because we want full control over space and speed of
// the code. This implementation is based on code by Robert Griesemer
// and Rob Pike.
//
// The list is parameterized by the type of its elements (T) and by an
// allocation policy (P). The policy is used for allocating lists in
// the C free store or the zone; see zone.h.

// Forward defined as
// template <typename T, class P = FreeStoreAllocationPolicy> class List;
template <typename T, class P>
class List {
 public:
  // 构造函数，申请一块内存
  INLINE(explicit List(int capacity)) { Initialize(capacity); }
  // 析构函数，是否内存
  INLINE(~List()) { DeleteData(data_); }
  // 分配内存，P是内存管理的类，如FreeStoreAllocationPolicy，见allocation.h
  INLINE(void* operator new(size_t size)) { return P::New(size); }
  // 释放内存
  INLINE(void operator delete(void* p, size_t)) { return P::Delete(p); }
  // 重载[]运算符，返回对应索引的元素
  inline T& operator[](int i) const  {
    ASSERT(0 <= i && i < length_);
    return data_[i];
  }
  // 取某个索引对应的元素
  inline T& at(int i) const  { return this->operator[](i); }
  // 取最后一个元素
  INLINE(const T& last() const)  {
    ASSERT(!is_empty());
    return this->at(length_ - 1);
  }
  // 判空
  INLINE(bool is_empty() const) { return length_ == 0; }
  // 容量
  INLINE(int length() const) { return length_; }
  // 转成Vector，见utils.h
  Vector<T> ToVector() { return Vector<T>(data_, length_); }

  // Adds a copy of the given 'element' to the end of the list,
  // expanding the list if necessary.
  // 追加元素
  T& Add(const T& element);

  // Added 'count' elements with the value 'value' and returns a
  // vector that allows access to the elements.  The vector is valid
  // until the next change is made to this list.
  Vector<T> AddBlock(const T& value, int count);

  // Removes the i'th element without deleting it even if T is a
  // pointer type; moves all elements above i "down". Returns the
  // removed element.
  T Remove(int i);

  // Removes the last element without deleting it even if T is a
  // pointer type. Returns the removed element.
  INLINE(T RemoveLast()) { return Remove(length_ - 1); }

  // Clears the list by setting the length to zero. Even if T is a
  // pointer type, clearing the list doesn't delete the entries.
  INLINE(void Clear());

  // Drops all but the first 'pos' elements from the list.
  INLINE(void Rewind(int pos));

  // Iterate through all list entries, starting at index 0.
  void Iterate(void (*callback)(T* x));

  // Sort all list entries (using QuickSort)
  void Sort(int (*cmp)(const T* x, const T* y));

  INLINE(void Initialize(int capacity));

 private:
  // 指向数组的首地址
  T* data_;
  // 最多可以存储多少个元素
  int capacity_;
  // 已经存储了多少个元素
  int length_;

  INLINE(T* NewData(int n))  { return static_cast<T*>(P::New(n * sizeof(T))); }
  INLINE(void DeleteData(T* data))  { P::Delete(data); }

  DISALLOW_EVIL_CONSTRUCTORS(List);
};


} }  // namespace v8::internal

#endif  // V8_LIST_H_
