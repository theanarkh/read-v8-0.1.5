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

#include "bootstrapper.h"
#include "debug.h"
#include "serialize.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

DEFINE_bool(preemption, false,
            "activate a 100ms timer that switches between V8 threads");

bool V8::has_been_setup_ = false;
bool V8::has_been_disposed_ = false;

bool V8::Initialize(Deserializer *des) {
  bool create_heap_objects = des == NULL;
  if (HasBeenDisposed()) return false;
  if (HasBeenSetup()) return true;
  has_been_setup_ = true;
#ifdef DEBUG
  // The initialization process does not handle memory exhaustion.
  DisallowAllocationFailure disallow_allocation_failure;
#endif

  // Enable logging before setting up the heap
  Logger::Setup();
  if (des) des->GetLog();

  // Setup the CPU support.
  CPU::Setup();

  // Setup the platform OS support.
  OS::Setup();

  // Setup the object heap
  ASSERT(!Heap::HasBeenSetup());
  if (!Heap::Setup(create_heap_objects)) {
    has_been_setup_ = false;
    return false;
  }

  // Initialize other runtime facilities
  Bootstrapper::Initialize(create_heap_objects);
  Builtins::Setup(create_heap_objects);
  Top::Initialize();

  if (FLAG_preemption) {
    v8::Locker locker;
    v8::Locker::StartPreemption(100);
  }

  Debug::Setup(create_heap_objects);
  StubCache::Initialize(create_heap_objects);

  // If we are deserializing, read the state into the now-empty heap.
  if (des != NULL) {
    des->Deserialize();
    StubCache::Clear();
  }

  return true;
}


void V8::TearDown() {
  if (HasBeenDisposed()) return;
  if (!HasBeenSetup()) return;

  if (FLAG_preemption) {
    v8::Locker locker;
    v8::Locker::StopPreemption();
  }

  Builtins::TearDown();
  Bootstrapper::TearDown();

  Top::TearDown();

  Heap::TearDown();
  Logger::TearDown();

  has_been_setup_ = false;
  has_been_disposed_ = true;
}

} }  // namespace v8::internal
