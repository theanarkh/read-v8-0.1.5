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

#include "accessors.h"
#include "api.h"
#include "arguments.h"
#include "execution.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

#ifdef DEBUG
DEFINE_bool(trace_ic, false, "trace inline cache state transitions");
#endif
DEFINE_bool(use_ic, true, "use inline caching");
DECLARE_bool(strict);


#ifdef DEBUG
static char TransitionMarkFromState(IC::State state) {
  switch (state) {
    case UNINITIALIZED: return '0';
    case PREMONOMORPHIC: return '0';
    case MONOMORPHIC: return '1';
    case MONOMORPHIC_PROTOTYPE_FAILURE: return '^';
    case MEGAMORPHIC: return 'N';

    // We never see the debugger states here, because the state is
    // computed from the original code - not the patched code. Let
    // these cases fall through to the unreachable code below.
    case DEBUG_BREAK: break;
    case DEBUG_PREPARE_STEP_IN: break;
  }
  UNREACHABLE();
  return 0;
}

void IC::TraceIC(const char* type,
                 Handle<String> name,
                 State old_state,
                 Code* new_target) {
  if (FLAG_trace_ic) {
    State new_state = StateFrom(new_target, Heap::undefined_value());
    PrintF("[%s (%c->%c) ", type,
           TransitionMarkFromState(old_state),
           TransitionMarkFromState(new_state));
    name->Print();
    PrintF("]\n");
  }
}
#endif


IC::IC(FrameDepth depth) {
  // To improve the performance of the (much used) IC code, we unfold
  // a few levels of the stack frame iteration code. This yields a
  // ~35% speedup when running DeltaBlue with the '--nouse-ic' flag.
  const Address entry = Top::c_entry_fp(Top::GetCurrentThread());
  Address* pc_address =
      reinterpret_cast<Address*>(entry + ExitFrameConstants::kCallerPCOffset);
  Address fp = Memory::Address_at(entry + ExitFrameConstants::kCallerFPOffset);
  // If there's another JavaScript frame on the stack, we need to look
  // one frame further down the stack to find the frame pointer and
  // the return address stack slot.
  if (depth == EXTRA_CALL_FRAME) {
    const int kCallerPCOffset = StandardFrameConstants::kCallerPCOffset;
    pc_address = reinterpret_cast<Address*>(fp + kCallerPCOffset);
    fp = Memory::Address_at(fp + StandardFrameConstants::kCallerFPOffset);
  }
#ifdef DEBUG
  StackFrameIterator it;
  for (int i = 0; i < depth + 1; i++) it.Advance();
  StackFrame* frame = it.frame();
  ASSERT(fp == frame->fp() && pc_address == frame->pc_address());
#endif
  fp_ = fp;
  pc_address_ = pc_address;
}


Address IC::OriginalCodeAddress() {
  HandleScope scope;
  // Compute the JavaScript frame for the frame pointer of this IC
  // structure. We need this to be able to find the function
  // corresponding to the frame.
  StackFrameIterator it;
  while (it.frame()->fp() != this->fp()) it.Advance();
  JavaScriptFrame* frame = JavaScriptFrame::cast(it.frame());
  // Find the function on the stack and both the active code for the
  // function and the original code.
  JSFunction* function = JSFunction::cast(frame->function());
  Handle<SharedFunctionInfo> shared(function->shared());
  Code* code = shared->code();
  ASSERT(Debug::HasDebugInfo(shared));
  Code* original_code = Debug::GetDebugInfo(shared)->original_code();
  ASSERT(original_code->IsCode());
  // Get the address of the call site in the active code. This is the
  // place where the call to DebugBreakXXX is and where the IC
  // normally would be.
  Address addr = pc() - Assembler::kTargetAddrToReturnAddrDist;
  // Return the address in the original code. This is the place where
  // the call which has been overwriten by the DebugBreakXXX resides
  // and the place where the inline cache system should look.
  int delta = original_code->instruction_start() - code->instruction_start();
  return addr + delta;
}


IC::State IC::StateFrom(Code* target, Object* receiver) {
  IC::State state = target->state();

  if (state != MONOMORPHIC) return state;
  if (receiver->IsUndefined() || receiver->IsNull()) return state;

  Map* map = GetCodeCacheMapForObject(receiver);

  // Decide whether the inline cache failed because of changes to the
  // receiver itself or changes to one of its prototypes.
  //
  // If there are changes to the receiver itself, the map of the
  // receiver will have changed and the current target will not be in
  // the receiver map's code cache.  Therefore, if the current target
  // is in the receiver map's code cache, the inline cache failed due
  // to prototype check failure.
  if (map->IncludedInCodeCache(target)) {
    // For keyed load/store, the most likely cause of cache failure is
    // that the key has changed.  We do not distinguish between
    // prototype and non-prototype failures for keyed access.
    Code::Kind kind = target->kind();
    if (kind == Code::KEYED_LOAD_IC || kind == Code::KEYED_STORE_IC) {
      return MONOMORPHIC;
    }

    // Clear the code cache for this map to avoid hitting the same
    // invalid stub again.  It seems likely that most of the code in
    // the cache is invalid if one of the stubs is so we flush the
    // entire code cache.
    map->ClearCodeCache();

    return MONOMORPHIC_PROTOTYPE_FAILURE;
  }
  return MONOMORPHIC;
}


RelocMode IC::ComputeMode() {
  Address addr = address();
  Code* code = Code::cast(Heap::FindCodeObject(addr));
  for (RelocIterator it(code, RelocInfo::kCodeTargetMask);
       !it.done(); it.next()) {
    RelocInfo* info = it.rinfo();
    if (info->pc() == addr) return info->rmode();
  }
  UNREACHABLE();
  return no_reloc;
}


Failure* IC::TypeError(const char* type,
                       Handle<Object> object,
                       Handle<String> name) {
  HandleScope scope;
  Handle<Object> args[2] = { name, object };
  Handle<Object> error = Factory::NewTypeError(type, HandleVector(args, 2));
  return Top::Throw(*error);
}


Failure* IC::ReferenceError(const char* type, Handle<String> name) {
  HandleScope scope;
  Handle<Object> error =
      Factory::NewReferenceError(type, HandleVector(&name, 1));
  return Top::Throw(*error);
}


void IC::Clear(Address address) {
  Code* target = GetTargetAtAddress(address);

  // Don't clear debug break inline cache as it will remove the break point.
  if (target->state() == DEBUG_BREAK) return;

  switch (target->kind()) {
    case Code::LOAD_IC: return LoadIC::Clear(address, target);
    case Code::KEYED_LOAD_IC: return KeyedLoadIC::Clear(address, target);
    case Code::STORE_IC: return StoreIC::Clear(address, target);
    case Code::KEYED_STORE_IC: return KeyedStoreIC::Clear(address, target);
    case Code::CALL_IC: return CallIC::Clear(address, target);
    default: UNREACHABLE();
  }
}


void CallIC::Clear(Address address, Code* target) {
  if (target->state() == UNINITIALIZED) return;
  Code* code = StubCache::FindCallInitialize(target->arguments_count());
  SetTargetAtAddress(address, code);
}


void KeyedLoadIC::Clear(Address address, Code* target) {
  if (target->state() == UNINITIALIZED) return;
  SetTargetAtAddress(address, initialize_stub());
}


void LoadIC::Clear(Address address, Code* target) {
  if (target->state() == UNINITIALIZED) return;
  SetTargetAtAddress(address, initialize_stub());
}


void StoreIC::Clear(Address address, Code* target) {
  if (target->state() == UNINITIALIZED) return;
  SetTargetAtAddress(address, initialize_stub());
}


void KeyedStoreIC::Clear(Address address, Code* target) {
  if (target->state() == UNINITIALIZED) return;
  SetTargetAtAddress(address, initialize_stub());
}


Object* CallIC::LoadFunction(State state,
                             Handle<Object> object,
                             Handle<String> name) {
  // If the object is undefined or null it's illegal to try to get any
  // of its properties; throw a TypeError in that case.
  if (object->IsUndefined() || object->IsNull()) {
    return TypeError("non_object_property_call", object, name);
  }

  // Lookup the property in the object.
  LookupResult lookup;
  object->Lookup(*name, &lookup);

  Object* result = Heap::the_hole_value();

  if (!lookup.IsValid()) {
    // If the object does not have the requested property, check which
    // exception we need to throw.
    if (is_contextual()) {
      return ReferenceError("not_defined", name);
    }
    return TypeError("undefined_method", object, name);
  }

  // Lookup is valid: Update inline cache and stub cache.
  if (FLAG_use_ic && lookup.IsLoaded()) {
    UpdateCaches(&lookup, state, object, name);
  }

  if (lookup.type() == INTERCEPTOR) {
    // Get the property.
    PropertyAttributes attr;
    result = object->GetProperty(*name, &attr);
    if (result->IsFailure()) return result;
    // If the object does not have the requested property, check which
    // exception we need to throw.
    if (attr == ABSENT) {
      if (is_contextual()) {
        return ReferenceError("not_defined", name);
      }
      return TypeError("undefined_method", object, name);
    }
  } else {
    // Lookup is valid and no interceptors are involved. Get the
    // property.
    result = object->GetProperty(*name);
    if (result->IsFailure()) return result;
  }

  ASSERT(result != Heap::the_hole_value());

  if (result->IsJSFunction()) {
    // Check if there is an optimized (builtin) version of the function.
    // Ignored this will degrade performance for Array.prototype.{push,pop}.
    // Please note we only return the optimized function iff
    // the JSObject has FastElements.
    if (object->IsJSObject() && JSObject::cast(*object)->HasFastElements()) {
      Object* opt = Top::LookupSpecialFunction(JSObject::cast(*object),
                                               lookup.holder(),
                                               JSFunction::cast(result));
      if (opt->IsJSFunction()) return opt;
    }

    // If performing debug step into then flood this function with one-shot
    // break points if it is called from where step into was requested.
    if (Debug::StepInActive() && fp() == Debug::step_in_fp()) {
      // Don't allow step into functions in the native context.
      if (JSFunction::cast(result)->context()->global() !=
          Top::context()->builtins()) {
        HandleScope scope;
        Handle<SharedFunctionInfo> shared(JSFunction::cast(result)->shared());
        Debug::FloodWithOneShot(shared);
      }
    }
    return result;
  }

  // Try to find a suitable function delegate for the object at hand.
  HandleScope scope;
  Handle<Object> target(result);
  Handle<Object> delegate = Execution::GetFunctionDelegate(target);

  if (delegate->IsJSFunction()) {
    // Patch the receiver and use the delegate as the function to
    // invoke. This is used for invoking objects as if they were
    // functions.
    const int argc = this->target()->arguments_count();
    StackFrameLocator locator;
    JavaScriptFrame* frame = locator.FindJavaScriptFrame(0);
    int index = frame->ComputeExpressionsCount() - (argc + 1);
    frame->SetExpression(index, *target);
    return *delegate;
  } else {
    return TypeError("property_not_function", object, name);
  }
}


void CallIC::UpdateCaches(LookupResult* lookup,
                          State state,
                          Handle<Object> object,
                          Handle<String> name) {
  ASSERT(lookup->IsLoaded());
  // Bail out if we didn't find a result.
  if (!lookup->IsValid() || !lookup->IsCacheable()) return;

  // Compute the number of arguments.
  int argc = target()->arguments_count();
  Object* code = NULL;

  if (state == UNINITIALIZED) {
    // This is the first time we execute this inline cache.
    // Set the target to the pre monomorphic stub to delay
    // setting the monomorphic state.
    code = StubCache::ComputeCallPreMonomorphic(argc);
  } else if (state == MONOMORPHIC) {
    code = StubCache::ComputeCallMegamorphic(argc);
  } else {
    // Compute monomorphic stub.
    switch (lookup->type()) {
      case FIELD: {
        int index = lookup->GetFieldIndex();
        code = StubCache::ComputeCallField(argc, *name, *object,
                                           lookup->holder(), index);
        break;
      }
      case CONSTANT_FUNCTION: {
        // Get the constant function and compute the code stub for this
        // call; used for rewriting to monomorphic state and making sure
        // that the code stub is in the stub cache.
        JSFunction* function = lookup->GetConstantFunction();
        code = StubCache::ComputeCallConstant(argc, *name, *object,
                                              lookup->holder(), function);
        break;
      }
      case NORMAL: {
        // There is only one shared stub for calling normalized
        // properties. It does not traverse the prototype chain, so the
        // property must be found in the receiver for the stub to be
        // applicable.
        if (!object->IsJSObject()) return;
        Handle<JSObject> receiver = Handle<JSObject>::cast(object);
        if (lookup->holder() != *receiver) return;
        code = StubCache::ComputeCallNormal(argc, *name, *receiver);
        break;
      }
      case INTERCEPTOR: {
        code = StubCache::ComputeCallInterceptor(argc, *name, *object,
                                                 lookup->holder());
        break;
      }
      default:
        return;
    }
  }

  // If we're unable to compute the stub (not enough memory left), we
  // simply avoid updating the caches.
  if (code->IsFailure()) return;

  // Patch the call site depending on the state of the cache.
  if (state == UNINITIALIZED || state == PREMONOMORPHIC ||
      state == MONOMORPHIC || state == MONOMORPHIC_PROTOTYPE_FAILURE) {
    set_target(Code::cast(code));
  }

#ifdef DEBUG
  TraceIC("CallIC", name, state, target());
#endif
}


Object* LoadIC::Load(State state, Handle<Object> object, Handle<String> name) {
  // If the object is undefined or null it's illegal to try to get any
  // of its properties; throw a TypeError in that case.
  if (object->IsUndefined() || object->IsNull()) {
    return TypeError("non_object_property_load", object, name);
  }

  if (FLAG_use_ic) {
    // Use specialized code for getting the length of strings.
    if (object->IsString() && name->Equals(Heap::length_symbol())) {
#ifdef DEBUG
      if (FLAG_trace_ic) PrintF("[LoadIC : +#length /string]\n");
#endif
      Code* target = NULL;
      if (object->IsShortString()) {
        target = Builtins::builtin(Builtins::LoadIC_ShortStringLength);
      } else if (object->IsMediumString()) {
        target = Builtins::builtin(Builtins::LoadIC_MediumStringLength);
      } else {
        ASSERT(object->IsLongString());
        target  = Builtins::builtin(Builtins::LoadIC_LongStringLength);
      }
      set_target(target);
      StubCache::Set(*name, HeapObject::cast(*object)->map(), target);
      return Smi::FromInt(String::cast(*object)->length());
    }

    // Use specialized code for getting the length of arrays.
    if (object->IsJSArray() && name->Equals(Heap::length_symbol())) {
#ifdef DEBUG
      if (FLAG_trace_ic) PrintF("[LoadIC : +#length /array]\n");
#endif
      Code* target = Builtins::builtin(Builtins::LoadIC_ArrayLength);
      set_target(target);
      StubCache::Set(*name, HeapObject::cast(*object)->map(), target);
      return JSArray::cast(*object)->length();
    }

    // Use specialized code for getting prototype of functions.
    if (object->IsJSFunction() && name->Equals(Heap::prototype_symbol())) {
#ifdef DEBUG
      if (FLAG_trace_ic) PrintF("[LoadIC : +#prototype /function]\n");
#endif
      Code* target = Builtins::builtin(Builtins::LoadIC_FunctionPrototype);
      set_target(target);
      StubCache::Set(*name, HeapObject::cast(*object)->map(), target);
      return Accessors::FunctionGetPrototype(*object, 0);
    }
  }

  // Check if the name is trivially convertible to an index and get
  // the element if so.
  uint32_t index;
  if (name->AsArrayIndex(&index)) return object->GetElement(index);

  // Named lookup in the object.
  LookupResult lookup;
  object->Lookup(*name, &lookup);

  // If lookup is invalid, check if we need to throw an exception.
  if (!lookup.IsValid()) {
    if (FLAG_strict || is_contextual()) {
      return ReferenceError("not_defined", name);
    }
    String* class_name = object->IsJSObject()
                         ? Handle<JSObject>::cast(object)->class_name()
                         : Heap::empty_string();
    LOG(SuspectReadEvent(*name, class_name));
    USE(class_name);
  }

  // Update inline cache and stub cache.
  if (FLAG_use_ic && lookup.IsLoaded()) {
    UpdateCaches(&lookup, state, object, name);
  }

  PropertyAttributes attr;
  if (lookup.IsValid() && lookup.type() == INTERCEPTOR) {
    // Get the property.
    Object* result = object->GetProperty(*object, &lookup, *name, &attr);
    if (result->IsFailure()) return result;
    // If the property is not present, check if we need to throw an
    // exception.
    if (attr == ABSENT && is_contextual()) {
      return ReferenceError("not_defined", name);
    }
    return result;
  }

  // Get the property.
  return object->GetProperty(*object, &lookup, *name, &attr);
}


void LoadIC::UpdateCaches(LookupResult* lookup,
                          State state,
                          Handle<Object> object,
                          Handle<String> name) {
  ASSERT(lookup->IsLoaded());
  // Bail out if we didn't find a result.
  if (!lookup->IsValid() || !lookup->IsCacheable()) return;

  // Loading properties from values is not common, so don't try to
  // deal with non-JS objects here.
  if (!object->IsJSObject()) return;
  Handle<JSObject> receiver = Handle<JSObject>::cast(object);

  // Compute the code stub for this load.
  Object* code = NULL;
  if (state == UNINITIALIZED) {
    // This is the first time we execute this inline cache.
    // Set the target to the pre monomorphic stub to delay
    // setting the monomorphic state.
    code = pre_monomorphic_stub();
  } else {
    // Compute monomorphic stub.
    switch (lookup->type()) {
      case FIELD: {
        code = StubCache::ComputeLoadField(*name, *receiver,
                                           lookup->holder(),
                                           lookup->GetFieldIndex());
        break;
      }
      case CONSTANT_FUNCTION: {
        Object* constant = lookup->GetConstantFunction();
        code = StubCache::ComputeLoadConstant(*name, *receiver,
                                              lookup->holder(), constant);
        break;
      }
      case NORMAL: {
        // There is only one shared stub for loading normalized
        // properties. It does not traverse the prototype chain, so the
        // property must be found in the receiver for the stub to be
        // applicable.
        if (lookup->holder() != *receiver) return;
        code = StubCache::ComputeLoadNormal(*name, *receiver);
        break;
      }
      case CALLBACKS: {
        if (!lookup->GetCallbackObject()->IsAccessorInfo()) return;
        AccessorInfo* callback =
            AccessorInfo::cast(lookup->GetCallbackObject());
        if (v8::ToCData<Address>(callback->getter()) == 0) return;
        code = StubCache::ComputeLoadCallback(*name, *receiver,
                                              lookup->holder(), callback);
        break;
      }
      case INTERCEPTOR: {
        code = StubCache::ComputeLoadInterceptor(*name, *receiver,
                                                 lookup->holder());
        break;
      }
      default:
        return;
    }
  }

  // If we're unable to compute the stub (not enough memory left), we
  // simply avoid updating the caches.
  if (code->IsFailure()) return;

  // Patch the call site depending on the state of the cache.
  if (state == UNINITIALIZED || state == PREMONOMORPHIC ||
      state == MONOMORPHIC_PROTOTYPE_FAILURE) {
    set_target(Code::cast(code));
  } else if (state == MONOMORPHIC) {
    set_target(megamorphic_stub());
  }

#ifdef DEBUG
  TraceIC("LoadIC", name, state, target());
#endif
}


Object* KeyedLoadIC::Load(State state,
                          Handle<Object> object,
                          Handle<Object> key) {
  if (key->IsSymbol()) {
    Handle<String> name = Handle<String>::cast(key);

    // If the object is undefined or null it's illegal to try to get any
    // of its properties; throw a TypeError in that case.
    if (object->IsUndefined() || object->IsNull()) {
      return TypeError("non_object_property_load", object, name);
    }

    if (FLAG_use_ic) {
      // Use specialized code for getting the length of strings.
      if (object->IsString() && name->Equals(Heap::length_symbol())) {
        Handle<String> string = Handle<String>::cast(object);
        Object* code = NULL;
        if (string->IsShortString()) {
          code = StubCache::ComputeKeyedLoadShortStringLength(*name, *string);
        } else if (string->IsMediumString()) {
          code =
              StubCache::ComputeKeyedLoadMediumStringLength(*name, *string);
        } else {
          ASSERT(string->IsLongString());
          code = StubCache::ComputeKeyedLoadLongStringLength(*name, *string);
        }
        if (code->IsFailure()) return code;
        set_target(Code::cast(code));
#ifdef DEBUG
        TraceIC("KeyedLoadIC", name, state, target());
#endif
        return Smi::FromInt(string->length());
      }

      // Use specialized code for getting the length of arrays.
      if (object->IsJSArray() && name->Equals(Heap::length_symbol())) {
        Handle<JSArray> array = Handle<JSArray>::cast(object);
        Object* code = StubCache::ComputeKeyedLoadArrayLength(*name, *array);
        if (code->IsFailure()) return code;
        set_target(Code::cast(code));
#ifdef DEBUG
        TraceIC("KeyedLoadIC", name, state, target());
#endif
        return JSArray::cast(*object)->length();
      }

      // Use specialized code for getting prototype of functions.
      if (object->IsJSFunction() && name->Equals(Heap::prototype_symbol())) {
        Handle<JSFunction> function = Handle<JSFunction>::cast(object);
        Object* code =
            StubCache::ComputeKeyedLoadFunctionPrototype(*name, *function);
        if (code->IsFailure()) return code;
        set_target(Code::cast(code));
#ifdef DEBUG
        TraceIC("KeyedLoadIC", name, state, target());
#endif
        return Accessors::FunctionGetPrototype(*object, 0);
      }
    }

    // Check if the name is trivially convertible to an index and get
    // the element or char if so.
    uint32_t index = 0;
    if (name->AsArrayIndex(&index)) {
      HandleScope scope;
      // Rewrite to the generic keyed load stub.
      if (FLAG_use_ic) set_target(generic_stub());
      return Runtime::GetElementOrCharAt(object, index);
    }

    // Named lookup.
    LookupResult lookup;
    object->Lookup(*name, &lookup);

    // If lookup is invalid, check if we need to throw an exception.
    if (!lookup.IsValid()) {
      if (FLAG_strict || is_contextual()) {
        return ReferenceError("not_defined", name);
      }
    }

    // Update the inline cache.
    if (FLAG_use_ic && lookup.IsLoaded()) {
      UpdateCaches(&lookup, state, object, name);
    }

    PropertyAttributes attr;
    if (lookup.IsValid() && lookup.type() == INTERCEPTOR) {
      // Get the property.
      Object* result = object->GetProperty(*object, &lookup, *name, &attr);
      if (result->IsFailure()) return result;
      // If the property is not present, check if we need to throw an
      // exception.
      if (attr == ABSENT && is_contextual()) {
        return ReferenceError("not_defined", name);
      }
      return result;
    }

    return object->GetProperty(*object, &lookup, *name, &attr);
  }

  // Do not use ICs for objects that require access checks (including
  // the global object).
  bool use_ic = FLAG_use_ic && !object->IsAccessCheckNeeded();

  if (use_ic) set_target(generic_stub());

  // Get the property.
  return Runtime::GetObjectProperty(object, *key);
}


void KeyedLoadIC::UpdateCaches(LookupResult* lookup, State state,
                               Handle<Object> object, Handle<String> name) {
  ASSERT(lookup->IsLoaded());
  // Bail out if we didn't find a result.
  if (!lookup->IsValid() || !lookup->IsCacheable()) return;

  if (!object->IsJSObject()) return;
  Handle<JSObject> receiver = Handle<JSObject>::cast(object);

  // Compute the code stub for this load.
  Object* code = NULL;

  if (state == UNINITIALIZED) {
    // This is the first time we execute this inline cache.
    // Set the target to the pre monomorphic stub to delay
    // setting the monomorphic state.
    code = pre_monomorphic_stub();
  } else {
    // Compute a monomorphic stub.
    switch (lookup->type()) {
      case FIELD: {
        code = StubCache::ComputeKeyedLoadField(*name, *receiver,
                                                lookup->holder(),
                                                lookup->GetFieldIndex());
        break;
      }
      case CONSTANT_FUNCTION: {
        Object* constant = lookup->GetConstantFunction();
        code = StubCache::ComputeKeyedLoadConstant(*name, *receiver,
                                                   lookup->holder(), constant);
        break;
      }
      case CALLBACKS: {
        if (!lookup->GetCallbackObject()->IsAccessorInfo()) return;
        AccessorInfo* callback =
            AccessorInfo::cast(lookup->GetCallbackObject());
        if (v8::ToCData<Address>(callback->getter()) == 0) return;
        code = StubCache::ComputeKeyedLoadCallback(*name, *receiver,
                                                   lookup->holder(), callback);
        break;
      }
      case INTERCEPTOR: {
        code = StubCache::ComputeKeyedLoadInterceptor(*name, *receiver,
                                                      lookup->holder());
        break;
      }
      default: {
        // Always rewrite to the generic case so that we do not
        // repeatedly try to rewrite.
        code = generic_stub();
        break;
      }
    }
  }

  // If we're unable to compute the stub (not enough memory left), we
  // simply avoid updating the caches.
  if (code->IsFailure()) return;

  // Patch the call site depending on the state of the cache.  Make
  // sure to always rewrite from monomorphic to megamorphic.
  ASSERT(state != MONOMORPHIC_PROTOTYPE_FAILURE);
  if (state == UNINITIALIZED || state == PREMONOMORPHIC) {
    set_target(Code::cast(code));
  } else if (state == MONOMORPHIC) {
    set_target(megamorphic_stub());
  }

#ifdef DEBUG
  TraceIC("KeyedLoadIC", name, state, target());
#endif
}


Object* StoreIC::Store(State state,
                       Handle<Object> object,
                       Handle<String> name,
                       Handle<Object> value) {
  // If the object is undefined or null it's illegal to try to set any
  // properties on it; throw a TypeError in that case.
  if (object->IsUndefined() || object->IsNull()) {
    return TypeError("non_object_property_store", object, name);
  }

  // Ignore stores where the receiver is not a JSObject.
  if (!object->IsJSObject()) return *value;
  Handle<JSObject> receiver = Handle<JSObject>::cast(object);

  // Check if the given name is an array index.
  uint32_t index;
  if (name->AsArrayIndex(&index)) {
    SetElement(receiver, index, value);
    return *value;
  }

  // Lookup the property locally in the receiver.
  LookupResult lookup;
  receiver->LocalLookup(*name, &lookup);

  // Update inline cache and stub cache.
  if (FLAG_use_ic && lookup.IsLoaded()) {
    UpdateCaches(&lookup, state, receiver, name, value);
  }

  // Set the property.
  return receiver->SetProperty(*name, *value, NONE);
}


void StoreIC::UpdateCaches(LookupResult* lookup,
                           State state,
                           Handle<JSObject> receiver,
                           Handle<String> name,
                           Handle<Object> value) {
  ASSERT(lookup->IsLoaded());
  // Bail out if we didn't find a result.
  if (!lookup->IsValid() || !lookup->IsCacheable()) return;

  // If the property is read-only, we leave the IC in its current
  // state.
  if (lookup->IsReadOnly()) return;

  // If the property has a non-field type allowing map transitions
  // where there is extra room in the object, we leave the IC in its
  // current state.
  PropertyType type = lookup->type();

  // Compute the code stub for this store; used for rewriting to
  // monomorphic state and making sure that the code stub is in the
  // stub cache.
  Object* code = NULL;
  switch (type) {
    case FIELD: {
      code = StubCache::ComputeStoreField(*name, *receiver,
                                          lookup->GetFieldIndex());
      break;
    }
    case MAP_TRANSITION: {
      if (lookup->GetAttributes() != NONE) return;
      if (receiver->map()->unused_property_fields() == 0) return;
      HandleScope scope;
      ASSERT(type == MAP_TRANSITION &&
             (receiver->map()->unused_property_fields() > 0));
      Handle<Map> transition(lookup->GetTransitionMap());
      int index = transition->PropertyIndexFor(*name);
      code = StubCache::ComputeStoreField(*name, *receiver, index, *transition);
      break;
    }
    case CALLBACKS: {
      if (!lookup->GetCallbackObject()->IsAccessorInfo()) return;
      AccessorInfo* callback = AccessorInfo::cast(lookup->GetCallbackObject());
      if (v8::ToCData<Address>(callback->setter()) == 0) return;
      code = StubCache::ComputeStoreCallback(*name, *receiver, callback);
      break;
    }
    case INTERCEPTOR: {
      code = StubCache::ComputeStoreInterceptor(*name, *receiver);
      break;
    }
    default:
      return;
  }

  // If we're unable to compute the stub (not enough memory left), we
  // simply avoid updating the caches.
  if (code->IsFailure()) return;

  // Patch the call site depending on the state of the cache.
  if (state == UNINITIALIZED || state == MONOMORPHIC_PROTOTYPE_FAILURE) {
    set_target(Code::cast(code));
  } else if (state == MONOMORPHIC) {
    set_target(megamorphic_stub());
  }

#ifdef DEBUG
  TraceIC("StoreIC", name, state, target());
#endif
}


Object* KeyedStoreIC::Store(State state,
                            Handle<Object> object,
                            Handle<Object> key,
                            Handle<Object> value) {
  if (key->IsSymbol()) {
    Handle<String> name = Handle<String>::cast(key);

    // If the object is undefined or null it's illegal to try to set any
    // properties on it; throw a TypeError in that case.
    if (object->IsUndefined() || object->IsNull()) {
      return TypeError("non_object_property_store", object, name);
    }

    // Ignore stores where the receiver is not a JSObject.
    if (!object->IsJSObject()) return *value;
    Handle<JSObject> receiver = Handle<JSObject>::cast(object);

    // Check if the given name is an array index.
    uint32_t index;
    if (name->AsArrayIndex(&index)) {
      SetElement(receiver, index, value);
      return *value;
    }

    // Lookup the property locally in the receiver.
    LookupResult lookup;
    receiver->LocalLookup(*name, &lookup);

    // Update inline cache and stub cache.
    if (FLAG_use_ic && lookup.IsLoaded()) {
      UpdateCaches(&lookup, state, receiver, name, value);
    }

    // Set the property.
    return receiver->SetProperty(*name, *value, NONE);
  }

  // Do not use ICs for objects that require access checks (including
  // the global object).
  bool use_ic = FLAG_use_ic && !object->IsAccessCheckNeeded();

  if (use_ic) set_target(generic_stub());

  // Set the property.
  return Runtime::SetObjectProperty(object, key, value, NONE);
}


void KeyedStoreIC::UpdateCaches(LookupResult* lookup,
                                State state,
                                Handle<JSObject> receiver,
                                Handle<String> name,
                                Handle<Object> value) {
  ASSERT(lookup->IsLoaded());
  // Bail out if we didn't find a result.
  if (!lookup->IsValid() || !lookup->IsCacheable()) return;

  // If the property is read-only, we leave the IC in its current
  // state.
  if (lookup->IsReadOnly()) return;

  // If the property has a non-field type allowing map transitions
  // where there is extra room in the object, we leave the IC in its
  // current state.
  PropertyType type = lookup->type();

  // Compute the code stub for this store; used for rewriting to
  // monomorphic state and making sure that the code stub is in the
  // stub cache.
  Object* code = NULL;

  switch (type) {
    case FIELD: {
      code = StubCache::ComputeKeyedStoreField(*name, *receiver,
                                               lookup->GetFieldIndex());
      break;
    }
    case MAP_TRANSITION: {
      if (lookup->GetAttributes() == NONE) {
        if (receiver->map()->unused_property_fields() == 0) return;
        HandleScope scope;
        ASSERT(type == MAP_TRANSITION &&
               (receiver->map()->unused_property_fields() > 0));
        Handle<Map> transition(lookup->GetTransitionMap());
        int index = transition->PropertyIndexFor(*name);
        code = StubCache::ComputeKeyedStoreField(*name, *receiver,
                                                 index, *transition);
        break;
      }
      // fall through.
    }
    default: {
      // Always rewrite to the generic case so that we do not
      // repeatedly try to rewrite.
      code = generic_stub();
      break;
    }
  }

  // If we're unable to compute the stub (not enough memory left), we
  // simply avoid updating the caches.
  if (code->IsFailure()) return;

  // Patch the call site depending on the state of the cache.  Make
  // sure to always rewrite from monomorphic to megamorphic.
  ASSERT(state != MONOMORPHIC_PROTOTYPE_FAILURE);
  if (state == UNINITIALIZED || state == PREMONOMORPHIC) {
    set_target(Code::cast(code));
  } else if (state == MONOMORPHIC) {
    set_target(megamorphic_stub());
  }

#ifdef DEBUG
  TraceIC("KeyedStoreIC", name, state, target());
#endif
}


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

// Used from ic_<arch>.cc.
Object* CallIC_Miss(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 2);
  CallIC ic;
  IC::State state = IC::StateFrom(ic.target(), args[0]);
  return ic.LoadFunction(state, args.at<Object>(0), args.at<String>(1));
}


void CallIC::GenerateInitialize(MacroAssembler* masm, int argc) {
  Generate(masm, argc, ExternalReference(IC_Utility(kCallIC_Miss)));
}


void CallIC::GeneratePreMonomorphic(MacroAssembler* masm, int argc) {
  Generate(masm, argc, ExternalReference(IC_Utility(kCallIC_Miss)));
}


void CallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  Generate(masm, argc, ExternalReference(IC_Utility(kCallIC_Miss)));
}


// Used from ic_<arch>.cc.
Object* LoadIC_Miss(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 2);
  LoadIC ic;
  IC::State state = IC::StateFrom(ic.target(), args[0]);
  return ic.Load(state, args.at<Object>(0), args.at<String>(1));
}


void LoadIC::GenerateInitialize(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


void LoadIC::GeneratePreMonomorphic(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


// Used from ic_<arch>.cc
Object* KeyedLoadIC_Miss(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 2);
  KeyedLoadIC ic;
  IC::State state = IC::StateFrom(ic.target(), args[0]);
  return ic.Load(state, args.at<Object>(0), args.at<Object>(1));
}


void KeyedLoadIC::GenerateInitialize(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kKeyedLoadIC_Miss)));
}


void KeyedLoadIC::GeneratePreMonomorphic(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kKeyedLoadIC_Miss)));
}


// Used from ic_<arch>.cc.
Object* StoreIC_Miss(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 3);
  StoreIC ic;
  IC::State state = IC::StateFrom(ic.target(), args[0]);
  return ic.Store(state, args.at<Object>(0), args.at<String>(1),
                  args.at<Object>(2));
}


void StoreIC::GenerateInitialize(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kStoreIC_Miss)));
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kStoreIC_Miss)));
}


// Used from ic_<arch>.cc.
Object* KeyedStoreIC_Miss(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 3);
  KeyedStoreIC ic;
  IC::State state = IC::StateFrom(ic.target(), args[0]);
  return ic.Store(state, args.at<Object>(0), args.at<Object>(1),
                  args.at<Object>(2));
}


void KeyedStoreIC::GenerateInitialize(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kKeyedStoreIC_Miss)));
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kKeyedStoreIC_Miss)));
}


static Address IC_utilities[] = {
#define ADDR(name) FUNCTION_ADDR(name),
    IC_UTIL_LIST(ADDR)
    NULL
#undef ADDR
};


Address IC::AddressFromUtilityId(IC::UtilityId id) {
  return IC_utilities[id];
}


} }  // namespace v8::internal
