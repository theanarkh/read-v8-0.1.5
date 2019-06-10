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

#include "ic-inl.h"
#include "codegen-inl.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

#define __ masm->


static void ProbeTable(MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register name,
                       Register offset) {
  ExternalReference key_offset(SCTableReference::keyReference(table));
  ExternalReference value_offset(SCTableReference::valueReference(table));

  Label miss;

  // Save the offset on the stack.
  __ push(offset);

  // Check that the key in the entry matches the name.
  __ cmp(name, Operand::StaticArray(offset, times_2, key_offset));
  __ j(not_equal, &miss, not_taken);

  // Get the code entry from the cache.
  __ mov(offset, Operand::StaticArray(offset, times_2, value_offset));

  // Check that the flags match what we're looking for.
  __ mov(offset, FieldOperand(offset, Code::kFlagsOffset));
  __ and_(offset, ~Code::kFlagsTypeMask);
  __ cmp(offset, flags);
  __ j(not_equal, &miss);

  // Restore offset and re-load code entry from cache.
  __ pop(offset);
  __ mov(offset, Operand::StaticArray(offset, times_2, value_offset));

  // Jump to the first instruction in the code stub.
  __ add(Operand(offset), Immediate(Code::kHeaderSize - kHeapObjectTag));
  __ jmp(Operand(offset));

  // Miss: Restore offset and fall through.
  __ bind(&miss);
  __ pop(offset);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch) {
  Label miss;

  // Make sure that code is valid. The shifting code relies on the
  // entry size being 8.
  ASSERT(sizeof(Entry) == 8);

  // Make sure the flags does not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Make sure that there are no register conflicts.
  ASSERT(!scratch.is(receiver));
  ASSERT(!scratch.is(name));

  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Get the map of the receiver and compute the hash.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ add(scratch, FieldOperand(name, String::kLengthOffset));
  __ xor_(scratch, flags);
  __ and_(scratch, (kPrimaryTableSize - 1) << kHeapObjectTagSize);

  // Probe the primary table.
  ProbeTable(masm, flags, kPrimary, name, scratch);

  // Primary miss: Compute hash for secondary probe.
  __ sub(scratch, Operand(name));
  __ add(Operand(scratch), Immediate(flags));
  __ and_(scratch, (kSecondaryTableSize - 1) << kHeapObjectTagSize);

  // Probe the secondary table.
  ProbeTable(masm, flags, kSecondary, name, scratch);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ bind(&miss);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  // Load the global or builtins object from the current context.
  __ mov(prototype, Operand(esi, Context::SlotOffset(Context::GLOBAL_INDEX)));
  // Load the global context from the global or builtins object.
  __ mov(prototype,
         FieldOperand(prototype, GlobalObject::kGlobalContextOffset));
  // Load the function from the global context.
  __ mov(prototype, Operand(prototype, Context::SlotOffset(index)));
  // Load the initial map.  The global functions all have initial maps.
  __ mov(prototype,
         FieldOperand(prototype, JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ mov(prototype, FieldOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the object is a JS array.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch, FieldOperand(scratch, Map::kInstanceTypeOffset));
  __ cmp(scratch, JS_ARRAY_TYPE);
  __ j(not_equal, miss_label, not_taken);

  // Load length directly from the JS array.
  __ mov(eax, FieldOperand(receiver, JSArray::kLengthOffset));
  __ ret(0);
}


void StubCompiler::GenerateLoadShortStringLength(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch,
                                                 Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the object is a short string.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch, FieldOperand(scratch, Map::kInstanceTypeOffset));
  __ and_(scratch, kIsNotStringMask | kStringSizeMask);
  __ cmp(scratch, kStringTag | kShortStringTag);
  __ j(not_equal, miss_label, not_taken);

  // Load length directly from the string.
  __ mov(eax, FieldOperand(receiver, String::kLengthOffset));
  __ shr(eax, String::kShortLengthShift);
  __ shl(eax, kSmiTagSize);
  __ ret(0);
}

void StubCompiler::GenerateLoadMediumStringLength(MacroAssembler* masm,
                                                  Register receiver,
                                                  Register scratch,
                                                  Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the object is a short string.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch, FieldOperand(scratch, Map::kInstanceTypeOffset));
  __ and_(scratch, kIsNotStringMask | kStringSizeMask);
  __ cmp(scratch, kStringTag | kMediumStringTag);
  __ j(not_equal, miss_label, not_taken);

  // Load length directly from the string.
  __ mov(eax, FieldOperand(receiver, String::kLengthOffset));
  __ shr(eax, String::kMediumLengthShift);
  __ shl(eax, kSmiTagSize);
  __ ret(0);
}


void StubCompiler::GenerateLoadLongStringLength(MacroAssembler* masm,
                                                Register receiver,
                                                Register scratch,
                                                Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the object is a short string.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch, FieldOperand(scratch, Map::kInstanceTypeOffset));
  __ and_(scratch, kIsNotStringMask | kStringSizeMask);
  __ cmp(scratch, kStringTag | kLongStringTag);
  __ j(not_equal, miss_label, not_taken);

  // Load length directly from the string.
  __ mov(eax, FieldOperand(receiver, String::kLengthOffset));
  __ shr(eax, String::kLongLengthShift);
  __ shl(eax, kSmiTagSize);
  __ ret(0);
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the receiver is a function.
  __ mov(scratch1, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch2, FieldOperand(scratch1, Map::kInstanceTypeOffset));
  __ cmp(scratch2, JS_FUNCTION_TYPE);
  __ j(not_equal, miss_label, not_taken);

  // Make sure that the function has an instance prototype.
  Label non_instance;
  __ movzx_b(scratch2, FieldOperand(scratch1, Map::kBitFieldOffset));
  __ test(scratch2, Immediate(1 << Map::kHasNonInstancePrototype));
  __ j(not_zero, &non_instance, not_taken);

  // Get the prototype or initial map from the function.
  __ mov(scratch1,
         FieldOperand(receiver, JSFunction::kPrototypeOrInitialMapOffset));

  // If the prototype or initial map is the hole, don't return it and
  // simply miss the cache instead. This will allow us to allocate a
  // prototype object on-demand in the runtime system.
  __ cmp(Operand(scratch1), Immediate(Factory::the_hole_value()));
  __ j(equal, miss_label, not_taken);
  __ mov(eax, Operand(scratch1));

  // If the function does not have an initial map, we're done.
  Label done;
  __ mov(scratch1, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(scratch2, FieldOperand(scratch1, Map::kInstanceTypeOffset));
  __ cmp(scratch2, MAP_TYPE);
  __ j(not_equal, &done);

  // Get the prototype from the initial map.
  __ mov(eax, FieldOperand(eax, Map::kPrototypeOffset));

  // All done: Return the prototype.
  __ bind(&done);
  __ ret(0);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  __ bind(&non_instance);
  __ mov(eax, FieldOperand(scratch1, Map::kConstructorOffset));
  __ ret(0);
}


void StubCompiler::GenerateLoadField(MacroAssembler* masm,
                                     JSObject* object,
                                     JSObject* holder,
                                     Register receiver,
                                     Register scratch1,
                                     Register scratch2,
                                     int index,
                                     Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the maps haven't changed.
  Register reg =
      __ CheckMaps(object, receiver, holder, scratch1, scratch2, miss_label);

  // Get the properties array of the holder.
  __ mov(scratch1, FieldOperand(reg, JSObject::kPropertiesOffset));

  // Return the value from the properties array.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ mov(eax, FieldOperand(scratch1, offset));
  __ ret(0);
}


void StubCompiler::GenerateLoadCallback(MacroAssembler* masm,
                                        JSObject* object,
                                        JSObject* holder,
                                        Register receiver,
                                        Register name,
                                        Register scratch1,
                                        Register scratch2,
                                        AccessorInfo* callback,
                                        Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the maps haven't changed.
  Register reg =
      __ CheckMaps(object, receiver, holder, scratch1, scratch2, miss_label);

  // Push the arguments on the JS stack of the caller.
  __ pop(scratch2);  // remove return address
  __ push(receiver);  // receiver
  __ push(Immediate(Handle<AccessorInfo>(callback)));  // callback data
  __ push(name);  // name
  __ push(reg);  // holder
  __ push(scratch2);  // restore return address

  // Do tail-call to the C builtin.
  __ mov(eax, 3);  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kLoadCallbackProperty)));
}


void StubCompiler::GenerateLoadConstant(MacroAssembler* masm,
                                        JSObject* object,
                                        JSObject* holder,
                                        Register receiver,
                                        Register scratch1,
                                        Register scratch2,
                                        Object* value,
                                        Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the maps haven't changed.
  Register reg =
      __ CheckMaps(object, receiver, holder, scratch1, scratch2, miss_label);

  // Return the constant value.
  __ mov(eax, Handle<Object>(value));
  __ ret(0);
}


void StubCompiler::GenerateLoadInterceptor(MacroAssembler* masm,
                                           JSObject* object,
                                           JSObject* holder,
                                           Register receiver,
                                           Register name,
                                           Register scratch1,
                                           Register scratch2,
                                           Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the maps haven't changed.
  Register reg =
      __ CheckMaps(object, receiver, holder, scratch1, scratch2, miss_label);

  // Push the arguments on the JS stack of the caller.
  __ pop(scratch2);  // remove return address
  __ push(receiver);  // receiver
  __ push(reg);  // holder
  __ push(name);  // name
  __ push(scratch2);  // restore return address

  // Do tail-call to the C builtin.
  __ mov(eax, 2);  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kLoadInterceptorProperty)));
}


void StubCompiler::GenerateLoadMiss(MacroAssembler* masm, Code::Kind kind) {
  ASSERT(kind == Code::LOAD_IC || kind == Code::KEYED_LOAD_IC);
  Code* code = NULL;
  if (kind == Code::LOAD_IC) {
    code = Builtins::builtin(Builtins::LoadIC_Miss);
  } else {
    code = Builtins::builtin(Builtins::KeyedLoadIC_Miss);
  }

  Handle<Code> ic(code);
  __ jmp(ic, code_target);
}


void StubCompiler::GenerateStoreField(MacroAssembler* masm,
                                      JSObject* object,
                                      int index,
                                      Map* transition,
                                      Register receiver_reg,
                                      Register name_reg,
                                      Register scratch,
                                      Label* miss_label) {
  // Check that the object isn't a smi.
  __ test(receiver_reg, Immediate(kSmiTagMask));
  __ j(zero, miss_label, not_taken);

  // Check that the map of the object hasn't changed.
  __ cmp(FieldOperand(receiver_reg, HeapObject::kMapOffset),
         Immediate(Handle<Map>(object->map())));
  __ j(not_equal, miss_label, not_taken);

  // Perform global security token check if needed.
  if (object->IsJSGlobalObject()) {
    __ CheckAccessGlobal(receiver_reg, scratch, miss_label);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());

  // Get the properties array (optimistically).
  __ mov(scratch, FieldOperand(receiver_reg, JSObject::kPropertiesOffset));

  // Perform map transition for the receiver if necessary.
  if (transition != NULL) {
    // Update the map of the object; no write barrier updating is
    // needed because the map is never in new space.
    __ mov(FieldOperand(receiver_reg, HeapObject::kMapOffset),
           Immediate(Handle<Map>(transition)));
  }

  // Write to the properties array.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ mov(FieldOperand(scratch, offset), eax);

  // Update the write barrier for the array address.
  // Pass the value being stored in the now unused name_reg.
  __ mov(name_reg, Operand(eax));
  __ RecordWrite(scratch, offset, name_reg, receiver_reg);

  // Return the value (register eax).
  __ ret(0);
}


#undef __

#define __ masm()->


// TODO(1241006): Avoid having lazy compile stubs specialized by the
// number of arguments. It is not needed anymore.
Object* StubCompiler::CompileLazyCompile(Code::Flags flags) {
  HandleScope scope;

  // Enter an internal frame.
  __ EnterFrame(StackFrame::INTERNAL);

  // Push a copy of the function onto the stack.
  __ push(edi);

  __ push(edi);  // function is also the parameter to the runtime call
  __ CallRuntime(Runtime::kLazyCompile, 1);
  __ pop(edi);

  __ ExitFrame(StackFrame::INTERNAL);

  // Do a tail-call of the compiled function.
  __ lea(ecx, FieldOperand(eax, Code::kHeaderSize));
  __ jmp(Operand(ecx));

  return GetCodeWithFlags(flags);
}


Object* CallStubCompiler::CompileCallField(Object* object,
                                           JSObject* holder,
                                           int index) {
  // ----------- S t a t e -------------
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the receiver from the stack.
  const int argc = arguments().immediate();
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  // Check that the receiver isn't a smi.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Do the right check and compute the holder register.
  Register reg =
      __ CheckMaps(JSObject::cast(object), edx, holder, ebx, ecx, &miss);

  // Get the properties array of the holder and get the function from the field.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ mov(edi, FieldOperand(reg, JSObject::kPropertiesOffset));
  __ mov(edi, FieldOperand(edi, offset));

  // Check that the function really is a function.
  __ test(edi, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);
  __ mov(ebx, FieldOperand(edi, HeapObject::kMapOffset));  // get the map
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  __ cmp(ebx, JS_FUNCTION_TYPE);
  __ j(not_equal, &miss, not_taken);

  // Invoke the function.
  __ InvokeFunction(edi, arguments(), JUMP_FUNCTION);

  // Handle call cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(arguments().immediate());
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(FIELD);
}


Object* CallStubCompiler::CompileCallConstant(Object* object,
                                              JSObject* holder,
                                              JSFunction* function,
                                              CheckType check) {
  // ----------- S t a t e -------------
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the receiver from the stack.
  const int argc = arguments().immediate();
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ test(edx, Immediate(kSmiTagMask));
    __ j(zero, &miss, not_taken);
  }

  switch (check) {
    case RECEIVER_MAP_CHECK:
      // Check that the maps haven't changed.
      __ CheckMaps(JSObject::cast(object), edx, holder, ebx, ecx, &miss);
      break;

    case STRING_CHECK:
      // Check that the object is a two-byte string or a symbol.
      __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
      __ cmp(ecx, FIRST_NONSTRING_TYPE);
      __ j(above_equal, &miss, not_taken);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::STRING_FUNCTION_INDEX,
                                          ecx);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   ecx, holder, ebx, edx, &miss);
      break;

    case NUMBER_CHECK: {
      Label fast;
      // Check that the object is a smi or a heap number.
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, &fast, taken);
      __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
      __ cmp(ecx, HEAP_NUMBER_TYPE);
      __ j(not_equal, &miss, not_taken);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::NUMBER_FUNCTION_INDEX,
                                          ecx);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   ecx, holder, ebx, edx, &miss);
      break;
    }

    case BOOLEAN_CHECK: {
      Label fast;
      // Check that the object is a boolean.
      __ cmp(edx, Factory::true_value());
      __ j(equal, &fast, taken);
      __ cmp(edx, Factory::false_value());
      __ j(not_equal, &miss, not_taken);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::BOOLEAN_FUNCTION_INDEX,
                                          ecx);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   ecx, holder, ebx, edx, &miss);
      break;
    }

    case JSARRAY_HAS_FAST_ELEMENTS_CHECK:
      __ CheckMaps(JSObject::cast(object), edx, holder, ebx, ecx, &miss);
      // Make sure object->elements()->map() != Heap::dictionary_array_map()
      // Get the elements array of the object.
      __ mov(ebx, FieldOperand(edx, JSObject::kElementsOffset));
      // Check that the object is in fast mode (not dictionary).
      __ cmp(FieldOperand(ebx, HeapObject::kMapOffset),
             Immediate(Factory::hash_table_map()));
      __ j(equal, &miss, not_taken);
      break;

    default:
      UNREACHABLE();
  }

  // Get the function and setup the context.
  __ mov(Operand(edi), Immediate(Handle<JSFunction>(function)));
  __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));

  // Jump to the cached code (tail call).
  Handle<Code> code(function->code());
  ParameterCount expected(function->shared()->formal_parameter_count());
  __ InvokeCode(code, expected, arguments(), code_target, JUMP_FUNCTION);

  // Handle call cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(arguments().immediate());
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(CONSTANT_FUNCTION);
}


Object* CallStubCompiler::CompileCallInterceptor(Object* object,
                                                 JSObject* holder,
                                                 String* name) {
  // ----------- S t a t e -------------
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the number of arguments.
  const int argc = arguments().immediate();

  // Get the receiver from the stack.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));
  // Check that the receiver isn't a smi.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Check that maps have not changed and compute the holder register.
  Register reg =
      __ CheckMaps(JSObject::cast(object), edx, holder, ebx, ecx, &miss);

  // Enter an internal frame.
  __ EnterFrame(StackFrame::INTERNAL);

  // Push arguments on the expression stack.
  __ push(edx);  // receiver
  __ push(reg);  // holder
  __ push(Operand(ebp, (argc + 3) * kPointerSize));  // name

  // Perform call.
  __ mov(Operand(eax), Immediate(2));  // 2 arguments w/o receiver
  ExternalReference load_interceptor =
      ExternalReference(IC_Utility(IC::kLoadInterceptorProperty));
  __ mov(Operand(ebx), Immediate(load_interceptor));

  CEntryStub stub;
  __ CallStub(&stub);

  // Move result to edi and restore receiver.
  __ mov(Operand(edi), eax);
  __ mov(edx, Operand(ebp, (argc + 2) * kPointerSize));  // receiver

  // Exit frame.
  __ ExitFrame(StackFrame::INTERNAL);

  // Check that the function really is a function.
  __ test(edi, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);
  __ mov(ebx, FieldOperand(edi, HeapObject::kMapOffset));
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  __ cmp(ebx, JS_FUNCTION_TYPE);
  __ j(not_equal, &miss, not_taken);

  // Invoke the function.
  __ InvokeFunction(edi, arguments(), JUMP_FUNCTION);

  // Handle load cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(argc);
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


Object* StoreStubCompiler::CompileStoreField(JSObject* object,
                                             int index,
                                             Map* transition,
                                             String* name) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the object from the stack.
  __ mov(ebx, Operand(esp, 1 * kPointerSize));

  // Generate store field code.  Trashes the name register.
  GenerateStoreField(masm(), object, index, transition, ebx, ecx, edx, &miss);

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(Operand(ecx), Immediate(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(transition == NULL ? FIELD : MAP_TRANSITION);
}


Object* StoreStubCompiler::CompileStoreCallback(JSObject* object,
                                                AccessorInfo* callback,
                                                String* name) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the object from the stack.
  __ mov(ebx, Operand(esp, 1 * kPointerSize));

  // Check that the object isn't a smi.
  __ test(ebx, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Check that the map of the object hasn't changed.
  __ cmp(FieldOperand(ebx, HeapObject::kMapOffset),
         Immediate(Handle<Map>(object->map())));
  __ j(not_equal, &miss, not_taken);

  // Perform global security token check if needed.
  if (object->IsJSGlobalObject()) {
    __ CheckAccessGlobal(ebx, edx, &miss);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());

  __ pop(ebx);  // remove the return address
  __ push(Operand(esp, 0));  // receiver
  __ push(Immediate(Handle<AccessorInfo>(callback)));  // callback info
  __ push(ecx);  // name
  __ push(eax);  // value
  __ push(ebx);  // restore return address

  // Do tail-call to the C builtin.
  __ mov(eax, 3);  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kStoreCallbackProperty)));

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(Operand(ecx), Immediate(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* StoreStubCompiler::CompileStoreInterceptor(JSObject* receiver,
                                                   String* name) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the object from the stack.
  __ mov(ebx, Operand(esp, 1 * kPointerSize));

  // Check that the object isn't a smi.
  __ test(ebx, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Check that the map of the object hasn't changed.
  __ cmp(FieldOperand(ebx, HeapObject::kMapOffset),
         Immediate(Handle<Map>(receiver->map())));
  __ j(not_equal, &miss, not_taken);

  // Perform global security token check if needed.
  if (receiver->IsJSGlobalObject()) {
    __ CheckAccessGlobal(ebx, edx, &miss);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(receiver->IsJSGlobalObject() || !receiver->IsAccessCheckNeeded());

  __ pop(ebx);  // remove the return address
  __ push(Operand(esp, 0));  // receiver
  __ push(ecx);  // name
  __ push(eax);  // value
  __ push(ebx);  // restore return address

  // Do tail-call to the C builtin.
  __ mov(eax, 2);  // not counting receiver
  ExternalReference store_interceptor =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty));
  __ JumpToBuiltin(store_interceptor);

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(Operand(ecx), Immediate(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


Object* KeyedStoreStubCompiler::CompileStoreField(JSObject* object,
                                                  int index,
                                                  Map* transition,
                                                  String* name) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- esp[0] : return address
  //  -- esp[4] : key
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ IncrementCounter(&Counters::keyed_store_field, 1);

  // Get the name from the stack.
  __ mov(ecx, Operand(esp, 1 * kPointerSize));
  // Check that the name has not changed.
  __ cmp(Operand(ecx), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  // Get the object from the stack.
  __ mov(ebx, Operand(esp, 2 * kPointerSize));

  // Generate store field code.  Trashes the name register.
  GenerateStoreField(masm(), object, index, transition, ebx, ecx, edx, &miss);

  // Handle store cache miss.
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_store_field, 1);
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Miss));
  __ jmp(ic, code_target);

  // Return the generated code.
  return GetCode(transition == NULL ? FIELD : MAP_TRANSITION);
}


Object* LoadStubCompiler::CompileLoadField(JSObject* object,
                                           JSObject* holder,
                                           int index) {
  // ----------- S t a t e -------------
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  GenerateLoadField(masm(), object, holder, eax, ebx, edx, index, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(FIELD);
}


Object* LoadStubCompiler::CompileLoadCallback(JSObject* object,
                                              JSObject* holder,
                                              AccessorInfo* callback) {
  // ----------- S t a t e -------------
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  GenerateLoadCallback(masm(), object, holder, eax, ecx, ebx,
                       edx, callback, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* LoadStubCompiler::CompileLoadConstant(JSObject* object,
                                              JSObject* holder,
                                              Object* value) {
  // ----------- S t a t e -------------
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  GenerateLoadConstant(masm(), object, holder, eax, ebx, edx, value, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(CONSTANT_FUNCTION);
}


Object* LoadStubCompiler::CompileLoadInterceptor(JSObject* receiver,
                                                 JSObject* holder,
                                                 String* name) {
  // ----------- S t a t e -------------
  //  -- ecx    : name
  //  -- esp[0] : return address
  //  -- esp[4] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  GenerateLoadInterceptor(masm(), receiver, holder, eax, ecx, edx, ebx, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


Object* KeyedLoadStubCompiler::CompileLoadField(String* name,
                                                JSObject* receiver,
                                                JSObject* holder,
                                                int index) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_field, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadField(masm(), receiver, holder, ecx, ebx, edx, index, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_field, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(FIELD);
}


Object* KeyedLoadStubCompiler::CompileLoadCallback(String* name,
                                                   JSObject* receiver,
                                                   JSObject* holder,
                                                   AccessorInfo* callback) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_callback, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadCallback(masm(), receiver, holder, ecx, eax, ebx, edx,
                       callback, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_callback, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* KeyedLoadStubCompiler::CompileLoadConstant(String* name,
                                                   JSObject* receiver,
                                                   JSObject* holder,
                                                   Object* value) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_constant_function, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadConstant(masm(), receiver, holder, ecx, ebx, edx, value, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_constant_function, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CONSTANT_FUNCTION);
}


Object* KeyedLoadStubCompiler::CompileLoadInterceptor(JSObject* receiver,
                                                      JSObject* holder,
                                                      String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_interceptor, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadInterceptor(masm(), receiver, holder, ecx, eax, edx, ebx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_interceptor, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}




Object* KeyedLoadStubCompiler::CompileLoadArrayLength(String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_array_length, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadArrayLength(masm(), ecx, edx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_array_length, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* KeyedLoadStubCompiler::CompileLoadShortStringLength(String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_string_length, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadShortStringLength(masm(), ecx, edx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_string_length, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* KeyedLoadStubCompiler::CompileLoadMediumStringLength(String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_string_length, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadMediumStringLength(masm(), ecx, edx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_string_length, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* KeyedLoadStubCompiler::CompileLoadLongStringLength(String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_string_length, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadLongStringLength(masm(), ecx, edx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_string_length, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* KeyedLoadStubCompiler::CompileLoadFunctionPrototype(String* name) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : name
  //  -- esp[8] : receiver
  // -----------------------------------
  HandleScope scope;
  Label miss;

  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, (Operand(esp, 2 * kPointerSize)));
  __ IncrementCounter(&Counters::keyed_load_function_prototype, 1);

  // Check that the name has not changed.
  __ cmp(Operand(eax), Immediate(Handle<String>(name)));
  __ j(not_equal, &miss, not_taken);

  GenerateLoadFunctionPrototype(masm(), ecx, edx, ebx, &miss);
  __ bind(&miss);
  __ DecrementCounter(&Counters::keyed_load_function_prototype, 1);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


#undef __

} }  // namespace v8::internal
