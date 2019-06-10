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

#ifndef V8_ZONE_INL_H_
#define V8_ZONE_INL_H_

#include "zone.h"

namespace v8 { namespace internal {


inline void* Zone::New(int size) {
  ASSERT(AssertNoZoneAllocation::allow_allocation());
  // Round up the requested size to fit the alignment.
  size = RoundUp(size, kAlignment);

  // Check if the requested size is available without expanding.
  // 当前的指针位置
  Address result = position_;
  /*
    一开始position和limit都是0，所以会分配一个segment，后续还需要分配的时候，如果segment
    里的内存还可以满足，则不需要再分配一个新的segment，在原来的分配就行，
    分配size后超过了限制，扩容的时候也是分配一个segment
  */
  if ((position_ += size) > limit_) result = NewExpand(size);

  // Check that the result has the proper alignment and return it.
  ASSERT(IsAddressAligned(result, kAlignment, 0));
  return reinterpret_cast<void*>(result);
}


} }  // namespace v8::internal

#endif  // V8_ZONE_INL_H_
