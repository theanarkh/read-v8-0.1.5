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

#include "api.h"
#include "bootstrapper.h"
#include "builtins.h"
#include "ic-inl.h"

namespace v8 { namespace internal {

// ----------------------------------------------------------------------------
// Support macros for defining builtins in C.
// ----------------------------------------------------------------------------
//
// A builtin function is defined by writing:
//
//   BUILTIN_<n>(name, ...)
//   {
//     ...
//   }
//   BUILTIN_END
//
// where <n> is the number of arguments (not counting the receiver). The
// names of the arguments must be listed after the name in the declaration.
// In the body of the builtin function, the variables 'env' and 'receiver'
// are visible. The arguments can be accessed through:
//
//   BUILTIN_ARG(0): Receiver (also available as 'receiver')
//   BUILTIN_ARG(1): First argument
//     ...
//   BUILTIN_ARG(n): Last argument
//
// and they evaluate to undefined values if too few arguments were
// passed to the builtin function invocation.
// ----------------------------------------------------------------------------


// TODO(1238487): This is not okay. We need to get rid of this macro
// and start calling the builtins in a more direct way. Looking at the
// stack frames for all builtin invocations comes with a pretty
// significant performance penalty.
#define BUILTIN_0(name)                                                 \
  static Object* Builtin_##name(int __argc__,                           \
                                Object** __argv__) {                    \
    Handle<Object> receiver(&__argv__[0]);                              \
    bool is_construct = false;                                          \
    USE(__argc__);                                                      \
    USE(__argv__);                                                      \
    { StackFrameIterator it;                                            \
      ASSERT(it.frame()->is_exit());                                    \
      it.Advance();                                                     \
      StackFrame::Type type = it.frame()->type();                       \
      if (type == StackFrame::INTERNAL) {                               \
        InternalFrame* frame = InternalFrame::cast(it.frame());         \
        is_construct = frame->is_construct_trampoline();                \
      } else if (type == StackFrame::ARGUMENTS_ADAPTOR) {               \
        ArgumentsAdaptorFrame* frame =                                  \
            ArgumentsAdaptorFrame::cast(it.frame());                    \
        __argc__ = frame->GetProvidedParametersCount();                 \
        __argv__ = reinterpret_cast<Object**>(frame->pp()) - 1;         \
        it.Advance();                                                   \
        is_construct =                                                  \
            it.frame()->is_internal() &&                                \
            InternalFrame::cast(it.frame())->is_construct_trampoline(); \
      }                                                                 \
    }


#define BUILTIN_1(name, a0)     \
  BUILTIN_0(name)               \
  Object* a0 = BUILTIN_ARG(1);


#define BUILTIN_2(name, a0, a1)  \
  BUILTIN_1(name, a0)            \
  Object* a1 = BUILTIN_ARG(2);


#define BUILTIN_3(name, a0, a1, a2)  \
  BUILTIN_2(name, a0, a1)            \
  Object* a2 = BUILTIN_ARG(3);


#define BUILTIN_VARARG(name, aidx0, aidxN)    \
  BUILTIN_0(name);                            \
  int aidx0 = 1;                              \
  int aidxN = __argc__;                       \


// Use an inline function to avoid evaluating the index (n) more than
// once in the BUILTIN_ARG macro.
static inline Object* __builtin_arg__(int n, int argc, Object** argv) {
  ASSERT(n >= 0);
  return (argc >= n) ? argv[-n] : Heap::undefined_value();
}


// NOTE: Argument 0 is the receiver. The first 'real' argument is
// argument 1 - BUILTIN_ARG(1).
#define BUILTIN_ARG(n) (__builtin_arg__(n, __argc__, __argv__))


#define BUILTIN_END                             \
  return Heap::undefined_value();               \
}


// ----------------------------------------------------------------------------


int Builtins::construct_call_pc_offset_ = 0;
int Builtins::arguments_adaptor_call_pc_offset_ = 0;


// Check if the builtin was called in a 'new' call.
bool Builtins::IsConstructCall(Address pc) {
  ASSERT(construct_call_pc_offset_ > 0);
  int offset = pc - builtin(JSConstructCall)->address();
  return offset == construct_call_pc_offset_;
}


bool Builtins::IsArgumentsAdaptorCall(Address pc) {
  ASSERT(arguments_adaptor_call_pc_offset_ > 0);
  int offset = pc - builtin(ArgumentsAdaptorTrampoline)->address();
  return offset == arguments_adaptor_call_pc_offset_;
}


BUILTIN_0(Illegal) {
  UNREACHABLE();
}
BUILTIN_END


BUILTIN_0(EmptyFunction) {
}
BUILTIN_END


BUILTIN_0(ArrayCode) {
  JSArray* array;
  if (is_construct) {
    array = JSArray::cast(*receiver);
  } else {
    // Allocate the JS Array
    JSFunction* constructor =
        Top::context()->global_context()->array_function();
    Object* obj = Heap::AllocateJSObject(constructor);
    if (obj->IsFailure()) return obj;
    array = JSArray::cast(obj);
  }

  // 'array' now contains the JSArray we should initialize.

  // Optimize the case where there is one argument and the argument is a
  // small smi.
  if (__argc__ == 1) {
    Object* obj = BUILTIN_ARG(1);
    if (obj->IsSmi()) {
      int len = Smi::cast(obj)->value();
      if (len >= 0 && len < JSObject::kMaxFastElementsLength) {
        Object* obj = Heap::AllocateFixedArrayWithHoles(len);
        if (obj->IsFailure()) return obj;
        array->SetContent(FixedArray::cast(obj));
        return array;
      }
    }
    // Take the argument as the length.
    obj = array->Initialize(0);
    if (obj->IsFailure()) return obj;
    if (__argc__ == 1) return array->SetElementsLength(BUILTIN_ARG(1));
  }

  // Optimize the case where there are no paramaters passed.
  if (__argc__ == 0) return array->Initialize(4);

  // Take the arguments as elements.
  int len = Smi::FromInt(__argc__)->value();
  Object* obj = Heap::AllocateFixedArrayWithHoles(len);
  if (obj->IsFailure()) return obj;
  FixedArray* elms = FixedArray::cast(obj);
  FixedArray::WriteBarrierMode mode = elms->GetWriteBarrierMode();
  // Fill in the content
  for (int index = 0; index < __argc__; index++) {
    elms->set(index, BUILTIN_ARG(index+1), mode);
  }

  // Set length and elements on the array.
  array->set_elements(FixedArray::cast(obj));
  array->set_length(Smi::FromInt(__argc__));

  return array;
}
BUILTIN_END


BUILTIN_0(ArrayPush) {
  JSArray* array = JSArray::cast(*receiver);
  ASSERT(array->HasFastElements());

  // Make sure we have space for the elements.
  int len = Smi::cast(array->length())->value();

  // Set new length.
  int new_length = len + __argc__;
  FixedArray* elms = FixedArray::cast(array->elements());

  if (new_length <= elms->length()) {
    // Backing storage has extra space for the provided values.
    for (int index = 0; index < __argc__; index++) {
      elms->set(index + len, BUILTIN_ARG(index+1));
    }
  } else {
    // New backing storage is needed.
    int capacity = new_length + (new_length >> 1) + 16;
    Object* obj = Heap::AllocateFixedArrayWithHoles(capacity);
    if (obj->IsFailure()) return obj;
    FixedArray* new_elms = FixedArray::cast(obj);
    FixedArray::WriteBarrierMode mode = new_elms->GetWriteBarrierMode();
    // Fill out the new array with old elements.
    for (int i = 0; i < len; i++) new_elms->set(i, elms->get(i), mode);
    // Add the provided values.
    for (int index = 0; index < __argc__; index++) {
      new_elms->set(index + len, BUILTIN_ARG(index+1), mode);
    }
    // Set the new backing storage.
    array->set_elements(new_elms);
  }
  // Set the length.
  array->set_length(Smi::FromInt(new_length));
  return array->length();
}
BUILTIN_END


BUILTIN_0(ArrayPop) {
  JSArray* array = JSArray::cast(*receiver);
  ASSERT(array->HasFastElements());
  Object* undefined = Heap::undefined_value();

  int len = Smi::cast(array->length())->value();
  if (len == 0) return undefined;

  // Get top element
  FixedArray* elms = FixedArray::cast(array->elements());
  Object* top = elms->get(len - 1);

  // Set the length.
  array->set_length(Smi::FromInt(len - 1));

  if (!top->IsTheHole()) {
    // Delete the top element.
    elms->set_the_hole(len - 1);

    return top;
  }

  // Remember to check the prototype chain.
  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  JSObject* prototype = JSObject::cast(array_function->prototype());
  top = prototype->GetElement(len - 1);

  return top;
}
BUILTIN_END


// -----------------------------------------------------------------------------
//


// Returns the holder JSObject if the function can legally be called
// with this receiver.  Returns Heap::null_value() if the call is
// illegal.  Any arguments that don't fit the expected type is
// overwritten with undefined.  Arguments that do fit the expected
// type is overwritten with the object in the prototype chain that
// actually has that type.
static inline Object* TypeCheck(int argc,
                                Object** argv,
                                FunctionTemplateInfo* info) {
  Object* recv = argv[0];
  Object* sig_obj = info->signature();
  if (sig_obj->IsUndefined()) return recv;
  SignatureInfo* sig = SignatureInfo::cast(sig_obj);
  // If necessary, check the receiver
  Object* recv_type = sig->receiver();

  Object* holder = recv;
  if (!recv_type->IsUndefined()) {
    for (; holder != Heap::null_value(); holder = holder->GetPrototype()) {
      if (holder->IsInstanceOf(FunctionTemplateInfo::cast(recv_type))) {
        break;
      }
    }
    if (holder == Heap::null_value()) return holder;
  }
  Object* args_obj = sig->args();
  // If there is no argument signature we're done
  if (args_obj->IsUndefined()) return holder;
  FixedArray* args = FixedArray::cast(args_obj);
  int length = args->length();
  if (argc < length) length = argc;
  for (int i = 0; i < length; i++) {
    Object* argtype = args->get(i);
    if (argtype->IsUndefined()) continue;
    Object** arg = &argv[-1 - i];
    Object* current = *arg;
    for (; current != Heap::null_value(); current = current->GetPrototype()) {
      if (current->IsInstanceOf(FunctionTemplateInfo::cast(argtype))) {
        *arg = current;
        break;
      }
    }
    if (current == Heap::null_value()) *arg = Heap::undefined_value();
  }
  return holder;
}


BUILTIN_0(HandleApiCall) {
  HandleScope scope;

  // TODO(1238487): This is not nice. We need to get rid of this
  // retarded behavior and start handling API calls in a more direct
  // way - maybe compile specialized stubs lazily?.
#ifdef USE_OLD_CALLING_CONVENTIONS
  Handle<JSFunction> function =
      Handle<JSFunction>(JSFunction::cast(__argv__[1]));
#else
  Handle<JSFunction> function =
      Handle<JSFunction>(JSFunction::cast(Builtins::builtin_passed_function));
#endif

  if (is_construct) {
    Handle<FunctionTemplateInfo> desc =
        Handle<FunctionTemplateInfo>(
            FunctionTemplateInfo::cast(function->shared()->function_data()));
    bool pending_exception = false;
    Factory::ConfigureInstance(desc, Handle<JSObject>::cast(receiver),
                               &pending_exception);
    ASSERT(Top::has_pending_exception() == pending_exception);
    if (pending_exception) return Failure::Exception();
  }

  FunctionTemplateInfo* fun_data =
      FunctionTemplateInfo::cast(function->shared()->function_data());
  Object* raw_holder = TypeCheck(__argc__, __argv__, fun_data);

  if (raw_holder->IsNull()) {
    // This function cannot be called with the given receiver.  Abort!
    Handle<Object> obj =
        Factory::NewTypeError("illegal_invocation", HandleVector(&function, 1));
    return Top::Throw(*obj);
  }

  Object* raw_call_data = fun_data->call_code();
  if (!raw_call_data->IsUndefined()) {
    CallHandlerInfo* call_data = CallHandlerInfo::cast(raw_call_data);
    Object* callback_obj = call_data->callback();
    v8::InvocationCallback callback =
        v8::ToCData<v8::InvocationCallback>(callback_obj);
    Object* data_obj = call_data->data();
    Object* result;

    v8::Local<v8::Object> self =
        v8::Utils::ToLocal(Handle<JSObject>::cast(receiver));
    Handle<Object> data_handle(data_obj);
    v8::Local<v8::Value> data = v8::Utils::ToLocal(data_handle);
    ASSERT(raw_holder->IsJSObject());
    v8::Local<v8::Function> callee = v8::Utils::ToLocal(function);
    Handle<JSObject> holder_handle(JSObject::cast(raw_holder));
    v8::Local<v8::Object> holder = v8::Utils::ToLocal(holder_handle);
    LOG(ApiObjectAccess("call", JSObject::cast(*receiver)));
    v8::Arguments args = v8::ImplementationUtilities::NewArguments(
        data,
        holder,
        callee,
        is_construct,
        reinterpret_cast<void**>(__argv__ - 1),
        __argc__);

    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      value = callback(args);
    }
    if (value.IsEmpty()) {
      result = Heap::undefined_value();
    } else {
      result = *reinterpret_cast<Object**>(*value);
    }

    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!is_construct || result->IsJSObject()) return result;
  }

  return *receiver;
}
BUILTIN_END


// Handle calls to non-function objects created through the API that
// support calls.
BUILTIN_0(HandleApiCallAsFunction) {
  // Non-functions are never called as constructors.
  ASSERT(!is_construct);

  // Get the object called.
  JSObject* obj = JSObject::cast(*receiver);

  // Get the invocation callback from the function descriptor that was
  // used to create the called object.
  ASSERT(obj->map()->has_instance_call_handler());
  JSFunction* constructor = JSFunction::cast(obj->map()->constructor());
  Object* template_info = constructor->shared()->function_data();
  Object* handler =
      FunctionTemplateInfo::cast(template_info)->instance_call_handler();
  ASSERT(!handler->IsUndefined());
  CallHandlerInfo* call_data = CallHandlerInfo::cast(handler);
  Object* callback_obj = call_data->callback();
  v8::InvocationCallback callback =
      v8::ToCData<v8::InvocationCallback>(callback_obj);

  // Get the data for the call and perform the callback.
  Object* data_obj = call_data->data();
  Object* result;
  { HandleScope scope;
    v8::Local<v8::Object> self =
        v8::Utils::ToLocal(Handle<JSObject>::cast(receiver));
    Handle<Object> data_handle(data_obj);
    v8::Local<v8::Value> data = v8::Utils::ToLocal(data_handle);
    Handle<JSFunction> callee_handle(constructor);
    v8::Local<v8::Function> callee = v8::Utils::ToLocal(callee_handle);
    LOG(ApiObjectAccess("call non-function", JSObject::cast(*receiver)));
    v8::Arguments args = v8::ImplementationUtilities::NewArguments(
        data,
        self,
        callee,
        is_construct,
        reinterpret_cast<void**>(__argv__ - 1),
        __argc__);
    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      value = callback(args);
    }
    if (value.IsEmpty()) {
      result = Heap::undefined_value();
    } else {
      result = *reinterpret_cast<Object**>(*value);
    }
  }
  // Check for exceptions and return result.
  RETURN_IF_SCHEDULED_EXCEPTION();
  return result;
}
BUILTIN_END


// TODO(1238487): This is a nasty hack. We need to improve the way we
// call builtins considerable to get rid of this and the hairy macros
// in builtins.cc.
Object* Builtins::builtin_passed_function;



static void Generate_LoadIC_ArrayLength(MacroAssembler* masm) {
  LoadIC::GenerateArrayLength(masm);
}


static void Generate_LoadIC_ShortStringLength(MacroAssembler* masm) {
  LoadIC::GenerateShortStringLength(masm);
}


static void Generate_LoadIC_MediumStringLength(MacroAssembler* masm) {
  LoadIC::GenerateMediumStringLength(masm);
}


static void Generate_LoadIC_LongStringLength(MacroAssembler* masm) {
  LoadIC::GenerateLongStringLength(masm);
}


static void Generate_LoadIC_FunctionPrototype(MacroAssembler* masm) {
  LoadIC::GenerateFunctionPrototype(masm);
}


static void Generate_LoadIC_Initialize(MacroAssembler* masm) {
  LoadIC::GenerateInitialize(masm);
}


static void Generate_LoadIC_PreMonomorphic(MacroAssembler* masm) {
  LoadIC::GeneratePreMonomorphic(masm);
}


static void Generate_LoadIC_Miss(MacroAssembler* masm) {
  LoadIC::GenerateMiss(masm);
}


static void Generate_LoadIC_Megamorphic(MacroAssembler* masm) {
  LoadIC::GenerateMegamorphic(masm);
}


static void Generate_LoadIC_Normal(MacroAssembler* masm) {
  LoadIC::GenerateNormal(masm);
}


static void Generate_KeyedLoadIC_Initialize(MacroAssembler* masm) {
  KeyedLoadIC::GenerateInitialize(masm);
}


static void Generate_KeyedLoadIC_Miss(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMiss(masm);
}


static void Generate_KeyedLoadIC_Generic(MacroAssembler* masm) {
  KeyedLoadIC::GenerateGeneric(masm);
}


static void Generate_KeyedLoadIC_PreMonomorphic(MacroAssembler* masm) {
  KeyedLoadIC::GeneratePreMonomorphic(masm);
}


static void Generate_StoreIC_Initialize(MacroAssembler* masm) {
  StoreIC::GenerateInitialize(masm);
}


static void Generate_StoreIC_Miss(MacroAssembler* masm) {
  StoreIC::GenerateMiss(masm);
}


static void Generate_StoreIC_Megamorphic(MacroAssembler* masm) {
  StoreIC::GenerateMegamorphic(masm);
}


static void Generate_KeyedStoreIC_Generic(MacroAssembler* masm) {
  KeyedStoreIC::GenerateGeneric(masm);
}


static void Generate_KeyedStoreIC_Miss(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMiss(masm);
}


static void Generate_KeyedStoreIC_Initialize(MacroAssembler* masm) {
  KeyedStoreIC::GenerateInitialize(masm);
}


Object* Builtins::builtins_[builtin_count] = { NULL, };
const char* Builtins::names_[builtin_count] = { NULL, };

#define DEF_ENUM_C(name, ignore) FUNCTION_ADDR(Builtin_##name),
  Address Builtins::c_functions_[cfunction_count] = {
    BUILTIN_LIST_C(DEF_ENUM_C)
  };
#undef DEF_ENUM_C

#define DEF_JS_NAME(name, ignore) #name,
#define DEF_JS_ARGC(ignore, argc) argc,
const char* Builtins::javascript_names_[id_count] = {
  BUILTINS_LIST_JS(DEF_JS_NAME)
};

int Builtins::javascript_argc_[id_count] = {
  BUILTINS_LIST_JS(DEF_JS_ARGC)
};
#undef DEF_JS_NAME
#undef DEF_JS_ARGC

static bool is_initialized = false;
void Builtins::Setup(bool create_heap_objects) {
  ASSERT(!is_initialized);

  // Create a scope for the handles in the builtins.
  HandleScope scope;

  struct BuiltinDesc {
    byte* generator;
    byte* c_code;
    const char* s_name;  // name is only used for generating log information.
    int name;
    Code::Flags flags;
    int argc;
  };

#define DEF_FUNCTION_PTR_C(name, argc)   \
    { FUNCTION_ADDR(Generate_Adaptor),   \
      FUNCTION_ADDR(Builtin_##name),     \
      #name,                             \
      c_##name,                          \
      Code::ComputeFlags(Code::BUILTIN), \
      argc                               \
    },

#define DEF_FUNCTION_PTR_A(name, kind, state) \
    { FUNCTION_ADDR(Generate_##name),         \
      NULL,                                   \
      #name,                                  \
      name,                                   \
      Code::ComputeFlags(Code::kind, state),  \
      -1                                      \
    },

  // Define array of pointers to generators and C builtin functions.
  static BuiltinDesc functions[] = {
      BUILTIN_LIST_C(DEF_FUNCTION_PTR_C)
      BUILTIN_LIST_A(DEF_FUNCTION_PTR_A)
      // Terminator:
      { NULL, NULL, NULL, builtin_count, static_cast<Code::Flags>(0), -1}
  };

#undef DEF_FUNCTION_PTR_C
#undef DEF_FUNCTION_PTR_A

  // For now we generate builtin adaptor code into a stack-allocated
  // buffer, before copying it into invididual code objects.
  byte buffer[4*KB];

  // Traverse the list of builtins and generate an adaptor in a
  // separate code object for each one.
  for (int i = 0; i < builtin_count; i++) {
    if (create_heap_objects) {
      MacroAssembler masm(buffer, sizeof buffer);
      // Generate the code/adaptor.
      typedef void (*Generator)(MacroAssembler*, int, int);
      Generator g = FUNCTION_CAST<Generator>(functions[i].generator);
      // We pass all arguments to the generator, but it may not use all of
      // them.  This works because the first arguments are on top of the
      // stack.
      g(&masm, functions[i].argc, functions[i].name);
      // Move the code into the object heap.
      CodeDesc desc;
      masm.GetCode(&desc);
      Code::Flags flags =  functions[i].flags;
      Object* code = Heap::CreateCode(desc, NULL, flags);
      if (code->IsRetryAfterGC()) {
        CHECK(Heap::CollectGarbage(Failure::cast(code)->requested(),
                                   Failure::cast(code)->allocation_space()));
        code = Heap::CreateCode(desc, NULL, flags);
      }
      // Add any unresolved jumps or calls to the fixup list in the
      // bootstrapper.
      Bootstrapper::AddFixup(Code::cast(code), &masm);
      // Log the event and add the code to the builtins array.
      LOG(CodeCreateEvent("Builtin", Code::cast(code), functions[i].s_name));
      builtins_[i] = code;
    } else {
      // Deserializing. The values will be filled in during IterateBuiltins.
      builtins_[i] = NULL;
    }
    names_[i] = functions[i].s_name;
  }

  // Mark as initialized.
  is_initialized = true;
}


void Builtins::TearDown() {
  is_initialized = false;
}


void Builtins::IterateBuiltins(ObjectVisitor* v) {
  v->VisitPointers(&builtins_[0], &builtins_[0] + builtin_count);
}


const char* Builtins::Lookup(byte* pc) {
  if (is_initialized) {  // may be called during initialization (disassembler!)
    for (int i = 0; i < builtin_count; i++) {
      Code* entry = Code::cast(builtins_[i]);
      if (entry->contains(pc)) {
        return names_[i];
      }
    }
  }
  return NULL;
}


} }  // namespace v8::internal
