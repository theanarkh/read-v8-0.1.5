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

#include "hashmap.h"

namespace v8 { namespace internal {

/*
  判断x是不是有且仅有一位是1.如果是则下面的式子成立。
  假设x的第n位是1，x - 1后，n的左边位都是0，右边都是1，n变成0.
  00001000 => 00000111,再和x与，n以及n的右边位是肯定为0的。右边就看
  n的左边的位就可以了。
*/
static inline bool IsPowerOf2(uint32_t x) {
  ASSERT(x != 0);
  return (x & (x - 1)) == 0;
}

// 内存分配器
Allocator HashMap::DefaultAllocator;

// 默认构造函数
HashMap::HashMap() {
  allocator_ = NULL;
  match_ = NULL;
}

// 初始化属性，分配内存
HashMap::HashMap(MatchFun match,
                 Allocator* allocator,
                 uint32_t initial_capacity) {
  allocator_ = allocator;
  match_ = match;
  Initialize(initial_capacity);
}

// 析构函数，释放内存
HashMap::~HashMap() {
  if (allocator_) {
    allocator_->Delete(map_);
  }
}

// 查找或插入一个元素
HashMap::Entry* HashMap::Lookup(void* key, uint32_t hash, bool insert) {
  // Find a matching entry.
  // 找到key和hash对应的索引。
  Entry* p = Probe(key, hash);
  // 找到则返回
  if (p->key != NULL) {
    return p;
  }

  // No entry found; insert one if necessary.
  // 没有找到判断是否需要插入
  if (insert) {
    p->key = key;
    p->value = NULL;
    p->hash = hash;
    // 更新使用的元素个数
    occupancy_++;

    // Grow the map if we reached >= 80% occupancy.
    // 分配的元素过多，重新分配内存，否则导致冲突频繁，影响效率
    if (occupancy_ + occupancy_/4 >= capacity_) {
      Resize();
      // 重新查找对应的元素
      p = Probe(key, hash);
    }

    return p;
  }

  // No entry found and none inserted.
  return NULL;
}

// 
void HashMap::Clear() {
  // Mark all entries as empty.
  // 最后一个元素的末地址
  const Entry* end = map_end();
  // 遍历数组，清空key字段
  for (Entry* p = map_; p < end; p++) {
    p->key = NULL;
  }
  // 分配出去的元素个数为0
  occupancy_ = 0;
}

// 用于迭代
HashMap::Entry* HashMap::Start() const {
  // Next函数的for执行了p++，所以这里要回退一个元素，见Next函数
  return Next(map_ - 1);
}


HashMap::Entry* HashMap::Next(Entry* p) const {
  // 最后一个元素的末地址
  const Entry* end = map_end();
  ASSERT(map_ - 1 <= p && p < end);
  /*
    遍历数组，返回遇到的第一个key非空的节点，
    p++，所以初始化的时候，p指向第一个元素的第一个元素
  */
  for (p++; p < end; p++) {
    if (p->key != NULL) {
      return p;
    }
  }
  return NULL;
}

// 根据key和hash找到哈希表中可用的索引，hash值由调用方提供
HashMap::Entry* HashMap::Probe(void* key, uint32_t hash) {
  ASSERT(key != NULL);

  ASSERT(IsPowerOf2(capacity_));
  // capacity_ - 1防止溢出，实现回环  
  Entry* p = map_ + (hash & (capacity_ - 1));
  // 最后一个元素的末地址
  const Entry* end = map_end();
  ASSERT(map_ <= p && p < end);
  // 至少有一个非NULL，使p->key != NULL成立
  ASSERT(occupancy_ < capacity_);  // guarantees loop termination
  /*
    如果key等于空说明这个项还没被使用，则返回，
    如果key非空，并且hash和key都匹配，则返回。
    hash值不相等或者名字不match,则查找下一个可用的元素，即开放地址法
  */
  while (p->key != NULL && (hash != p->hash || !match_(key, p->key))) {
    p++;
    // 到底了，从头开始
    if (p >= end) {
      p = map_;
    }
  }

  return p;
}

// 申请一个Entry* 数组
void HashMap::Initialize(uint32_t capacity) {
  ASSERT(IsPowerOf2(capacity));
  map_ = reinterpret_cast<Entry*>(allocator_->New(capacity * sizeof(Entry)));
  if (map_ == NULL) V8::FatalProcessOutOfMemory("HashMap::Initialize");
  capacity_ = capacity;
  // 初始化内存数据
  Clear();
}

// 扩展
void HashMap::Resize() {
  // 先保存旧地址的指针
  Entry* map = map_;
  uint32_t n = occupancy_;
  // 重新分配一个更大的数组
  // Allocate larger map.
  Initialize(capacity_ * 2);

  // Rehash all current entries.
  // 重新计算当前哈希表中的元素的位置，n的作用是迁移完n个可用退出循环了，不需要遍历到底
  for (Entry* p = map; n > 0; p++) {
    if (p->key != NULL) {
      // 把旧的元素插入到新的数组中，因为map_更新了，里面是空的，所以会一直插入新的元素到map_
      Lookup(p->key, p->hash, true)->value = p->value;
      n--;
    }
  }
  // 释放旧的地址
  // Delete old map.
  allocator_->Delete(map);
}


} }  // namespace v8::internal
