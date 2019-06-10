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
#include "code-stubs.h"
#include "factory.h"
#include "macro-assembler.h"

namespace v8 { namespace internal {

#ifdef DEBUG
DEFINE_bool(print_code_stubs, false, "print code stubs");
#endif

Handle<Code> CodeStub::GetCode() {
  uint32_t key = GetKey();
  int index = Heap::code_stubs()->FindNumberEntry(key);
  if (index == -1) {
    HandleScope scope;

    // Update the static counter each time a new code stub is generated.
    Counters::code_stubs.Increment();

    // Generate the new code.
    MacroAssembler masm(NULL, 256);

    bool needs_check_for_stub_calls = !AllowsStubCalls();
    if (needs_check_for_stub_calls) {
      // Nested stubs are not allowed for leafs.
      ASSERT(!masm.generating_stub());
      masm.set_generating_stub(true);
    }

    // Generate the code for the stub.
    Generate(&masm);

    if (needs_check_for_stub_calls) masm.set_generating_stub(false);

    // Create the code object.
    CodeDesc desc;
    masm.GetCode(&desc);

    // Copy the generated code into a heap object.
    // TODO(1238541): Simplify this somewhat complicated encoding.
    CodeStub::Major major = MajorKey();
    // Lower three bits in state field.
    InlineCacheState state = static_cast<InlineCacheState>(major & 0x07);
    // Upper two bits in type field.
    PropertyType type = static_cast<PropertyType>((major >> 3) & 0x03);
    // Compute flags with state and type used to hold majr key.
    Code::Flags flags = Code::ComputeFlags(Code::STUB, state, type);

    Handle<Code> code = Factory::NewCode(desc, NULL, flags);

    // Add unresolved entries in the code to the fixup list.
    Bootstrapper::AddFixup(*code, &masm);

    LOG(CodeCreateEvent(GetName(), *code, ""));
    Counters::total_stubs_code_size.Increment(code->instruction_size());

#ifdef DEBUG
    if (FLAG_print_code_stubs) {
      Print();
      code->Print();
      PrintF("\n");
    }
#endif

    // Update the dictionary and the root in Heap.
    Handle<Dictionary> dict =
        Factory::DictionaryAtNumberPut(Handle<Dictionary>(Heap::code_stubs()),
                                       key,
                                       code);
    Heap::set_code_stubs(*dict);
    index = Heap::code_stubs()->FindNumberEntry(key);
  }
  ASSERT(index != -1);

  return Handle<Code>(Code::cast(Heap::code_stubs()->ValueAt(index)));
}


const char* CodeStub::MajorName(CodeStub::Major major_key) {
  switch (major_key) {
    case CallFunction:
      return "CallFunction";
    case InlinedGenericOp:
      return "InlinedGenericOp";
    case SmiOp:
      return "SmiOp";
    case Compare:
      return "Compare";
    case RecordWrite:
      return "RecordWrite";
    case GenericOp:
      return "GenericOp";
    case StackCheck:
      return "StackCheck";
    case UnarySub:
      return "UnarySub";
    case RevertToNumber:
      return "RevertToNumber";
    case CounterOp:
      return "CounterOp";
    case ArgumentsAccess:
      return "ArgumentsAccess";
    case Runtime:
      return "Runtime";
    case CEntry:
      return "CEntry";
    case JSEntry:
      return "JSEntry";
    case GetProperty:
      return "GetProperty";
    case SetProperty:
      return "SetProperty";
    case InvokeBuiltin:
      return "InvokeBuiltin";
    case JSExit:
      return "JSExit";
    default:
      UNREACHABLE();
      return NULL;
  }
}


} }  // namespace v8::internal
