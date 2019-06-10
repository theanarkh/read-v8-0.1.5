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

#ifndef V8_LIST_INL_H_
#define V8_LIST_INL_H_

#include "list.h"

namespace v8 { namespace internal {


template<typename T, class P>
T& List<T, P>::Add(const T& element) {
  // 没有足够的空间了，重新申请一块新的更大的内存
  if (length_ >= capacity_) {
    // Grow the list capacity by 50%, but make sure to let it grow
    // even when the capacity is zero (possible initial case).
    // 多扩展50% + 1的空间，加一是兼容capacity_为0的情况
    int new_capacity = 1 + capacity_ + (capacity_ >> 1);
    // 申请一块内存
    T* new_data = NewData(new_capacity);
    // 把原来的复制过去
    memcpy(new_data, data_, capacity_ * sizeof(T));
    // 删除原来的
    DeleteData(data_);
    // 更新属性
    data_ = new_data;
    capacity_ = new_capacity;
  }
  // 赋值
  return data_[length_++] = element;
}


template<typename T, class P>
// 新增count个element元素到list，转成Vector返回
Vector<T> List<T, P>::AddBlock(const T& element, int count) {
  int start = length_;
  for (int i = 0; i < count; i++)
    Add(element);
  return Vector<T>(&data_[start], count);
}


template<typename T, class P>
// 删除某个元素，后续的元素补位
T List<T, P>::Remove(int i) {
  // 取出i对应的元素
  T element = at(i);
  // 长度减一
  length_--;
  // 如果删除的不是最后一个元素，则i后面的元素要往前补位
  while (i < length_) {
    data_[i] = data_[i + 1];
    i++;
  }
  return element;
}


template<typename T, class P>
// 清空list，释放内存
void List<T, P>::Clear() {
  DeleteData(data_);
  Initialize(0);
}

// 改变list的长度 
template<typename T, class P>
void List<T, P>::Rewind(int pos) {
  length_ = pos;
}

// 迭代list
template<typename T, class P>
void List<T, P>::Iterate(void (*callback)(T* x)) {
  for (int i = 0; i < length_; i++) callback(&data_[i]);
}

// 排序
template<typename T, class P>
void List<T, P>::Sort(int (*cmp)(const T* x, const T* y)) {
  qsort(data_,
        length_,
        sizeof(T),
        reinterpret_cast<int (*)(const void*, const void*)>(cmp));
#ifdef DEBUG
  for (int i = 1; i < length_; i++)
    ASSERT(cmp(&data_[i - 1], &data_[i]) <= 0);
#endif
}


template<typename T, class P>
void List<T, P>::Initialize(int capacity) {
  ASSERT(capacity >= 0);
  data_ = (capacity > 0) ? NewData(capacity) : NULL;
  capacity_ = capacity;
  length_ = 0;
}


} }  // namespace v8::internal

#endif  // V8_LIST_INL_H_
