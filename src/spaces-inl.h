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

#ifndef V8_SPACES_INL_H_
#define V8_SPACES_INL_H_

#include "memory.h"
#include "spaces.h"

namespace v8 { namespace internal {


// -----------------------------------------------------------------------------
// HeapObjectIterator

bool HeapObjectIterator::has_next() {
  if (cur_addr_ < cur_limit_) {
    return true;  // common case
  }
  ASSERT(cur_addr_ == cur_limit_);
  return HasNextInNextPage();  // slow path
}


HeapObject* HeapObjectIterator::next() {
  ASSERT(has_next());

  HeapObject* obj = HeapObject::FromAddress(cur_addr_);
  int obj_size = (size_func_ == NULL) ? obj->Size() : size_func_(obj);
  ASSERT_OBJECT_SIZE(obj_size);

  cur_addr_ += obj_size;
  ASSERT(cur_addr_ <= cur_limit_);

  return obj;
}


// -----------------------------------------------------------------------------
// PageIterator

bool PageIterator::has_next() {
  return cur_page_ != stop_page_;
}

// 迭代函数，返回当前的地址，更新下一页的地址
Page* PageIterator::next() {
  ASSERT(has_next());
  Page* result = cur_page_;
  cur_page_ = cur_page_->next_page();
  return result;
}


// -----------------------------------------------------------------------------
// Page
// 下一个page，在page的对象head里保存了下一个page的地址
Page* Page::next_page() {
  return MemoryAllocator::GetNextPage(this);
}


Address Page::AllocationTop() {
  PagedSpace* owner = MemoryAllocator::PageOwner(this);
  if (Heap::old_space() == owner) {
    return Heap::old_space()->PageAllocationTop(this);
  } else if (Heap::code_space() == owner) {
    return Heap::code_space()->PageAllocationTop(this);
  } else {
    ASSERT(Heap::map_space() == owner);
    return Heap::map_space()->PageAllocationTop(this);
  }
}

// 清除用来做记录集的内存的数据
void Page::ClearRSet() {
  // This method can be called in all rset states.
  memset(RSetStart(), 0, kRSetEndOffset - kRSetStartOffset);
}


// Give an address a (32-bits):
// | page address | words (6) | bit offset (5) | pointer alignment (2) |
// The rset address is computed as:
//    page_address + words * 4
// 计算一个地址在记录集中的位置，并返回虚拟地址
Address Page::ComputeRSetBitPosition(Address address, int offset,
                                     uint32_t* bitmask) {
  ASSERT(Page::is_rset_in_use());
  // 地址所属的page
  Page* page = Page::FromAddress(address);
  // 算出address+offset相对记录集来说的偏移
  uint32_t bit_offset = ArithmeticShiftRight(page->Offset(address) + offset,
                                             kObjectAlignmentBits);
  // 算出偏移在记录集里某个字节中的偏移                                          
  *bitmask = 1 << (bit_offset % kBitsPerInt);
  // 算出偏移在记录集中所属的int（记录集相当于一个int数组），加上page地址，得到虚拟地址
  Address rset_address =
      page->address() + (bit_offset / kBitsPerInt) * kIntSize;
  // The remembered set address is either in the normal remembered set range
  // of a page or else we have a large object page.
  ASSERT((page->RSetStart() <= rset_address && rset_address < page->RSetEnd())
         || page->IsLargeObjectPage());

  if (rset_address >= page->RSetEnd()) {
    // We have a large object page, and the remembered set address is actually
    // past the end of the object.  The address of the remembered set in this
    // case is the extra remembered set start address at the address of the
    // end of the object:
    //   (page->ObjectAreaStart() + object size)
    // plus the offset of the computed remembered set address from the start
    // of the object:
    //   (rset_address - page->ObjectAreaStart()).
    // Ie, we can just add the object size.
    ASSERT(HeapObject::FromAddress(address)->IsFixedArray());
    rset_address +=
        FixedArray::SizeFor(Memory::int_at(page->ObjectAreaStart()
                                           + Array::kLengthOffset));
  }
  return rset_address;
}

// 设置记录集
void Page::SetRSet(Address address, int offset) {
  uint32_t bitmask = 0;
  // 记录所属的位置和偏移
  Address rset_address = ComputeRSetBitPosition(address, offset, &bitmask偏移
  // 设置
  Memory::uint32_at(rset_address) |= bitmask;

  ASSERT(IsRSetSet(address, offset));
}


// Clears the corresponding remembered set bit for a given address.
// 同上
void Page::UnsetRSet(Address address, int offset) {
  uint32_t bitmask = 0;
  Address rset_address = ComputeRSetBitPosition(address, offset, &bitmask);
  Memory::uint32_at(rset_address) &= ~bitmask;

  ASSERT(!IsRSetSet(address, offset));
}

// 同上
bool Page::IsRSetSet(Address address, int offset) {
  uint32_t bitmask = 0;
  Address rset_address = ComputeRSetBitPosition(address, offset, &bitmask);
  return (Memory::uint32_at(rset_address) & bitmask) != 0;
}


// -----------------------------------------------------------------------------
// MemoryAllocator
// 检查chunk是否管理着有效内存
bool MemoryAllocator::IsValidChunk(int chunk_id) {
  if (!IsValidChunkId(chunk_id)) return false;

  ChunkInfo& c = chunks_[chunk_id];
  return (c.address() != NULL) && (c.size() != 0) && (c.owner() != NULL);
}

// 检查chunkid的有效性
bool MemoryAllocator::IsValidChunkId(int chunk_id) {
  return (0 <= chunk_id) && (chunk_id < max_nof_chunks_);
}

// 检查给定page的地址是否在chunk管理的内存中
bool MemoryAllocator::IsPageInSpace(Page* p, PagedSpace* space) {
  ASSERT(p->is_valid());

  int chunk_id = GetChunkId(p);
  if (!IsValidChunkId(chunk_id)) return false;

  ChunkInfo& c = chunks_[chunk_id];
  return (c.address() <= p->address()) &&
         (p->address() < c.address() + c.size()) &&
         (space == c.owner());
}

// 获取下一页的地址
Page* MemoryAllocator::GetNextPage(Page* p) {
  ASSERT(p->is_valid());
  // 取出在opaque_header中的有效地址，取高位的值
  int raw_addr = p->opaque_header & ~Page::kPageAlignmentMask; // 2 ^ 13 - 1 
  return Page::FromAddress(AddressFrom<Address>(raw_addr));
}

// 取opaque_header的低n位，是ChunkId
int MemoryAllocator::GetChunkId(Page* p) {
  ASSERT(p->is_valid());
  return p->opaque_header & Page::kPageAlignmentMask;
}

// 在prev后插入next
void MemoryAllocator::SetNextPage(Page* prev, Page* next) {
  ASSERT(prev->is_valid());
  int chunk_id = prev->opaque_header & Page::kPageAlignmentMask;
  ASSERT_PAGE_ALIGNED(next->address());
  prev->opaque_header = OffsetFrom(next->address()) | chunk_id;
}

// 获取page所属的Space
PagedSpace* MemoryAllocator::PageOwner(Page* page) {
  int chunk_id = GetChunkId(page);
  ASSERT(IsValidChunk(chunk_id));
  return chunks_[chunk_id].owner();
}


// -----------------------------------------------------------------------------
// Space

bool PagedSpace::Contains(Address addr) {
  Page* p = Page::FromAddress(addr);
  ASSERT(p->is_valid());

  return MemoryAllocator::IsPageInSpace(p, this);
}


// -----------------------------------------------------------------------------
// LargeObjectChunk

HeapObject* LargeObjectChunk::GetObject() {
  // Round the chunk address up to the nearest page-aligned address
  // and return the heap object in that page.
  Page* page = Page::FromAddress(RoundUp(address(), Page::kPageSize));
  return HeapObject::FromAddress(page->ObjectAreaStart());
}


// -----------------------------------------------------------------------------
// LargeObjectSpace

int LargeObjectSpace::ExtraRSetBytesFor(int object_size) {
  int extra_rset_bits =
      RoundUp((object_size - Page::kObjectAreaSize) / kPointerSize,
              kBitsPerInt);
  return extra_rset_bits / kBitsPerByte;
}

// 分配内存
Object* NewSpace::AllocateRawInternal(int size_in_bytes,
                                      AllocationInfo* alloc_info) {
  
  Address new_top = alloc_info->top + size_in_bytes;
  // 内存不够了
  if (new_top > alloc_info->limit) {
    return Failure::RetryAfterGC(size_in_bytes, NEW_SPACE);
  }
  // 地址+低一位的标记，转成堆对象的地址表示，即低位置1
  Object* obj = HeapObject::FromAddress(alloc_info->top);
  // 更新指针，指向下一块可分配的内存
  alloc_info->top = new_top;
#ifdef DEBUG
  SemiSpace* space =
      (alloc_info == &allocation_info_) ? to_space_ : from_space_;
  ASSERT(space->low() <= alloc_info->top
         && alloc_info->top <= space->high()
         && alloc_info->limit == space->high());
#endif
  return obj;
}

} }  // namespace v8::internal

#endif  // V8_SPACES_INL_H_
