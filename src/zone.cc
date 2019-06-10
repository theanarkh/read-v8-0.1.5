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

#include "v8.h"

#include "zone-inl.h"

namespace v8 { namespace internal {


Address Zone::position_ = 0;
Address Zone::limit_ = 0;

bool AssertNoZoneAllocation::allow_allocation_ = true;


// Segments represent chunks of memory: They have starting address
// (encoded in the this pointer) and a size in bytes. Segments are
// chained together forming a LIFO structure with the newest segment
// available as Segment::head(). Segments are allocated using malloc()
// and de-allocated using free().

class Segment {
 public:
  // 下一个节点
  Segment* next() const { return next_; }
  // 断开指向下一个节点的指针
  void clear_next() { next_ = NULL; }
  // 内存总大小
  int size() const { return size_; }
  // 内存可用大小，前面有一个Segment对象
  int capacity() const { return size_ - sizeof(Segment); }
  // 内存的开始地址，即Segement对象
  Address start() const { return address(sizeof(Segment)); }
  // 结束地址即首地址加上size
  Address end() const { return address(size_); }
  // 返回第一个Segment节点
  static Segment* head() { return head_; }
  static void set_head(Segment* head) { head_ = head; }

  // Creates a new segment, sets it size, and pushes it to the front
  // of the segment chain. Returns the new segment.
  // 新增一个Segment
  static Segment* New(int size) {
    Segment* result = reinterpret_cast<Segment*>(Malloced::New(size));
    // 分配成功
    if (result != NULL) {
      // 头插入插入链表，size是分配的总大小
      result->next_ = head_;
      result->size_ = size;
      head_ = result;
    }
    return result;
  }

  // Deletes the given segment. Does not touch the segment chain.
  // 释放segment节点
  static void Delete(Segment* segment) {
    Malloced::Delete(segment);
  }

 private:
  // Computes the address of the nth byte in this segment.
  // 首地址加上n个字节
  Address address(int n) const {
    return Address(this) + n;
  }
  // 管理所有segment节点的头指针
  static Segment* head_;
  // 每个segment节点的属性
  Segment* next_;
  int size_;
};


Segment* Segment::head_ = NULL;


void Zone::DeleteAll() {
#ifdef DEBUG
  // Constant byte value used for zapping dead memory in debug mode.
  static const unsigned char kZapDeadByte = 0xcd;
#endif

  // Find a segment with a suitable size to keep around.
  Segment* keep = Segment::head();
  // 到末节点或者小于kMaximumKeptSegmentSize大小的节点
  while (keep != NULL && keep->size() > kMaximumKeptSegmentSize) {
    keep = keep->next();
  }

  // Traverse the chained list of segments, zapping (in debug mode)
  // and freeing every segment except the one we wish to keep.
  Segment* current = Segment::head();
  // 处理keep节点，其余节点的内存都被释放
  while (current != NULL) {
    Segment* next = current->next();
    if (current == keep) {
      // Unlink the segment we wish to keep from the list.
      current->clear_next();
    } else {
#ifdef DEBUG
      // Zap the entire current segment (including the header).
      memset(current, kZapDeadByte, current->size());
#endif
      Segment::Delete(current);
    }
    current = next;
  }

  // If we have found a segment we want to keep, we must recompute the
  // variables 'position' and 'limit' to prepare for future allocate
  // attempts. Otherwise, we must clear the position and limit to
  // force a new segment to be allocated on demand.
  // 更新属性，有保留的内存则用于下次分配
  if (keep != NULL) {
    Address start = keep->start();
    position_ = RoundUp(start, kAlignment);
    limit_ = keep->end();
#ifdef DEBUG
    // Zap the contents of the kept segment (but not the header).
    memset(start, kZapDeadByte, keep->capacity());
#endif
  } else {
    position_ = limit_ = 0;
  }

  // Update the head segment to be the kept segment (if any).
  // 更新头指针
  Segment::set_head(keep);
}


Address Zone::NewExpand(int size) {
  // Make sure the requested size is already properly aligned and that
  // there isn't enough room in the Zone to satisfy the request.
  ASSERT(size == RoundDown(size, kAlignment));
  ASSERT(position_ + size > limit_);

  // Compute the new segment size. We use a 'high water mark'
  // strategy, where we increase the segment size every time we expand
  // except that we employ a maximum segment size when we delete. This
  // is to avoid excessive malloc() and free() overhead.
  Segment* head = Segment::head();
  int old_size = (head == NULL) ? 0 : head->size();
  int new_size = sizeof(Segment) + kAlignment + size + (old_size << 1);
  if (new_size < kMinimumSegmentSize) new_size = kMinimumSegmentSize;
  // 分配一个新的segment节点插入到链表
  Segment* segment = Segment::New(new_size);
  if (segment == NULL) V8::FatalProcessOutOfMemory("Zone");

  // Recompute 'top' and 'limit' based on the new segment.
  Address result = RoundUp(segment->start(), kAlignment);
  // 更新属性，下次分配的时候使用
  position_ = result + size;
  limit_ = segment->end();
  ASSERT(position_ <= limit_);
  return result;
}


} }  // namespace v8::internal
