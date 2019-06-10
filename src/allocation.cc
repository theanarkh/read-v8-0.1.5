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

#include <stdlib.h>

#include "v8.h"

namespace v8 { namespace internal {

// 对c函数的封装
void* Malloced::New(size_t size) {
  ASSERT(NativeAllocationChecker::allocation_allowed());
  void* result = malloc(size);
  if (result == NULL) V8::FatalProcessOutOfMemory("Malloced operator new");
  return result;
}


void Malloced::Delete(void* p) {
  free(p);
}


void Malloced::FatalProcessOutOfMemory() {
  V8::FatalProcessOutOfMemory("Out of memory");
}


#ifdef DEBUG

static void* invalid = static_cast<void*>(NULL);

void* Embedded::operator new(size_t size) {
  UNREACHABLE();
  return invalid;
}


void Embedded::operator delete(void* p) {
  UNREACHABLE();
}


void* AllStatic::operator new(size_t size) {
  UNREACHABLE();
  return invalid;
}


void AllStatic::operator delete(void* p) {
  UNREACHABLE();
}

#endif

// 复制字符串
char* StrDup(const char* str) {
  int length = strlen(str);
  // 申请一个字符数组
  char* result = NewArray<char>(length + 1);
  // 复制过去
  memcpy(result, str, length * kCharSize);
  result[length] = '\0';
  return result;
}


int NativeAllocationChecker::allocation_disallowed_ = 0;

// 初始化属性
PreallocatedStorage PreallocatedStorage::in_use_list_(0);
PreallocatedStorage PreallocatedStorage::free_list_(0);
bool PreallocatedStorage::preallocated_ = false;

// 申请一块内存对其进行管理
void PreallocatedStorage::Init(size_t size) {
  ASSERT(free_list_.next_ == &free_list_);
  ASSERT(free_list_.previous_ == &free_list_);
  // 申请size个字节，前n个字节是一个PreallocatedStorage对象
  PreallocatedStorage* free_chunk =
      reinterpret_cast<PreallocatedStorage*>(new char[size]);
  // 初始化链表，双向循环链表
  free_list_.next_ = free_list_.previous_ = free_chunk;
  free_chunk->next_ = free_chunk->previous_ = &free_list_;
  // 大小是申请的大小减去一个PreallocatedStorage对象
  free_chunk->size_ = size - sizeof(PreallocatedStorage);
  // 已经分配了内存
  preallocated_ = true;
}

// 从预分配的内存里分配一块内存
void* PreallocatedStorage::New(size_t size) {
  // 没有使用预分配内存，则直接到底层申请一块新的内存，否则从预分配的内存里分配
  if (!preallocated_) {
    return FreeStoreAllocationPolicy::New(size);
  }
  ASSERT(free_list_.next_ != &free_list_);
  ASSERT(free_list_.previous_ != &free_list_);
  /*
    ~(kPointerSize - 1)是使高n位取反，n取决于kPointerSize的大小，即1的位置。 
    size + (kPointerSize - 1)是如果没有按kPointerSize对齐则向上取整。 
  */
  size = (size + kPointerSize - 1) & ~(kPointerSize - 1);
  // Search for exact fit.
  // 从预分配的内存里找到等于size的块
  for (PreallocatedStorage* storage = free_list_.next_;
       storage != &free_list_;
       storage = storage->next_) {
    if (storage->size_ == size) {
      // 找到后，把该块从链表中删除，并插入到已分配链表中
      storage->Unlink();
      storage->LinkTo(&in_use_list_);
      // 返回存储数据的首地址，前面存储了一个PreallocatedStorage对象
      return reinterpret_cast<void*>(storage + 1);
    }
  }
  // Search for first fit.
  // 没有大小等于size的块，则找比size大的块
  for (PreallocatedStorage* storage = free_list_.next_;
       storage != &free_list_;
       storage = storage->next_) {
    // 多出来的那一块还需要一个PreallocatedStorage对象进行管理
    if (storage->size_ >= size + sizeof(PreallocatedStorage)) {
      storage->Unlink();
      storage->LinkTo(&in_use_list_);
      // 分配一部分出去，storage + 1即可用于存储数据的首地址，加上size得到还剩下的空闲内存首地址
      PreallocatedStorage* left_over =
          reinterpret_cast<PreallocatedStorage*>(
              reinterpret_cast<char*>(storage + 1) + size);
      // 剩下的大小等于本来的大小减去size-一个PreallocatedStorage对象
      left_over->size_ = storage->size_ - size - sizeof(PreallocatedStorage);
      ASSERT(size + left_over->size_ + sizeof(PreallocatedStorage) ==
             storage->size_);
      // 更新原来的storage的大小，为请求的size，stroage被切分了
      storage->size_ = size;
      // 剩下的插入空闲链表
      left_over->LinkTo(&free_list_);
      // 返回可用于存储数据的首地址
      return reinterpret_cast<void*>(storage + 1);
    }
  }
  // Allocation failure.
  ASSERT(false);
  return NULL;
}


// We don't attempt to coalesce.
// 释放内存，不作合并处理，p是存储数据的首地址
void PreallocatedStorage::Delete(void* p) {
  if (p == NULL) {
    return;
  }
  // 参考New
  if (!preallocated_) {
    FreeStoreAllocationPolicy::Delete(p);
    return;
  }
  // 转成PreallocatedStorage指针，减一则指向管理这块内存的PreallocatedStorage对象
  PreallocatedStorage* storage = reinterpret_cast<PreallocatedStorage*>(p) - 1;
  ASSERT(storage->next_->previous_ == storage);
  ASSERT(storage->previous_->next_ == storage);
  // 脱离原来的链表，插入空闲链表
  storage->Unlink();
  storage->LinkTo(&free_list_);
}


void PreallocatedStorage::LinkTo(PreallocatedStorage* other) {
  next_ = other->next_;
  other->next_->previous_ = this;
  previous_ = other;
  other->next_ = this;
}


void PreallocatedStorage::Unlink() {
  next_->previous_ = previous_;
  previous_->next_ = next_;
}


PreallocatedStorage::PreallocatedStorage(size_t size)
  : size_(size) {
  previous_ = next_ = this;
}

} }  // namespace v8::internal
