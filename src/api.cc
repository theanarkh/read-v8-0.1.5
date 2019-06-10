// Copyright 2007-2008 Google Inc. All Rights Reserved.
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
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "platform.h"
#include "serialize.h"
#include "snapshot.h"


namespace i = v8::internal;
#define LOG_API(expr) LOG(ApiEntryCall(expr))


namespace v8 {


#define ON_BAILOUT(location, code)              \
  if (IsDeadCheck(location)) {                  \
    code;                                       \
    UNREACHABLE();                              \
  }


#define EXCEPTION_PREAMBLE()                                      \
  thread_local.IncrementCallDepth();                              \
  ASSERT(!i::Top::external_caught_exception());                   \
  bool has_pending_exception = false


#define EXCEPTION_BAILOUT_CHECK(value)                                         \
  do {                                                                         \
    thread_local.DecrementCallDepth();                                         \
    if (has_pending_exception) {                                               \
      if (thread_local.CallDepthIsZero() && i::Top::is_out_of_memory()) {      \
        if (!thread_local.IgnoreOutOfMemory())                                 \
          i::V8::FatalProcessOutOfMemory(NULL);                                \
      }                                                                        \
      bool call_depth_is_zero = thread_local.CallDepthIsZero();                \
      i::Top::optional_reschedule_exception(call_depth_is_zero);               \
      return value;                                                            \
    }                                                                          \
  } while (false)


// --- D a t a   t h a t   i s   s p e c i f i c   t o   a   t h r e a d ---


static i::HandleScopeImplementer thread_local;


// --- E x c e p t i o n   B e h a v i o r ---


static bool has_shut_down = false;
static FatalErrorCallback exception_behavior = NULL;


static void DefaultFatalErrorHandler(const char* location,
                                     const char* message) {
  API_Fatal(location, message);
}



static FatalErrorCallback& GetFatalErrorHandler() {
  if (exception_behavior == NULL) {
    exception_behavior = DefaultFatalErrorHandler;
  }
  return exception_behavior;
}



// When V8 cannot allocated memory FatalProcessOutOfMemory is called.
// The default fatal error handler is called and execution is stopped.
void i::V8::FatalProcessOutOfMemory(const char* location) {
  has_shut_down = true;
  FatalErrorCallback callback = GetFatalErrorHandler();
  callback(location, "Allocation failed - process out of memory");
  // If the callback returns, we stop execution.
  UNREACHABLE();
}


void V8::SetFatalErrorHandler(FatalErrorCallback that) {
  exception_behavior = that;
}


bool Utils::ReportApiFailure(const char* location, const char* message) {
  FatalErrorCallback callback = GetFatalErrorHandler();
  callback(location, message);
  has_shut_down = true;
  return false;
}


bool V8::IsDead() {
  return has_shut_down;
}


static inline bool ApiCheck(bool condition,
                            const char* location,
                            const char* message) {
  return condition ? true : Utils::ReportApiFailure(location, message);
}


static bool ReportV8Dead(const char* location) {
  FatalErrorCallback callback = GetFatalErrorHandler();
  callback(location, "V8 is no longer useable");
  return true;
}


static bool ReportEmptyHandle(const char* location) {
  FatalErrorCallback callback = GetFatalErrorHandler();
  callback(location, "Reading from empty handle");
  return true;
}


/**
 * IsDeadCheck checks that the vm is useable.  If, for instance, the vm has been
 * out of memory at some point this check will fail.  It should be called on
 * entry to all methods that touch anything in the heap, except destructors
 * which you sometimes can't avoid calling after the vm has crashed.  Functions
 * that call EnsureInitialized or ON_BAILOUT don't have to also call
 * IsDeadCheck.  ON_BAILOUT has the advantage over EnsureInitialized that you
 * can arrange to return if the VM is dead.  This is needed to ensure that no VM
 * heap allocations are attempted on a dead VM.  EnsureInitialized has the
 * advantage over ON_BAILOUT that it actually initializes the VM if this has not
 * yet been done.
 */
static inline bool IsDeadCheck(const char* location) {
  return has_shut_down ? ReportV8Dead(location) : false;
}


static inline bool EmptyCheck(const char* location, v8::Handle<v8::Data> obj) {
  return obj.IsEmpty() ? ReportEmptyHandle(location) : false;
}


static inline bool EmptyCheck(const char* location, v8::Data* obj) {
  return (obj == 0) ? ReportEmptyHandle(location) : false;
}

// --- S t a t i c s ---


static i::StringInputBuffer write_input_buffer;


static void EnsureInitialized(const char* location) {
  if (IsDeadCheck(location)) return;
  ApiCheck(v8::V8::Initialize(), location, "Error initializing V8");
}


v8::Handle<v8::Primitive> ImplementationUtilities::Undefined() {
  if (IsDeadCheck("v8::Undefined()")) return v8::Handle<v8::Primitive>();
  EnsureInitialized("v8::Undefined()");
  return v8::Handle<Primitive>(ToApi<Primitive>(i::Factory::undefined_value()));
}


v8::Handle<v8::Primitive> ImplementationUtilities::Null() {
  if (IsDeadCheck("v8::Null()")) return v8::Handle<v8::Primitive>();
  EnsureInitialized("v8::Null()");
  return v8::Handle<Primitive>(ToApi<Primitive>(i::Factory::null_value()));
}


v8::Handle<v8::Boolean> ImplementationUtilities::True() {
  if (IsDeadCheck("v8::True()")) return v8::Handle<v8::Boolean>();
  EnsureInitialized("v8::True()");
  return v8::Handle<v8::Boolean>(ToApi<Boolean>(i::Factory::true_value()));
}


v8::Handle<v8::Boolean> ImplementationUtilities::False() {
  if (IsDeadCheck("v8::False()")) return v8::Handle<v8::Boolean>();
  EnsureInitialized("v8::False()");
  return v8::Handle<v8::Boolean>(ToApi<Boolean>(i::Factory::false_value()));
}


void V8::SetFlagsFromString(const char* str, int length) {
  i::FlagList::SetFlagsFromString(str, length);
}


v8::Handle<Value> ThrowException(v8::Handle<v8::Value> value) {
  if (IsDeadCheck("v8::ThrowException()")) return v8::Handle<Value>();
  i::Top::ScheduleThrow(*Utils::OpenHandle(*value));
  return v8::Undefined();
}


RegisteredExtension* RegisteredExtension::first_extension_ = NULL;


RegisteredExtension::RegisteredExtension(Extension* extension)
    : extension_(extension), state_(UNVISITED) { }


void RegisteredExtension::Register(RegisteredExtension* that) {
  that->next_ = RegisteredExtension::first_extension_;
  RegisteredExtension::first_extension_ = that;
}


void RegisterExtension(Extension* that) {
  RegisteredExtension* extension = new RegisteredExtension(that);
  RegisteredExtension::Register(extension);
}


Extension::Extension(const char* name,
                     const char* source,
                     int dep_count,
                     const char** deps)
    : name_(name),
      source_(source),
      dep_count_(dep_count),
      deps_(deps),
      auto_enable_(false) { }


v8::Handle<Primitive> Undefined() {
  LOG_API("Undefined");
  return ImplementationUtilities::Undefined();
}


v8::Handle<Primitive> Null() {
  LOG_API("Null");
  return ImplementationUtilities::Null();
}


v8::Handle<Boolean> True() {
  LOG_API("True");
  return ImplementationUtilities::True();
}


v8::Handle<Boolean> False() {
  LOG_API("False");
  return ImplementationUtilities::False();
}


ResourceConstraints::ResourceConstraints()
  : max_young_space_size_(0),
    max_old_space_size_(0),
    stack_limit_(NULL) { }


bool SetResourceConstraints(ResourceConstraints* constraints) {
  bool result = i::Heap::ConfigureHeap(constraints->max_young_space_size(),
                                       constraints->max_old_space_size());
  if (!result) return false;
  if (constraints->stack_limit() != NULL) {
    uintptr_t limit = reinterpret_cast<uintptr_t>(constraints->stack_limit());
    i::StackGuard::SetStackLimit(limit);
  }
  return true;
}


void** V8::GlobalizeReference(void** obj) {
  LOG_API("Persistent::New");
  if (IsDeadCheck("V8::Persistent::New")) return NULL;
  i::Handle<i::Object> result =
      i::GlobalHandles::Create(*reinterpret_cast<i::Object**>(obj));
  return reinterpret_cast<void**>(result.location());
}


void V8::MakeWeak(void** object, void* parameters,
                  WeakReferenceCallback callback) {
  LOG_API("MakeWeak");
  i::GlobalHandles::MakeWeak(reinterpret_cast<i::Object**>(object), parameters,
      callback);
}


void V8::ClearWeak(void** obj) {
  LOG_API("ClearWeak");
  i::GlobalHandles::ClearWeakness(reinterpret_cast<i::Object**>(obj));
}


bool V8::IsGlobalNearDeath(void** obj) {
  LOG_API("IsGlobalNearDeath");
  if (has_shut_down) return false;
  return i::GlobalHandles::IsNearDeath(reinterpret_cast<i::Object**>(obj));
}


bool V8::IsGlobalWeak(void** obj) {
  LOG_API("IsGlobalWeak");
  if (has_shut_down) return false;
  return i::GlobalHandles::IsWeak(reinterpret_cast<i::Object**>(obj));
}


void V8::DisposeGlobal(void** obj) {
  LOG_API("DisposeGlobal");
  if (has_shut_down) return;
  i::GlobalHandles::Destroy(reinterpret_cast<i::Object**>(obj));
}

// --- H a n d l e s ---


HandleScope::Data HandleScope::current_ = { -1, NULL, NULL };


int HandleScope::NumberOfHandles() {
  int n = thread_local.Blocks()->length();
  if (n == 0) return 0;
  return ((n - 1) * i::kHandleBlockSize) +
       (current_.next - thread_local.Blocks()->last());
}


void** v8::HandleScope::CreateHandle(void* value) {
  void** result = current_.next;
  if (result == current_.limit) {
    // Make sure there's at least one scope on the stack and that the
    // top of the scope stack isn't a barrier.
    if (!ApiCheck(current_.extensions >= 0,
                  "v8::HandleScope::CreateHandle()",
                  "Cannot create a handle without a HandleScope")) {
      return NULL;
    }
    // If there's more room in the last block, we use that. This is used
    // for fast creation of scopes after scope barriers.
    if (!thread_local.Blocks()->is_empty()) {
      void** limit = &thread_local.Blocks()->last()[i::kHandleBlockSize];
      if (current_.limit != limit) {
        current_.limit = limit;
      }
    }

    // If we still haven't found a slot for the handle, we extend the
    // current handle scope by allocating a new handle block.
    if (result == current_.limit) {
      // If there's a spare block, use it for growing the current scope.
      result = thread_local.GetSpareOrNewBlock();
      // Add the extension to the global list of blocks, but count the
      // extension as part of the current scope.
      thread_local.Blocks()->Add(result);
      current_.extensions++;
      current_.limit = &result[i::kHandleBlockSize];
    }
  }

  // Update the current next field, set the value in the created
  // handle, and return the result.
  ASSERT(result < current_.limit);
  current_.next = result + 1;
  *result = value;
  return result;
}


void Context::Enter() {
  if (IsDeadCheck("v8::Context::Enter()")) return;
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  thread_local.EnterContext(env);

  thread_local.SaveContext(i::GlobalHandles::Create(i::Top::context()));
  i::Top::set_context(*env);

  thread_local.SaveSecurityContext(
      i::GlobalHandles::Create(i::Top::security_context()));
  i::Top::set_security_context(*env);
}


void Context::Exit() {
  if (has_shut_down) return;
  if (!ApiCheck(thread_local.LeaveLastContext(),
                "v8::Context::Exit()",
                "Cannot exit non-entered context")) {
    return;
  }

  // Content of 'last_context' and 'last_security_context' could be NULL.
  i::Handle<i::Object> last_context = thread_local.RestoreContext();
  i::Top::set_context(static_cast<i::Context*>(*last_context));
  i::GlobalHandles::Destroy(last_context.location());

  i::Handle<i::Object> last_security_context =
      thread_local.RestoreSecurityContext();
  i::Top::set_security_context(
      static_cast<i::Context*>(*last_security_context));
  i::GlobalHandles::Destroy(last_security_context.location());
}


void v8::HandleScope::DeleteExtensions() {
  ASSERT(current_.extensions != 0);
  thread_local.DeleteExtensions(current_.extensions);
}


#ifdef DEBUG
void HandleScope::ZapRange(void** start, void** end) {
  if (start == NULL) return;
  for (void** p = start; p < end; p++) {
    *p = reinterpret_cast<void*>(v8::internal::kHandleZapValue);
  }
}
#endif


void** v8::HandleScope::RawClose(void** value) {
  if (!ApiCheck(!is_closed_,
                "v8::HandleScope::Close()",
                "Local scope has already been closed")) {
    return 0;
  }
  LOG_API("CloseHandleScope");

  // Read the result before popping the handle block.
  i::Object* result = reinterpret_cast<i::Object*>(*value);
  is_closed_ = true;
  RestorePreviousState();

  // Allocate a new handle on the previous handle block.
  i::Handle<i::Object> handle(result);
  return reinterpret_cast<void**>(handle.location());
}


// --- N e a n d e r ---


// A constructor cannot easily return an error value, therefore it is necessary
// to check for a dead VM with ON_BAILOUT before constructing any Neander
// objects.  To remind you about this there is no HandleScope in the
// NeanderObject constructor.  When you add one to the site calling the
// constructor you should check that you ensured the VM was not dead first.
NeanderObject::NeanderObject(int size) {
  EnsureInitialized("v8::Nowhere");
  value_ = i::Factory::NewNeanderObject();
  i::Handle<i::FixedArray> elements = i::Factory::NewFixedArray(size);
  value_->set_elements(*elements);
}


int NeanderObject::size() {
  return i::FixedArray::cast(value_->elements())->length();
}


NeanderArray::NeanderArray() : obj_(2) {
  obj_.set(0, i::Smi::FromInt(0));
}


int NeanderArray::length() {
  return i::Smi::cast(obj_.get(0))->value();
}


i::Object* NeanderArray::get(int offset) {
  ASSERT(0 <= offset);
  ASSERT(offset < length());
  return obj_.get(offset + 1);
}


// This method cannot easily return an error value, therefore it is necessary
// to check for a dead VM with ON_BAILOUT before calling it.  To remind you
// about this there is no HandleScope in this method.  When you add one to the
// site calling this method you should check that you ensured the VM was not
// dead first.
void NeanderArray::add(i::Handle<i::Object> value) {
  int length = this->length();
  int size = obj_.size();
  if (length == size - 1) {
    i::Handle<i::FixedArray> new_elms = i::Factory::NewFixedArray(2 * size);
    for (int i = 0; i < length; i++)
      new_elms->set(i + 1, get(i));
    obj_.value()->set_elements(*new_elms);
  }
  obj_.set(length + 1, *value);
  obj_.set(0, i::Smi::FromInt(length + 1));
}


void NeanderArray::set(int index, i::Object* value) {
  if (index < 0 || index >= this->length()) return;
  obj_.set(index + 1, value);
}


// --- T e m p l a t e ---


static void InitializeTemplate(i::Handle<i::TemplateInfo> that, int type) {
  that->set_tag(i::Smi::FromInt(type));
}


void Template::Set(v8::Handle<String> name, v8::Handle<Data> value,
                   v8::PropertyAttribute attribute) {
  if (IsDeadCheck("v8::Template::SetProperty()")) return;
  HandleScope scope;
  i::Handle<i::Object> list(Utils::OpenHandle(this)->property_list());
  if (list->IsUndefined()) {
    list = NeanderArray().value();
    Utils::OpenHandle(this)->set_property_list(*list);
  }
  NeanderArray array(list);
  array.add(Utils::OpenHandle(*name));
  array.add(Utils::OpenHandle(*value));
  array.add(Utils::OpenHandle(*v8::Integer::New(attribute)));
}


// --- F u n c t i o n   T e m p l a t e ---
static void InitializeFunctionTemplate(
      i::Handle<i::FunctionTemplateInfo> info) {
  info->set_tag(i::Smi::FromInt(Consts::FUNCTION_TEMPLATE));
  info->set_flag(0);
}


Local<ObjectTemplate> FunctionTemplate::PrototypeTemplate() {
  if (IsDeadCheck("v8::FunctionTemplate::PrototypeTemplate()")) {
    return Local<ObjectTemplate>();
  }
  i::Handle<i::Object> result(Utils::OpenHandle(this)->prototype_template());
  if (result->IsUndefined()) {
    result = Utils::OpenHandle(*ObjectTemplate::New());
    Utils::OpenHandle(this)->set_prototype_template(*result);
  }
  return Local<ObjectTemplate>(ToApi<ObjectTemplate>(result));
}


void FunctionTemplate::Inherit(v8::Handle<FunctionTemplate> value) {
  if (IsDeadCheck("v8::FunctionTemplate::Inherit()")) return;
  Utils::OpenHandle(this)->set_parent_template(*Utils::OpenHandle(*value));
}


// To distinguish the function templates, so that we can find them in the
// function cache of the global context.
static int next_serial_number = 0;


Local<FunctionTemplate> FunctionTemplate::New(InvocationCallback callback,
    v8::Handle<Value> data, v8::Handle<Signature> signature) {
  EnsureInitialized("v8::FunctionTemplate::New()");
  LOG_API("FunctionTemplate::New");
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::FUNCTION_TEMPLATE_INFO_TYPE);
  i::Handle<i::FunctionTemplateInfo> obj =
      i::Handle<i::FunctionTemplateInfo>::cast(struct_obj);
  InitializeFunctionTemplate(obj);
  obj->set_serial_number(i::Smi::FromInt(next_serial_number++));
  if (callback != 0) {
    if (data.IsEmpty()) data = v8::Undefined();
    Utils::ToLocal(obj)->SetCallHandler(callback, data);
  }
  obj->set_undetectable(false);
  obj->set_needs_access_check(false);

  if (!signature.IsEmpty())
    obj->set_signature(*Utils::OpenHandle(*signature));
  return Utils::ToLocal(obj);
}


Local<Signature> Signature::New(Handle<FunctionTemplate> receiver,
      int argc, Handle<FunctionTemplate> argv[]) {
  EnsureInitialized("v8::Signature::New()");
  LOG_API("Signature::New");
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::SIGNATURE_INFO_TYPE);
  i::Handle<i::SignatureInfo> obj =
      i::Handle<i::SignatureInfo>::cast(struct_obj);
  if (!receiver.IsEmpty()) obj->set_receiver(*Utils::OpenHandle(*receiver));
  if (argc > 0) {
    i::Handle<i::FixedArray> args = i::Factory::NewFixedArray(argc);
    for (int i = 0; i < argc; i++) {
      if (!argv[i].IsEmpty())
        args->set(i, *Utils::OpenHandle(*argv[i]));
    }
    obj->set_args(*args);
  }
  return Utils::ToLocal(obj);
}


Local<TypeSwitch> TypeSwitch::New(Handle<FunctionTemplate> type) {
  Handle<FunctionTemplate> types[1] = { type };
  return TypeSwitch::New(1, types);
}


Local<TypeSwitch> TypeSwitch::New(int argc, Handle<FunctionTemplate> types[]) {
  EnsureInitialized("v8::TypeSwitch::New()");
  LOG_API("TypeSwitch::New");
  i::Handle<i::FixedArray> vector = i::Factory::NewFixedArray(argc);
  for (int i = 0; i < argc; i++)
    vector->set(i, *Utils::OpenHandle(*types[i]));
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::TYPE_SWITCH_INFO_TYPE);
  i::Handle<i::TypeSwitchInfo> obj =
      i::Handle<i::TypeSwitchInfo>::cast(struct_obj);
  obj->set_types(*vector);
  return Utils::ToLocal(obj);
}


int TypeSwitch::match(v8::Handle<Value> value) {
  LOG_API("TypeSwitch::match");
  i::Handle<i::Object> obj = Utils::OpenHandle(*value);
  i::Handle<i::TypeSwitchInfo> info = Utils::OpenHandle(this);
  i::FixedArray* types = i::FixedArray::cast(info->types());
  for (int i = 0; i < types->length(); i++) {
    if (obj->IsInstanceOf(i::FunctionTemplateInfo::cast(types->get(i))))
      return i + 1;
  }
  return 0;
}


void FunctionTemplate::SetCallHandler(InvocationCallback callback,
                                      v8::Handle<Value> data) {
  if (IsDeadCheck("v8::FunctionTemplate::SetCallHandler()")) return;
  HandleScope scope;
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::CALL_HANDLER_INFO_TYPE);
  i::Handle<i::CallHandlerInfo> obj =
      i::Handle<i::CallHandlerInfo>::cast(struct_obj);
  obj->set_callback(*FromCData(callback));
  if (data.IsEmpty()) data = v8::Undefined();
  obj->set_data(*Utils::OpenHandle(*data));
  Utils::OpenHandle(this)->set_call_code(*obj);
}


void FunctionTemplate::SetLookupHandler(LookupCallback handler) {
  if (IsDeadCheck("v8::FunctionTemplate::SetLookupHandler()")) return;
  HandleScope scope;
  Utils::OpenHandle(this)->set_lookup_callback(*FromCData(handler));
}


void FunctionTemplate::AddInstancePropertyAccessor(
      v8::Handle<String> name,
      AccessorGetter getter,
      AccessorSetter setter,
      v8::Handle<Value> data,
      v8::AccessControl settings,
      v8::PropertyAttribute attributes) {
  if (IsDeadCheck("v8::FunctionTemplate::AddInstancePropertyAccessor()")) {
    return;
  }
  HandleScope scope;
  i::Handle<i::AccessorInfo> obj = i::Factory::NewAccessorInfo();
  ASSERT(getter != NULL);
  obj->set_getter(*FromCData(getter));
  obj->set_setter(*FromCData(setter));
  if (data.IsEmpty()) data = v8::Undefined();
  obj->set_data(*Utils::OpenHandle(*data));
  obj->set_name(*Utils::OpenHandle(*name));
  if (settings & ALL_CAN_READ) obj->set_all_can_read(true);
  if (settings & ALL_CAN_WRITE) obj->set_all_can_write(true);
  obj->set_property_attributes(static_cast<PropertyAttributes>(attributes));

  i::Handle<i::Object> list(Utils::OpenHandle(this)->property_accessors());
  if (list->IsUndefined()) {
    list = NeanderArray().value();
    Utils::OpenHandle(this)->set_property_accessors(*list);
  }
  NeanderArray array(list);
  array.add(obj);
}


Local<ObjectTemplate> FunctionTemplate::InstanceTemplate() {
  if (IsDeadCheck("v8::FunctionTemplate::InstanceTemplate()")
      || EmptyCheck("v8::FunctionTemplate::InstanceTemplate()", this))
    return Local<ObjectTemplate>();
  if (Utils::OpenHandle(this)->instance_template()->IsUndefined()) {
    Local<ObjectTemplate> templ =
        ObjectTemplate::New(v8::Handle<FunctionTemplate>(this));
    Utils::OpenHandle(this)->set_instance_template(*Utils::OpenHandle(*templ));
  }
  i::Handle<i::ObjectTemplateInfo> result(i::ObjectTemplateInfo::cast(
        Utils::OpenHandle(this)->instance_template()));
  return Utils::ToLocal(result);
}


void FunctionTemplate::SetClassName(Handle<String> name) {
  if (IsDeadCheck("v8::FunctionTemplate::SetClassName()")) return;
  Utils::OpenHandle(this)->set_class_name(*Utils::OpenHandle(*name));
}


void FunctionTemplate::SetHiddenPrototype(bool value) {
  if (IsDeadCheck("v8::FunctionTemplate::SetHiddenPrototype()")) return;
  Utils::OpenHandle(this)->set_hidden_prototype(value);
}


void FunctionTemplate::SetNamedInstancePropertyHandler(
      NamedPropertyGetter getter,
      NamedPropertySetter setter,
      NamedPropertyQuery query,
      NamedPropertyDeleter remover,
      NamedPropertyEnumerator enumerator,
      Handle<Value> data) {
  if (IsDeadCheck("v8::FunctionTemplate::SetNamedInstancePropertyHandler()")) {
    return;
  }
  HandleScope scope;
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::INTERCEPTOR_INFO_TYPE);
  i::Handle<i::InterceptorInfo> obj =
      i::Handle<i::InterceptorInfo>::cast(struct_obj);
  if (getter != 0) obj->set_getter(*FromCData(getter));
  if (setter != 0) obj->set_setter(*FromCData(setter));
  if (query != 0) obj->set_query(*FromCData(query));
  if (remover != 0) obj->set_deleter(*FromCData(remover));
  if (enumerator != 0) obj->set_enumerator(*FromCData(enumerator));
  if (data.IsEmpty()) data = v8::Undefined();
  obj->set_data(*Utils::OpenHandle(*data));
  Utils::OpenHandle(this)->set_named_property_handler(*obj);
}


void FunctionTemplate::SetIndexedInstancePropertyHandler(
      IndexedPropertyGetter getter,
      IndexedPropertySetter setter,
      IndexedPropertyQuery query,
      IndexedPropertyDeleter remover,
      IndexedPropertyEnumerator enumerator,
      Handle<Value> data) {
  if (IsDeadCheck(
        "v8::FunctionTemplate::SetIndexedInstancePropertyHandler()")) {
    return;
  }
  HandleScope scope;
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::INTERCEPTOR_INFO_TYPE);
  i::Handle<i::InterceptorInfo> obj =
      i::Handle<i::InterceptorInfo>::cast(struct_obj);
  if (getter != 0) obj->set_getter(*FromCData(getter));
  if (setter != 0) obj->set_setter(*FromCData(setter));
  if (query != 0) obj->set_query(*FromCData(query));
  if (remover != 0) obj->set_deleter(*FromCData(remover));
  if (enumerator != 0) obj->set_enumerator(*FromCData(enumerator));
  if (data.IsEmpty()) data = v8::Undefined();
  obj->set_data(*Utils::OpenHandle(*data));
  Utils::OpenHandle(this)->set_indexed_property_handler(*obj);
}


void FunctionTemplate::SetInstanceCallAsFunctionHandler(
      InvocationCallback callback,
      Handle<Value> data) {
  if (IsDeadCheck("v8::FunctionTemplate::SetInstanceCallAsFunctionHandler()")) {
    return;
  }
  HandleScope scope;
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::CALL_HANDLER_INFO_TYPE);
  i::Handle<i::CallHandlerInfo> obj =
      i::Handle<i::CallHandlerInfo>::cast(struct_obj);
  obj->set_callback(*FromCData(callback));
  if (data.IsEmpty()) data = v8::Undefined();
  obj->set_data(*Utils::OpenHandle(*data));
  Utils::OpenHandle(this)->set_instance_call_handler(*obj);
}


// --- O b j e c t T e m p l a t e ---


Local<ObjectTemplate> ObjectTemplate::New() {
  return New(Local<FunctionTemplate>());
}


Local<ObjectTemplate> ObjectTemplate::New(
      v8::Handle<FunctionTemplate> constructor) {
  if (IsDeadCheck("v8::ObjectTemplate::New()")) return Local<ObjectTemplate>();
  EnsureInitialized("v8::ObjectTemplate::New()");
  LOG_API("ObjectTemplate::New");
  i::Handle<i::Struct> struct_obj =
      i::Factory::NewStruct(i::OBJECT_TEMPLATE_INFO_TYPE);
  i::Handle<i::ObjectTemplateInfo> obj =
      i::Handle<i::ObjectTemplateInfo>::cast(struct_obj);
  InitializeTemplate(obj, Consts::OBJECT_TEMPLATE);
  if (!constructor.IsEmpty())
    obj->set_constructor(*Utils::OpenHandle(*constructor));
  obj->set_internal_field_count(i::Smi::FromInt(0));
  return Utils::ToLocal(obj);
}


// Ensure that the object template has a constructor.  If no
// constructor is available we create one.
static void EnsureConstructor(ObjectTemplate* object_template) {
  if (Utils::OpenHandle(object_template)->constructor()->IsUndefined()) {
    Local<FunctionTemplate> templ = FunctionTemplate::New();
    i::Handle<i::FunctionTemplateInfo> constructor = Utils::OpenHandle(*templ);
    constructor->set_instance_template(*Utils::OpenHandle(object_template));
    Utils::OpenHandle(object_template)->set_constructor(*constructor);
  }
}


void ObjectTemplate::SetAccessor(v8::Handle<String> name,
                                 AccessorGetter getter,
                                 AccessorSetter setter,
                                 v8::Handle<Value> data,
                                 AccessControl settings,
                                 PropertyAttribute attribute) {
  if (IsDeadCheck("v8::ObjectTemplate::SetAccessor()")) return;
  HandleScope scope;
  EnsureConstructor(this);
  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  Utils::ToLocal(cons)->AddInstancePropertyAccessor(name,
                                                    getter,
                                                    setter,
                                                    data,
                                                    settings,
                                                    attribute);
}


void ObjectTemplate::SetNamedPropertyHandler(NamedPropertyGetter getter,
                                             NamedPropertySetter setter,
                                             NamedPropertyQuery query,
                                             NamedPropertyDeleter remover,
                                             NamedPropertyEnumerator enumerator,
                                             Handle<Value> data) {
  if (IsDeadCheck("v8::ObjectTemplate::SetNamedPropertyHandler()")) return;
  HandleScope scope;
  EnsureConstructor(this);
  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  Utils::ToLocal(cons)->SetNamedInstancePropertyHandler(getter,
                                                        setter,
                                                        query,
                                                        remover,
                                                        enumerator,
                                                        data);
}


void ObjectTemplate::MarkAsUndetectable() {
  if (IsDeadCheck("v8::ObjectTemplate::MarkAsUndetectable()")) return;
  HandleScope scope;
  EnsureConstructor(this);
  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  cons->set_undetectable(true);
}


void ObjectTemplate::SetAccessCheckCallbacks(
      NamedSecurityCallback named_callback,
      IndexedSecurityCallback indexed_callback,
      Handle<Value> data) {
  if (IsDeadCheck("v8::ObjectTemplate::SetAccessCheckCallbacks()")) return;
  HandleScope scope;
  EnsureConstructor(this);

  i::Handle<i::Struct> struct_info =
      i::Factory::NewStruct(i::ACCESS_CHECK_INFO_TYPE);
  i::Handle<i::AccessCheckInfo> info =
      i::Handle<i::AccessCheckInfo>::cast(struct_info);
  info->set_named_callback(*FromCData(named_callback));
  info->set_indexed_callback(*FromCData(indexed_callback));
  if (data.IsEmpty()) data = v8::Undefined();
  info->set_data(*Utils::OpenHandle(*data));

  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  cons->set_needs_access_check(true);
  cons->set_access_check_info(*info);
}


void ObjectTemplate::SetIndexedPropertyHandler(
      IndexedPropertyGetter getter,
      IndexedPropertySetter setter,
      IndexedPropertyQuery query,
      IndexedPropertyDeleter remover,
      IndexedPropertyEnumerator enumerator,
      Handle<Value> data) {
  if (IsDeadCheck("v8::ObjectTemplate::SetIndexedPropertyHandler()")) return;
  HandleScope scope;
  EnsureConstructor(this);
  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  Utils::ToLocal(cons)->SetIndexedInstancePropertyHandler(getter,
                                                          setter,
                                                          query,
                                                          remover,
                                                          enumerator,
                                                          data);
}


void ObjectTemplate::SetCallAsFunctionHandler(InvocationCallback callback,
                                              Handle<Value> data) {
  if (IsDeadCheck("v8::ObjectTemplate::SetCallAsFunctionHandler()")) return;
  HandleScope scope;
  EnsureConstructor(this);
  i::FunctionTemplateInfo* constructor =
      i::FunctionTemplateInfo::cast(Utils::OpenHandle(this)->constructor());
  i::Handle<i::FunctionTemplateInfo> cons(constructor);
  Utils::ToLocal(cons)->SetInstanceCallAsFunctionHandler(callback, data);
}


int ObjectTemplate::InternalFieldCount() {
  if (IsDeadCheck("v8::ObjectTemplate::InternalFieldCount()")) {
    return 0;
  }
  return i::Smi::cast(Utils::OpenHandle(this)->internal_field_count())->value();
}


void ObjectTemplate::SetInternalFieldCount(int value) {
  if (IsDeadCheck("v8::ObjectTemplate::SetInternalFieldCount()")) return;
  if (!ApiCheck(i::Smi::IsValid(value),
                "v8::ObjectTemplate::SetInternalFieldCount()",
                "Invalid internal field count")) {
    return;
  }
  Utils::OpenHandle(this)->set_internal_field_count(i::Smi::FromInt(value));
}


// --- S c r i p t D a t a ---


ScriptData* ScriptData::PreCompile(const char* input, int length) {
  unibrow::Utf8InputBuffer<> buf(input, length);
  return i::PreParse(&buf, NULL);
}


ScriptData* ScriptData::New(unsigned* data, int length) {
  return new i::ScriptDataImpl(i::Vector<unsigned>(data, length));
}


// --- S c r i p t ---


Local<Script> Script::Compile(v8::Handle<String> source,
                              v8::ScriptOrigin* origin,
                              v8::ScriptData* script_data) {
  ON_BAILOUT("v8::Script::Compile()", return Local<Script>());
  LOG_API("Script::Compile");
  i::Handle<i::String> str = Utils::OpenHandle(*source);
  i::Handle<i::String> name_obj;
  int line_offset = 0;
  int column_offset = 0;
  if (origin != NULL) {
    if (!origin->ResourceName().IsEmpty()) {
      name_obj = Utils::OpenHandle(*origin->ResourceName());
    }
    if (!origin->ResourceLineOffset().IsEmpty()) {
      line_offset = static_cast<int>(origin->ResourceLineOffset()->Value());
    }
    if (!origin->ResourceColumnOffset().IsEmpty()) {
      column_offset = static_cast<int>(origin->ResourceColumnOffset()->Value());
    }
  }
  EXCEPTION_PREAMBLE();
  i::ScriptDataImpl* pre_data = static_cast<i::ScriptDataImpl*>(script_data);
  // We assert that the pre-data is sane, even though we can actually
  // handle it if it turns out not to be in release mode.
  ASSERT(pre_data == NULL || pre_data->SanityCheck());
  // If the pre-data isn't sane we simply ignore it
  if (pre_data != NULL && !pre_data->SanityCheck())
    pre_data = NULL;
  i::Handle<i::JSFunction> boilerplate = i::Compiler::Compile(str,
                                                              name_obj,
                                                              line_offset,
                                                              column_offset,
                                                              NULL,
                                                              pre_data);
  has_pending_exception = boilerplate.is_null();
  EXCEPTION_BAILOUT_CHECK(Local<Script>());
  i::Handle<i::JSFunction> result =
      i::Factory::NewFunctionFromBoilerplate(boilerplate,
                                             i::Top::global_context());
  return Local<Script>(ToApi<Script>(result));
}


Local<Value> Script::Run() {
  ON_BAILOUT("v8::Script::Run()", return Local<Value>());
  LOG_API("Script::Run");
  i::Object* raw_result = NULL;
  {
    HandleScope scope;
    i::Handle<i::JSFunction> fun = Utils::OpenHandle(this);
    EXCEPTION_PREAMBLE();
    i::Handle<i::Object> global(i::Top::context()->global());
    i::Handle<i::Object> result =
        i::Execution::Call(fun, global, 0, NULL, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Value>());
    raw_result = *result;
  }
  i::Handle<i::Object> result(raw_result);
  return Utils::ToLocal(result);
}


// --- E x c e p t i o n s ---


v8::TryCatch::TryCatch()
    : next_(i::Top::try_catch_handler()),
      exception_(i::Heap::the_hole_value()),
      is_verbose_(false) {
  i::Top::RegisterTryCatchHandler(this);
}


v8::TryCatch::~TryCatch() {
  i::Top::UnregisterTryCatchHandler(this);
}


bool v8::TryCatch::HasCaught() {
  return !reinterpret_cast<i::Object*>(exception_)->IsTheHole();
}


v8::Local<Value> v8::TryCatch::Exception() {
  if (HasCaught()) {
    // Check for out of memory exception.
    i::Object* exception = reinterpret_cast<i::Object*>(exception_);
    return v8::Utils::ToLocal(i::Handle<i::Object>(exception));
  } else {
    return v8::Local<Value>();
  }
}


void v8::TryCatch::Reset() {
  exception_ = i::Heap::the_hole_value();
}


void v8::TryCatch::SetVerbose(bool value) {
  is_verbose_ = value;
}


// --- M e s s a g e ---


Local<String> Message::Get() {
  ON_BAILOUT("v8::Message::Get()", return Local<String>());
  HandleScope scope;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::String> raw_result = i::MessageHandler::GetMessage(obj);
  Local<String> result = Utils::ToLocal(raw_result);
  return scope.Close(result);
}


v8::Handle<String> Message::GetScriptResourceName() {
  if (IsDeadCheck("v8::Message::GetScriptResourceName()")) {
    return Local<String>();
  }
  HandleScope scope;
  i::Handle<i::JSObject> obj =
      i::Handle<i::JSObject>::cast(Utils::OpenHandle(this));
  // Return this.script.name.
  i::Handle<i::JSValue> script =
      i::Handle<i::JSValue>::cast(GetProperty(obj, "script"));
  i::Handle<i::Object> resource_name(i::Script::cast(script->value())->name());
  if (!resource_name->IsString()) {
    return Local<String>();
  }
  Local<String> result =
      Utils::ToLocal(i::Handle<i::String>::cast(resource_name));
  return scope.Close(result);
}


// TODO(1240903): Remove this when no longer used in WebKit V8 bindings.
Handle<Value> Message::GetSourceData() {
  Handle<String> data = GetScriptResourceName();
  if (data.IsEmpty()) return v8::Undefined();
  return data;
}

static i::Handle<i::Object> CallV8HeapFunction(const char* name,
                                               i::Handle<i::Object> recv,
                                               int argc,
                                               i::Object** argv[],
                                               bool* has_pending_exception) {
  i::Handle<i::String> fmt_str = i::Factory::LookupAsciiSymbol(name);
  i::Object* object_fun = i::Top::builtins()->GetProperty(*fmt_str);
  i::Handle<i::JSFunction> fun =
      i::Handle<i::JSFunction>(i::JSFunction::cast(object_fun));
  i::Handle<i::Object> value =
      i::Execution::Call(fun, recv, argc, argv, has_pending_exception);
  return value;
}


static i::Handle<i::Object> CallV8HeapFunction(const char* name,
                                               i::Handle<i::Object> data,
                                               bool* has_pending_exception) {
  i::Object** argv[1] = { data.location() };
  return CallV8HeapFunction(name,
                            i::Top::builtins(),
                            1,
                            argv,
                            has_pending_exception);
}


int Message::GetLineNumber() {
  ON_BAILOUT("v8::Message::GetLineNumber()", return -1);
  HandleScope scope;
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> result = CallV8HeapFunction("GetLineNumber",
                                                   Utils::OpenHandle(this),
                                                   &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(0);
  return static_cast<int>(result->Number());
}


Local<Value> Message::GetSourceLine() {
  ON_BAILOUT("v8::Message::GetSourceLine()", return Local<Value>());
  HandleScope scope;
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> result = CallV8HeapFunction("GetSourceLine",
                                                   Utils::OpenHandle(this),
                                                   &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<v8::Value>());
  return scope.Close(Utils::ToLocal(result));
}


char* Message::GetUnderline(char* source_line, char underline_char) {
  if (IsDeadCheck("v8::Message::GetUnderline()")) return 0;
  HandleScope scope;

  i::Handle<i::JSObject> data_obj = Utils::OpenHandle(this);
  int start_pos = static_cast<int>(GetProperty(data_obj, "startPos")->Number());
  int end_pos = static_cast<int>(GetProperty(data_obj, "endPos")->Number());
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> start_col_obj = CallV8HeapFunction(
      "GetPositionInLine",
      data_obj,
      &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(0);
  int start_col = static_cast<int>(start_col_obj->Number());
  int end_col = start_col + (end_pos - start_pos);

  // Any tabs before or between the selected columns have to be
  // expanded into spaces.  We assume that a tab character advances
  // the cursor up until the next 8-character boundary and at least
  // one character.
  int real_start_col = 0;
  for (int i = 0; i < start_col; i++) {
    real_start_col++;
    if (source_line[i] == '\t') {
      real_start_col++;
      while (real_start_col % 8 != 0)
        real_start_col++;
    }
  }
  int real_end_col = real_start_col;
  for (int i = start_col; i < end_col; i++) {
    real_end_col++;
    if (source_line[i] == '\t') {
      while (real_end_col % 8 != 0)
        real_end_col++;
    }
  }
  char* result = i::NewArray<char>(real_end_col + 1);
  for (int i = 0; i < real_start_col; i++)
    result[i] = ' ';
  for (int i = real_start_col; i < real_end_col; i++)
    result[i] = underline_char;
  result[real_end_col] = '\0';
  return result;
}


void Message::PrintCurrentStackTrace(FILE* out) {
  if (IsDeadCheck("v8::Message::PrintCurrentStackTrace()")) return;
  i::Top::PrintCurrentStackTrace(out);
}


// --- D a t a ---

bool Value::IsUndefined() {
  if (IsDeadCheck("v8::Value::IsUndefined()")) return false;
  return Utils::OpenHandle(this)->IsUndefined();
}


bool Value::IsNull() {
  if (IsDeadCheck("v8::Value::IsNull()")) return false;
  return Utils::OpenHandle(this)->IsNull();
}


bool Value::IsTrue() {
  if (IsDeadCheck("v8::Value::IsTrue()")) return false;
  return Utils::OpenHandle(this)->IsTrue();
}


bool Value::IsFalse() {
  if (IsDeadCheck("v8::Value::IsFalse()")) return false;
  return Utils::OpenHandle(this)->IsFalse();
}


bool Value::IsFunction() {
  if (IsDeadCheck("v8::Value::IsFunction()")) return false;
  return Utils::OpenHandle(this)->IsJSFunction();
}


bool Value::IsString() {
  if (IsDeadCheck("v8::Value::IsString()")) return false;
  return Utils::OpenHandle(this)->IsString();
}


bool Value::IsArray() {
  if (IsDeadCheck("v8::Value::IsArray()")) return false;
  return Utils::OpenHandle(this)->IsJSArray();
}


bool Value::IsObject() {
  if (IsDeadCheck("v8::Value::IsObject()")) return false;
  return Utils::OpenHandle(this)->IsJSObject();
}


bool Value::IsNumber() {
  if (IsDeadCheck("v8::Value::IsNumber()")) return false;
  return Utils::OpenHandle(this)->IsNumber();
}


bool Value::IsBoolean() {
  if (IsDeadCheck("v8::Value::IsBoolean()")) return false;
  return Utils::OpenHandle(this)->IsBoolean();
}


bool Value::IsExternal() {
  if (IsDeadCheck("v8::Value::IsExternal()")) return false;
  return Utils::OpenHandle(this)->IsProxy();
}


bool Value::IsInt32() {
  if (IsDeadCheck("v8::Value::IsInt32()")) return false;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return true;
  if (obj->IsNumber()) {
    double value = obj->Number();
    return i::FastI2D(i::FastD2I(value)) == value;
  }
  return false;
}


Local<String> Value::ToString() {
  if (IsDeadCheck("v8::Value::ToString()")) return Local<String>();
  LOG_API("ToString");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> str;
  if (obj->IsString()) {
    str = obj;
  } else {
    EXCEPTION_PREAMBLE();
    str = i::Execution::ToString(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<String>());
  }
  return Local<String>(ToApi<String>(str));
}


Local<String> Value::ToDetailString() {
  if (IsDeadCheck("v8::Value::ToDetailString()")) return Local<String>();
  LOG_API("ToDetailString");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> str;
  if (obj->IsString()) {
    str = obj;
  } else {
    EXCEPTION_PREAMBLE();
    str = i::Execution::ToDetailString(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<String>());
  }
  return Local<String>(ToApi<String>(str));
}


Local<v8::Object> Value::ToObject() {
  if (IsDeadCheck("v8::Value::ToObject()")) return Local<v8::Object>();
  LOG_API("ToObject");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> val;
  if (obj->IsJSObject()) {
    val = obj;
  } else {
    EXCEPTION_PREAMBLE();
    val = i::Execution::ToObject(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<v8::Object>());
  }
  return Local<v8::Object>(ToApi<Object>(val));
}


Local<Boolean> Value::ToBoolean() {
  if (IsDeadCheck("v8::Value::ToBoolean()")) return Local<Boolean>();
  LOG_API("ToBoolean");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> val =
      obj->IsBoolean() ? obj : i::Execution::ToBoolean(obj);
  return Local<Boolean>(ToApi<Boolean>(val));
}


Local<Number> Value::ToNumber() {
  if (IsDeadCheck("v8::Value::ToNumber()")) return Local<Number>();
  LOG_API("ToNumber");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsNumber()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToNumber(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Number>());
  }
  return Local<Number>(ToApi<Number>(num));
}


Local<Integer> Value::ToInteger() {
  if (IsDeadCheck("v8::Value::ToInteger()")) return Local<Integer>();
  LOG_API("ToInteger");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsSmi()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToInteger(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Integer>());
  }
  return Local<Integer>(ToApi<Integer>(num));
}


External* External::Cast(v8::Value* that) {
  if (IsDeadCheck("v8::External::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsProxy(),
           "v8::External::Cast()",
           "Could not convert to external");
  return static_cast<External*>(that);
}


v8::Object* v8::Object::Cast(Value* that) {
  if (IsDeadCheck("v8::Object::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsJSObject(),
           "v8::Object::Cast()",
           "Could not convert to object");
  return static_cast<v8::Object*>(that);
}


v8::Function* v8::Function::Cast(Value* that) {
  if (IsDeadCheck("v8::Function::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsJSFunction(),
           "v8::Function::Cast()",
           "Could not convert to function");
  return static_cast<v8::Function*>(that);
}


v8::String* v8::String::Cast(v8::Value* that) {
  if (IsDeadCheck("v8::String::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsString(),
           "v8::String::Cast()",
           "Could not convert to string");
  return static_cast<v8::String*>(that);
}


v8::Number* v8::Number::Cast(v8::Value* that) {
  if (IsDeadCheck("v8::Number::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsNumber(),
           "v8::Number::Cast()",
           "Could not convert to number");
  return static_cast<v8::Number*>(that);
}


v8::Integer* v8::Integer::Cast(v8::Value* that) {
  if (IsDeadCheck("v8::Integer::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsNumber(),
           "v8::Integer::Cast()",
           "Could not convert to number");
  return static_cast<v8::Integer*>(that);
}


v8::Array* v8::Array::Cast(Value* that) {
  if (IsDeadCheck("v8::Array::Cast()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  ApiCheck(obj->IsJSArray(),
           "v8::Array::Cast()",
           "Could not convert to array");
  return static_cast<v8::Array*>(that);
}


bool Value::BooleanValue() {
  if (IsDeadCheck("v8::Value::BooleanValue()")) return false;
  LOG_API("BooleanValue");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> value =
      obj->IsBoolean() ? obj : i::Execution::ToBoolean(obj);
  return value->IsTrue();
}


double Value::NumberValue() {
  if (IsDeadCheck("v8::Value::NumberValue()")) return i::OS::nan_value();
  LOG_API("NumberValue");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsNumber()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToNumber(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(i::OS::nan_value());
  }
  return num->Number();
}


int64_t Value::IntegerValue() {
  if (IsDeadCheck("v8::Value::IntegerValue()")) return 0;
  LOG_API("IntegerValue");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsNumber()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToInteger(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(0);
  }
  if (num->IsSmi()) {
    return i::Smi::cast(*num)->value();
  } else {
    return static_cast<int64_t>(num->Number());
  }
}


Local<Int32> Value::ToInt32() {
  if (IsDeadCheck("v8::Value::ToInt32()")) return Local<Int32>();
  LOG_API("ToInt32");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsSmi()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToInt32(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Int32>());
  }
  return Local<Int32>(ToApi<Int32>(num));
}


Local<Uint32> Value::ToUint32() {
  if (IsDeadCheck("v8::Value::ToUint32()")) return Local<Uint32>();
  LOG_API("ToUInt32");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsSmi()) {
    num = obj;
  } else {
    EXCEPTION_PREAMBLE();
    num = i::Execution::ToUint32(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Uint32>());
  }
  return Local<Uint32>(ToApi<Uint32>(num));
}


Local<Uint32> Value::ToArrayIndex() {
  if (IsDeadCheck("v8::Value::ToArrayIndex()")) return Local<Uint32>();
  LOG_API("ToArrayIndex");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    if (i::Smi::cast(*obj)->value() >= 0) return Utils::Uint32ToLocal(obj);
    return Local<Uint32>();
  }
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> string_obj =
      i::Execution::ToString(obj, &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<Uint32>());
  i::Handle<i::String> str = i::Handle<i::String>::cast(string_obj);
  uint32_t index;
  if (str->AsArrayIndex(&index)) {
    i::Handle<i::Object> value;
    if (index <= static_cast<uint32_t>(i::Smi::kMaxValue)) {
      value = i::Handle<i::Object>(i::Smi::FromInt(index));
    } else {
      value = i::Factory::NewNumber(index);
    }
    return Utils::Uint32ToLocal(value);
  }
  return Local<Uint32>();
}


int32_t Value::Int32Value() {
  if (IsDeadCheck("v8::Value::Int32Value()")) return 0;
  LOG_API("Int32Value");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    LOG_API("Int32Value (slow)");
    EXCEPTION_PREAMBLE();
    i::Handle<i::Object> num =
        i::Execution::ToInt32(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(0);
    if (num->IsSmi()) {
      return i::Smi::cast(*num)->value();
    } else {
      return static_cast<int32_t>(num->Number());
    }
  }
}


bool Value::Equals(Handle<Value> that) {
  if (IsDeadCheck("v8::Value::Equals()")
      || EmptyCheck("v8::Value::Equals()", this)
      || EmptyCheck("v8::Value::Equals()", that))
    return false;
  LOG_API("Equals");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> other = Utils::OpenHandle(*that);
  i::Object** args[1] = { other.location() };
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> result =
      CallV8HeapFunction("EQUALS", obj, 1, args, &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(false);
  return *result == i::Smi::FromInt(i::EQUAL);
}


bool Value::StrictEquals(Handle<Value> that) {
  if (IsDeadCheck("v8::Value::StrictEquals()")
      || EmptyCheck("v8::Value::StrictEquals()", this)
      || EmptyCheck("v8::Value::StrictEquals()", that))
    return false;
  LOG_API("StrictEquals");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> other = Utils::OpenHandle(*that);
  // Must check HeapNumber first, since NaN !== NaN.
  if (obj->IsHeapNumber()) {
    if (!other->IsNumber()) return false;
    double x = obj->Number();
    double y = other->Number();
    // Must check explicitly for NaN:s on Windows, but -0 works fine.
    return x == y && !isnan(x) && !isnan(y);
  } else if (*obj == *other) {  // Also covers Booleans.
    return true;
  } else if (obj->IsSmi()) {
    return other->IsNumber() && obj->Number() == other->Number();
  } else if (obj->IsString()) {
    return other->IsString() &&
      i::String::cast(*obj)->Equals(i::String::cast(*other));
  } else if (obj->IsUndefined() || obj->IsUndetectableObject()) {
    return other->IsUndefined() || other->IsUndetectableObject();
  } else {
    return false;
  }
}


uint32_t Value::Uint32Value() {
  if (IsDeadCheck("v8::Value::Uint32Value()")) return 0;
  LOG_API("Uint32Value");
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    EXCEPTION_PREAMBLE();
    i::Handle<i::Object> num =
        i::Execution::ToUint32(obj, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(0);
    if (num->IsSmi()) {
      return i::Smi::cast(*num)->value();
    } else {
      return static_cast<uint32_t>(num->Number());
    }
  }
}


bool v8::Object::Set(v8::Handle<Value> key, v8::Handle<Value> value,
                             v8::PropertyAttribute attribs) {
  ON_BAILOUT("v8::Object::Set()", return false);
  i::Handle<i::Object> self = Utils::OpenHandle(this);
  i::Handle<i::Object> key_obj = Utils::OpenHandle(*key);
  i::Handle<i::Object> value_obj = Utils::OpenHandle(*value);
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> obj = i::SetProperty(
      self,
      key_obj,
      value_obj,
      static_cast<PropertyAttributes>(attribs));
  has_pending_exception = obj.is_null();
  EXCEPTION_BAILOUT_CHECK(false);
  return true;
}


Local<Value> v8::Object::Get(v8::Handle<Value> key) {
  ON_BAILOUT("v8::Object::Get()", return Local<v8::Value>());
  i::Handle<i::Object> self = Utils::OpenHandle(this);
  i::Handle<i::Object> key_obj = Utils::OpenHandle(*key);
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> result = i::GetProperty(self, key_obj);
  has_pending_exception = result.is_null();
  EXCEPTION_BAILOUT_CHECK(Local<Value>());
  return Utils::ToLocal(result);
}


Local<Value> v8::Object::GetPrototype() {
  ON_BAILOUT("v8::Object::GetPrototype()", return Local<v8::Value>());
  i::Handle<i::Object> self = Utils::OpenHandle(this);
  i::Handle<i::Object> result = i::GetPrototype(self);
  return Utils::ToLocal(result);
}


Local<String> v8::Object::ObjectProtoToString() {
  ON_BAILOUT("v8::Object::ObjectProtoToString()", return Local<v8::String>());
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);

  i::Handle<i::Object> name(self->class_name());

  // Native implementation of Object.prototype.toString (v8natives.js):
  //   var c = %ClassOf(this);
  //   if (c === 'Arguments') c  = 'Object';
  //   return "[object " + c + "]";

  if (!name->IsString()) {
    return v8::String::New("[object ]");

  } else {
    i::Handle<i::String> class_name = i::Handle<i::String>::cast(name);
    if (class_name->IsEqualTo(i::CStrVector("Arguments"))) {
      return v8::String::New("[object Object]");

    } else {
      const char* prefix = "[object ";
      Local<String> str = Utils::ToLocal(class_name);
      const char* postfix = "]";

      size_t prefix_len = strlen(prefix);
      size_t str_len = str->Length();
      size_t postfix_len = strlen(postfix);

      size_t buf_len = prefix_len + str_len + postfix_len;
      char* buf = i::NewArray<char>(buf_len);

      // Write prefix.
      char* ptr = buf;
      memcpy(ptr, prefix, prefix_len * v8::internal::kCharSize);
      ptr += prefix_len;

      // Write real content.
      str->WriteAscii(ptr, 0, str_len);
      ptr += str_len;

      // Write postfix.
      memcpy(ptr, postfix, postfix_len * v8::internal::kCharSize);

      // Copy the buffer into a heap-allocated string and return it.
      Local<String> result = v8::String::New(buf, buf_len);
      i::DeleteArray(buf);
      return result;
    }
  }
}


bool v8::Object::Delete(v8::Handle<String> key) {
  ON_BAILOUT("v8::Object::Delete()", return false);
  HandleScope scope;
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  return i::DeleteProperty(self, key_obj)->IsTrue();
}


bool v8::Object::Has(v8::Handle<String> key) {
  ON_BAILOUT("v8::Object::Has()", return false);
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  return self->HasProperty(*key_obj);
}


bool v8::Object::Delete(uint32_t index) {
  ON_BAILOUT("v8::Object::DeleteProperty()", return false);
  HandleScope scope;
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  return i::DeleteElement(self, index)->IsTrue();
}


bool v8::Object::Has(uint32_t index) {
  ON_BAILOUT("v8::Object::HasProperty()", return false);
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  return self->HasElement(index);
}


bool v8::Object::HasRealNamedProperty(Handle<String> key) {
  ON_BAILOUT("v8::Object::HasRealNamedProperty()", return false);
  return Utils::OpenHandle(this)->HasRealNamedProperty(
      *Utils::OpenHandle(*key));
}


bool v8::Object::HasRealIndexedProperty(uint32_t index) {
  ON_BAILOUT("v8::Object::HasRealIndexedProperty()", return false);
  return Utils::OpenHandle(this)->HasRealElementProperty(index);
}


bool v8::Object::HasRealNamedCallbackProperty(Handle<String> key) {
  ON_BAILOUT("v8::Object::HasRealNamedCallbackProperty()", return false);
  return Utils::OpenHandle(this)->HasRealNamedCallbackProperty(
      *Utils::OpenHandle(*key));
}


bool v8::Object::HasNamedLookupInterceptor() {
  ON_BAILOUT("v8::Object::HasNamedLookupInterceptor()", return false);
  return Utils::OpenHandle(this)->HasNamedInterceptor();
}


bool v8::Object::HasIndexedLookupInterceptor() {
  ON_BAILOUT("v8::Object::HasIndexedLookupInterceptor()", return false);
  return Utils::OpenHandle(this)->HasIndexedInterceptor();
}


Handle<Value> v8::Object::GetRealNamedPropertyInPrototypeChain(
      Handle<String> key) {
  ON_BAILOUT("v8::Object::GetRealNamedPropertyInPrototypeChain()",
             return Local<Value>());
  i::Handle<i::JSObject> self_obj = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  i::LookupResult lookup;
  self_obj->LookupRealNamedPropertyInPrototypes(*key_obj, &lookup);
  if (lookup.IsValid()) {
    PropertyAttributes attributes;
    i::Handle<i::Object> result(self_obj->GetProperty(*self_obj,
                                                      &lookup,
                                                      *key_obj,
                                                      &attributes));
    return Utils::ToLocal(result);
  }
  return Local<Value>();  // No real property was found in protoype chain.
}


Local<v8::Object> Function::NewInstance() {
  return NewInstance(0, NULL);
}


Local<v8::Object> Function::NewInstance(int argc,
                                        v8::Handle<v8::Value> argv[]) {
  ON_BAILOUT("v8::Function::NewInstance()", return Local<v8::Object>());
  LOG_API("Function::NewInstance");
  HandleScope scope;
  i::Handle<i::JSFunction> function = Utils::OpenHandle(this);
  STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
  i::Object*** args = reinterpret_cast<i::Object***>(argv);
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> returned =
      i::Execution::New(function, argc, args, &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<v8::Object>());
  return scope.Close(Utils::ToLocal(i::Handle<i::JSObject>::cast(returned)));
}


Local<v8::Value> Function::Call(v8::Handle<v8::Object> recv, int argc,
                                v8::Handle<v8::Value> argv[]) {
  ON_BAILOUT("v8::Function::Call()", return Local<v8::Value>());
  LOG_API("Function::Call");
  i::Object* raw_result = NULL;
  {
    HandleScope scope;
    i::Handle<i::JSFunction> fun = Utils::OpenHandle(this);
    i::Handle<i::Object> recv_obj = Utils::OpenHandle(*recv);
    STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
    i::Object*** args = reinterpret_cast<i::Object***>(argv);
    EXCEPTION_PREAMBLE();
    i::Handle<i::Object> returned =
        i::Execution::Call(fun, recv_obj, argc, args, &has_pending_exception);
    EXCEPTION_BAILOUT_CHECK(Local<Object>());
    raw_result = *returned;
  }
  i::Handle<i::Object> result(raw_result);
  return Utils::ToLocal(result);
}


void Function::SetName(v8::Handle<v8::String> name) {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  func->shared()->set_name(*Utils::OpenHandle(*name));
}


Handle<Value> Function::GetName() {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  return Utils::ToLocal(i::Handle<i::Object>(func->shared()->name()));
}


int String::Length() {
  if (IsDeadCheck("v8::String::Length()")) return 0;
  return Utils::OpenHandle(this)->length();
}


int String::WriteAscii(char* buffer, int start, int length) {
  if (IsDeadCheck("v8::String::WriteAscii()")) return 0;
  LOG_API("String::WriteAscii");
  ASSERT(start >= 0 && length >= -1);
  i::Handle<i::String> str = Utils::OpenHandle(this);
  // Flatten the string for efficiency.  This applies whether we are
  // using StringInputBuffer or Get(i) to access the characters.
  str->TryFlatten();
  int end = length;
  if ( (length == -1) || (length > str->length() - start) )
    end = str->length() - start;
  if (end < 0) return 0;
  write_input_buffer.Reset(start, *str);
  int i;
  for (i = 0; i < end; i++) {
    char c = static_cast<char>(write_input_buffer.GetNext());
    if (c == '\0') c = ' ';
    buffer[i] = c;
  }
  if (length == -1 || i < length)
    buffer[i] = '\0';
  return i;
}


int String::Write(uint16_t* buffer, int start, int length) {
  if (IsDeadCheck("v8::String::Write()")) return 0;
  LOG_API("String::Write");
  ASSERT(start >= 0 && length >= -1);
  i::Handle<i::String> str = Utils::OpenHandle(this);
  // Flatten the string for efficiency.  This applies whether we are
  // using StringInputBuffer or Get(i) to access the characters.
  str->TryFlatten();
  int end = length;
  if ( (length == -1) || (length > str->length() - start) )
    end = str->length() - start;
  if (end < 0) return 0;
  write_input_buffer.Reset(start, *str);
  int i;
  for (i = 0; i < end; i++)
    buffer[i] = write_input_buffer.GetNext();
  if (length == -1 || i < length)
    buffer[i] = '\0';
  return i;
}


bool v8::String::IsExternal() {
  EnsureInitialized("v8::String::IsExternal()");
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return str->IsExternalTwoByteString();
}


bool v8::String::IsExternalAscii() {
  EnsureInitialized("v8::String::IsExternalAscii()");
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return str->IsExternalAsciiString();
}


v8::String::ExternalStringResource* v8::String::GetExternalStringResource() {
  EnsureInitialized("v8::String::GetExternalStringResource()");
  i::Handle<i::String> str = Utils::OpenHandle(this);
  ASSERT(str->IsExternalTwoByteString());
  void* resource = i::Handle<i::ExternalTwoByteString>::cast(str)->resource();
  return reinterpret_cast<ExternalStringResource*>(resource);
}


v8::String::ExternalAsciiStringResource*
      v8::String::GetExternalAsciiStringResource() {
  EnsureInitialized("v8::String::GetExternalAsciiStringResource()");
  i::Handle<i::String> str = Utils::OpenHandle(this);
  ASSERT(str->IsExternalAsciiString());
  void* resource = i::Handle<i::ExternalAsciiString>::cast(str)->resource();
  return reinterpret_cast<ExternalAsciiStringResource*>(resource);
}


double Number::Value() {
  if (IsDeadCheck("v8::Number::Value()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return obj->Number();
}


bool Boolean::Value() {
  if (IsDeadCheck("v8::Boolean::Value()")) return false;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return obj->IsTrue();
}


int64_t Integer::Value() {
  if (IsDeadCheck("v8::Integer::Value()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    return static_cast<int64_t>(obj->Number());
  }
}


int32_t Int32::Value() {
  if (IsDeadCheck("v8::Int32::Value()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    return static_cast<int32_t>(obj->Number());
  }
}


void* External::Value() {
  if (IsDeadCheck("v8::External::Value()")) return 0;
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return reinterpret_cast<void*>(i::Proxy::cast(*obj)->proxy());
}


int v8::Object::InternalFieldCount() {
  if (IsDeadCheck("v8::Object::InternalFieldCount()")) return 0;
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  return obj->GetInternalFieldCount();
}


Local<Value> v8::Object::GetInternalField(int index) {
  if (IsDeadCheck("v8::Object::GetInternalField()")) return Local<Value>();
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  if (!ApiCheck(index < obj->GetInternalFieldCount(),
                "v8::Object::GetInternalField()",
                "Reading internal field out of bounds")) {
    return Local<Value>();
  }
  i::Handle<i::Object> value(obj->GetInternalField(index));
  return Utils::ToLocal(value);
}


void v8::Object::SetInternalField(int index, v8::Handle<Value> value) {
  if (IsDeadCheck("v8::Object::SetInternalField()")) return;
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  if (!ApiCheck(index < obj->GetInternalFieldCount(),
                "v8::Object::SetInternalField()",
                "Writing internal field out of bounds")) {
    return;
  }
  i::Handle<i::Object> val = Utils::OpenHandle(*value);
  obj->SetInternalField(index, *val);
}


// --- E n v i r o n m e n t ---

bool v8::V8::Initialize() {
  if (i::V8::HasBeenSetup()) return true;
  HandleScope scope;
  if (i::Snapshot::Initialize()) {
    i::Serializer::disable();
    return true;
  } else {
    return i::V8::Initialize(NULL);
  }
}


Persistent<Context> v8::Context::New(v8::ExtensionConfiguration* extensions,
                                     v8::Handle<ObjectTemplate> global_template,
                                     v8::Handle<Value> global_object) {
  EnsureInitialized("v8::Context::New()");
  LOG_API("Context::New");
  ON_BAILOUT("v8::Context::New()", return Persistent<Context>());
  // Make sure that the global_template has a constructor.
  if (!global_template.IsEmpty() &&
     Utils::OpenHandle(*global_template)->constructor()->IsUndefined()) {
    Local<FunctionTemplate> templ = FunctionTemplate::New();
    Utils::OpenHandle(*templ)->set_instance_template(
        *Utils::OpenHandle(*global_template));
    i::Handle<i::FunctionTemplateInfo> constructor = Utils::OpenHandle(*templ);
    Utils::OpenHandle(*global_template)->set_constructor(*constructor);
  }

  i::Handle<i::Context> env = i::Bootstrapper::CreateEnvironment(
      Utils::OpenHandle(*global_object),
      global_template, extensions);
  if (!ApiCheck(!env.is_null(),
                "v8::Context::New()",
                "Could not initialize environment"))
    return Persistent<Context>();
  return Persistent<Context>(Utils::ToLocal(env));
}


void v8::Context::SetSecurityToken(Handle<Value> token) {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Handle<i::Object> token_handle = Utils::OpenHandle(*token);
  // The global object of an environment is always a real global
  // object with security token and reference to the builtins object.
  i::JSGlobalObject::cast(env->global())->set_security_token(*token_handle);
}


Handle<Value> v8::Context::GetSecurityToken() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Object* security_token =
      i::JSGlobalObject::cast(env->global())->security_token();
  i::Handle<i::Object> token_handle(security_token);
  return Utils::ToLocal(token_handle);
}


bool Context::HasOutOfMemoryException() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  return env->has_out_of_memory();
}


bool Context::InContext() {
  return i::Top::context() != NULL;
}


bool Context::InSecurityContext() {
  return i::Top::security_context() != NULL;
}


v8::Local<v8::Context> Context::GetEntered() {
  if (IsDeadCheck("v8::Context::GetEntered()")) return Local<Context>();
  i::Handle<i::Object> last = thread_local.LastEnteredContext();
  if (last.is_null()) return Local<Context>();
  i::Handle<i::Context> context = i::Handle<i::Context>::cast(last);
  return Utils::ToLocal(context);
}


v8::Local<v8::Context> Context::GetCurrent() {
  if (IsDeadCheck("v8::Context::GetCurrent()")) return Local<Context>();
  i::Handle<i::Context> context(i::Top::global_context());
  return Utils::ToLocal(context);
}


v8::Local<v8::Context> Context::GetCurrentSecurityContext() {
  if (IsDeadCheck("v8::Context::GetCurrentSecurityContext()")) {
    return Local<Context>();
  }
  i::Handle<i::Context> context(i::Top::security_context());
  return Utils::ToLocal(context);
}


v8::Local<v8::Object> Context::Global() {
  if (IsDeadCheck("v8::Context::Global()")) return Local<v8::Object>();
  i::Object** ctx = reinterpret_cast<i::Object**>(this);
  i::Handle<i::Context> context =
      i::Handle<i::Context>::cast(i::Handle<i::Object>(ctx));
  i::Handle<i::JSObject> global(context->global());
  return Utils::ToLocal(global);
}


Local<v8::Object> ObjectTemplate::NewInstance() {
  ON_BAILOUT("v8::ObjectTemplate::NewInstance()", return Local<v8::Object>());
  LOG_API("ObjectTemplate::NewInstance");
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> obj =
      i::Execution::InstantiateObject(Utils::OpenHandle(this),
                                      &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<v8::Object>());
  return Utils::ToLocal(i::Handle<i::JSObject>::cast(obj));
}


Local<v8::Function> FunctionTemplate::GetFunction() {
  ON_BAILOUT("v8::FunctionTemplate::GetFunction()",
             return Local<v8::Function>());
  LOG_API("FunctionTemplate::GetFunction");
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> obj =
      i::Execution::InstantiateFunction(Utils::OpenHandle(this),
                                        &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<v8::Function>());
  return Utils::ToLocal(i::Handle<i::JSFunction>::cast(obj));
}


bool FunctionTemplate::HasInstance(v8::Handle<v8::Value> value) {
  ON_BAILOUT("v8::FunctionTemplate::HasInstanceOf()", return false);
  i::Object* obj = *Utils::OpenHandle(*value);
  return obj->IsInstanceOf(*Utils::OpenHandle(this));
}


Local<External> v8::External::New(void* data) {
  STATIC_ASSERT(sizeof(data) == sizeof(i::Address));
  LOG_API("External::New");
  EnsureInitialized("v8::External::New()");
  i::Handle<i::Proxy> obj = i::Factory::NewProxy(static_cast<i::Address>(data));
  return Utils::ToLocal(obj);
}


Local<String> v8::String::New(const char* data, int length) {
  EnsureInitialized("v8::String::New()");
  LOG_API("String::New(char)");
  if (length == -1) length = strlen(data);
  i::Handle<i::String> result =
      i::Factory::NewStringFromUtf8(i::Vector<const char>(data, length));
  return Utils::ToLocal(result);
}


Local<String> v8::String::NewUndetectable(const char* data, int length) {
  EnsureInitialized("v8::String::NewUndetectable()");
  LOG_API("String::NewUndetectable(char)");
  if (length == -1) length = strlen(data);
  i::Handle<i::String> result =
      i::Factory::NewStringFromUtf8(i::Vector<const char>(data, length));
  result->MarkAsUndetectable();
  return Utils::ToLocal(result);
}


static int TwoByteStringLength(const uint16_t* data) {
  int length = 0;
  while (data[length] != '\0') length++;
  return length;
}


Local<String> v8::String::New(const uint16_t* data, int length) {
  EnsureInitialized("v8::String::New()");
  LOG_API("String::New(uint16_)");
  if (length == -1) length = TwoByteStringLength(data);
  i::Handle<i::String> result =
      i::Factory::NewStringFromTwoByte(i::Vector<const uint16_t>(data, length));
  return Utils::ToLocal(result);
}


Local<String> v8::String::NewUndetectable(const uint16_t* data, int length) {
  EnsureInitialized("v8::String::NewUndetectable()");
  LOG_API("String::NewUndetectable(uint16_)");
  if (length == -1) length = TwoByteStringLength(data);
  i::Handle<i::String> result =
      i::Factory::NewStringFromTwoByte(i::Vector<const uint16_t>(data, length));
  result->MarkAsUndetectable();
  return Utils::ToLocal(result);
}


i::Handle<i::String> NewExternalStringHandle(
      v8::String::ExternalStringResource* resource) {
  i::Handle<i::String> result =
      i::Factory::NewExternalStringFromTwoByte(resource);
  return result;
}


i::Handle<i::String> NewExternalAsciiStringHandle(
      v8::String::ExternalAsciiStringResource* resource) {
  i::Handle<i::String> result =
      i::Factory::NewExternalStringFromAscii(resource);
  return result;
}


static void DisposeExternalString(v8::Persistent<v8::Object> obj,
                                  void* parameter) {
  v8::String::ExternalStringResource* resource =
    reinterpret_cast<v8::String::ExternalStringResource*>(parameter);
  const size_t total_size = resource->length() * sizeof(*resource->data());
  i::Counters::total_external_string_memory.Decrement(total_size);
  delete resource;
  obj.Dispose();
}


static void DisposeExternalAsciiString(v8::Persistent<v8::Object> obj,
                                       void* parameter) {
  v8::String::ExternalAsciiStringResource* resource =
    reinterpret_cast<v8::String::ExternalAsciiStringResource*>(parameter);
  const size_t total_size = resource->length() * sizeof(*resource->data());
  i::Counters::total_external_string_memory.Decrement(total_size);
  delete resource;
  obj.Dispose();
}


Local<String> v8::String::NewExternal(
      v8::String::ExternalStringResource* resource) {
  EnsureInitialized("v8::String::NewExternal()");
  LOG_API("String::NewExternal");
  const size_t total_size = resource->length() * sizeof(*resource->data());
  i::Counters::total_external_string_memory.Increment(total_size);
  i::Handle<i::String> result = NewExternalStringHandle(resource);
  i::Handle<i::Object> handle = i::GlobalHandles::Create(*result);
  i::GlobalHandles::MakeWeak(handle.location(),
                             resource,
                             &DisposeExternalString);
  return Utils::ToLocal(result);
}


Local<String> v8::String::NewExternal(
      v8::String::ExternalAsciiStringResource* resource) {
  EnsureInitialized("v8::String::NewExternal()");
  LOG_API("String::NewExternal");
  const size_t total_size = resource->length() * sizeof(*resource->data());
  i::Counters::total_external_string_memory.Increment(total_size);
  i::Handle<i::String> result = NewExternalAsciiStringHandle(resource);
  i::Handle<i::Object> handle = i::GlobalHandles::Create(*result);
  i::GlobalHandles::MakeWeak(handle.location(),
                             resource,
                             &DisposeExternalAsciiString);
  return Utils::ToLocal(result);
}


Local<v8::Object> v8::Object::New() {
  EnsureInitialized("v8::Object::New()");
  LOG_API("Object::New");
  i::Handle<i::JSObject> obj =
      i::Factory::NewJSObject(i::Top::object_function());
  return Utils::ToLocal(obj);
}


Local<v8::Value> v8::Date::New(double time) {
  EnsureInitialized("v8::Date::New()");
  LOG_API("Date::New");
  EXCEPTION_PREAMBLE();
  i::Handle<i::Object> obj =
      i::Execution::NewDate(time, &has_pending_exception);
  EXCEPTION_BAILOUT_CHECK(Local<v8::Value>());
  return Utils::ToLocal(obj);
}


Local<v8::Array> v8::Array::New(int length) {
  EnsureInitialized("v8::Array::New()");
  LOG_API("Array::New");
  i::Handle<i::JSArray> obj = i::Factory::NewJSArray(length);
  return Utils::ToLocal(obj);
}


uint32_t v8::Array::Length() {
  if (IsDeadCheck("v8::Array::Length()")) return 0;
  i::Handle<i::JSArray> obj = Utils::OpenHandle(this);
  i::Object* length = obj->length();
  if (length->IsSmi()) {
    return i::Smi::cast(length)->value();
  } else {
    return static_cast<uint32_t>(length->Number());
  }
}


Local<String> v8::String::NewSymbol(const char* data, int length) {
  EnsureInitialized("v8::String::NewSymbol()");
  LOG_API("String::NewSymbol(char)");
  if (length == -1) length = strlen(data);
  i::Handle<i::String> result =
      i::Factory::LookupSymbol(i::Vector<const char>(data, length));
  return Utils::ToLocal(result);
}


Local<Number> v8::Number::New(double value) {
  EnsureInitialized("v8::Number::New()");
  i::Handle<i::Object> result = i::Factory::NewNumber(value);
  return Utils::NumberToLocal(result);
}


Local<Integer> v8::Integer::New(int32_t value) {
  EnsureInitialized("v8::Integer::New()");
  if (i::Smi::IsValid(value)) {
    return Utils::IntegerToLocal(i::Handle<i::Object>(i::Smi::FromInt(value)));
  }
  i::Handle<i::Object> result = i::Factory::NewNumber(value);
  return Utils::IntegerToLocal(result);
}


void V8::IgnoreOutOfMemoryException() {
  thread_local.SetIgnoreOutOfMemory(true);
}


bool V8::AddMessageListener(MessageCallback that, Handle<Value> data) {
  EnsureInitialized("v8::V8::AddMessageListener()");
  ON_BAILOUT("v8::V8::AddMessageListener()", return false);
  HandleScope scope;
  NeanderArray listeners(i::Factory::message_listeners());
  NeanderObject obj(2);
  obj.set(0, *i::Factory::NewProxy(FUNCTION_ADDR(that)));
  obj.set(1, data.IsEmpty() ?
             i::Heap::undefined_value() :
             *Utils::OpenHandle(*data));
  listeners.add(obj.value());
  return true;
}


void V8::RemoveMessageListeners(MessageCallback that) {
  EnsureInitialized("v8::V8::RemoveMessageListener()");
  ON_BAILOUT("v8::V8::RemoveMessageListeners()", return);
  HandleScope scope;
  NeanderArray listeners(i::Factory::message_listeners());
  for (int i = 0; i < listeners.length(); i++) {
    if (listeners.get(i)->IsUndefined()) continue;  // skip deleted ones

    NeanderObject listener(i::JSObject::cast(listeners.get(i)));
    i::Handle<i::Proxy> callback_obj(i::Proxy::cast(listener.get(0)));
    if (callback_obj->proxy() == FUNCTION_ADDR(that)) {
      listeners.set(i, i::Heap::undefined_value());
    }
  }
}


void V8::SetCounterFunction(CounterLookupCallback callback) {
  if (IsDeadCheck("v8::V8::SetCounterFunction()")) return;
  i::StatsTable::SetCounterFunction(callback);
}


void V8::EnableSlidingStateWindow() {
  if (IsDeadCheck("v8::V8::EnableSlidingStateWindow()")) return;
  i::Logger::EnableSlidingStateWindow();
}


void V8::SetFailedAccessCheckCallbackFunction(
      FailedAccessCheckCallback callback) {
  if (IsDeadCheck("v8::V8::SetFailedAccessCheckCallbackFunction()")) return;
  i::Top::SetFailedAccessCheckCallback(callback);
}


void V8::AddObjectToGroup(void* group_id, Persistent<Object> obj) {
  if (IsDeadCheck("v8::V8::AddObjectToGroup()")) return;
  i::GlobalHandles::AddToGroup(group_id, reinterpret_cast<i::Object**>(*obj));
}


void V8::SetGlobalGCPrologueCallback(GCCallback callback) {
  if (IsDeadCheck("v8::V8::SetGlobalGCPrologueCallback()")) return;
  i::Heap::SetGlobalGCPrologueCallback(callback);
}


void V8::SetGlobalGCEpilogueCallback(GCCallback callback) {
  if (IsDeadCheck("v8::V8::SetGlobalGCEpilogueCallback()")) return;
  i::Heap::SetGlobalGCEpilogueCallback(callback);
}


String::AsciiValue::AsciiValue(v8::Handle<v8::Value> obj) {
  EnsureInitialized("v8::String::AsciiValue::AsciiValue()");
  HandleScope scope;
  Handle<String> str = obj->ToString();
  int length = str->Length();
  str_ = i::NewArray<char>(length + 1);
  str->WriteAscii(str_);
}


String::AsciiValue::~AsciiValue() {
  i::DeleteArray(str_);
}


String::Value::Value(v8::Handle<v8::Value> obj) {
  EnsureInitialized("v8::String::Value::Value()");
  HandleScope scope;
  Handle<String> str = obj->ToString();
  int length = str->Length();
  str_ = i::NewArray<uint16_t>(length + 1);
  str->Write(str_);
}


String::Value::~Value() {
  i::DeleteArray(str_);
}

Local<Value> Exception::RangeError(v8::Handle<v8::String> raw_message) {
  LOG_API("RangeError");
  ON_BAILOUT("v8::Exception::RangeError()", return Local<Value>());
  i::Object* error;
  {
    HandleScope scope;
    i::Handle<i::String> message = Utils::OpenHandle(*raw_message);
    i::Handle<i::Object> result = i::Factory::NewRangeError(message);
    error = *result;
  }
  i::Handle<i::Object> result(error);
  return Utils::ToLocal(result);
}

Local<Value> Exception::ReferenceError(v8::Handle<v8::String> raw_message) {
  LOG_API("ReferenceError");
  ON_BAILOUT("v8::Exception::ReferenceError()", return Local<Value>());
  i::Object* error;
  {
    HandleScope scope;
    i::Handle<i::String> message = Utils::OpenHandle(*raw_message);
    i::Handle<i::Object> result = i::Factory::NewReferenceError(message);
    error = *result;
  }
  i::Handle<i::Object> result(error);
  return Utils::ToLocal(result);
}

Local<Value> Exception::SyntaxError(v8::Handle<v8::String> raw_message) {
  LOG_API("SyntaxError");
  ON_BAILOUT("v8::Exception::SyntaxError()", return Local<Value>());
  i::Object* error;
  {
    HandleScope scope;
    i::Handle<i::String> message = Utils::OpenHandle(*raw_message);
    i::Handle<i::Object> result = i::Factory::NewSyntaxError(message);
    error = *result;
  }
  i::Handle<i::Object> result(error);
  return Utils::ToLocal(result);
}

Local<Value> Exception::TypeError(v8::Handle<v8::String> raw_message) {
  LOG_API("TypeError");
  ON_BAILOUT("v8::Exception::TypeError()", return Local<Value>());
  i::Object* error;
  {
    HandleScope scope;
    i::Handle<i::String> message = Utils::OpenHandle(*raw_message);
    i::Handle<i::Object> result = i::Factory::NewTypeError(message);
    error = *result;
  }
  i::Handle<i::Object> result(error);
  return Utils::ToLocal(result);
}

Local<Value> Exception::Error(v8::Handle<v8::String> raw_message) {
  LOG_API("Error");
  ON_BAILOUT("v8::Exception::Error()", return Local<Value>());
  i::Object* error;
  {
    HandleScope scope;
    i::Handle<i::String> message = Utils::OpenHandle(*raw_message);
    i::Handle<i::Object> result = i::Factory::NewError(message);
    error = *result;
  }
  i::Handle<i::Object> result(error);
  return Utils::ToLocal(result);
}


// --- D e b u g   S u p p o r t ---


bool Debug::AddDebugEventListener(DebugEventCallback that, Handle<Value> data) {
  EnsureInitialized("v8::V8::AddDebugEventListener()");
  ON_BAILOUT("v8::V8::AddDebugEventListener()", return false);
  HandleScope scope;
  NeanderArray listeners(i::Factory::debug_event_listeners());
  NeanderObject obj(2);
  obj.set(0, *i::Factory::NewProxy(FUNCTION_ADDR(that)));
  obj.set(1, data.IsEmpty() ?
             i::Heap::undefined_value() :
             *Utils::OpenHandle(*data));
  listeners.add(obj.value());
  i::Debugger::UpdateActiveDebugger();
  return true;
}


bool Debug::AddDebugEventListener(v8::Handle<v8::Function> that,
                                  Handle<Value> data) {
  ON_BAILOUT("v8::V8::AddDebugEventListener()", return false);
  HandleScope scope;
  NeanderArray listeners(i::Factory::debug_event_listeners());
  NeanderObject obj(2);
  obj.set(0, *Utils::OpenHandle(*that));
  obj.set(1, data.IsEmpty() ?
             i::Heap::undefined_value() :
             *Utils::OpenHandle(*data));
  listeners.add(obj.value());
  i::Debugger::UpdateActiveDebugger();
  return true;
}


void Debug::RemoveDebugEventListener(DebugEventCallback that) {
  EnsureInitialized("v8::V8::RemoveDebugEventListener()");
  ON_BAILOUT("v8::V8::RemoveDebugEventListener()", return);
  HandleScope scope;
  NeanderArray listeners(i::Factory::debug_event_listeners());
  for (int i = 0; i < listeners.length(); i++) {
    if (listeners.get(i)->IsUndefined()) continue;  // skip deleted ones

    NeanderObject listener(i::JSObject::cast(listeners.get(i)));
    // When removing a C debug event listener only consider proxy objects.
    if (listener.get(0)->IsProxy()) {
      i::Handle<i::Proxy> callback_obj(i::Proxy::cast(listener.get(0)));
      if (callback_obj->proxy() == FUNCTION_ADDR(that)) {
        listeners.set(i, i::Heap::undefined_value());
      }
    }
  }
  i::Debugger::UpdateActiveDebugger();
}


void Debug::RemoveDebugEventListener(v8::Handle<v8::Function> that) {
  ON_BAILOUT("v8::V8::RemoveDebugEventListener()", return);
  HandleScope scope;
  NeanderArray listeners(i::Factory::debug_event_listeners());
  for (int i = 0; i < listeners.length(); i++) {
    if (listeners.get(i)->IsUndefined()) continue;  // skip deleted ones

    NeanderObject listener(i::JSObject::cast(listeners.get(i)));
    // When removing a JavaScript debug event listener only consider JavaScript
    // function objects.
    if (listener.get(0)->IsJSFunction()) {
      i::JSFunction* callback = i::JSFunction::cast(listener.get(0));
      i::Handle<i::JSFunction> callback_fun(callback);
      if (callback_fun.is_identical_to(Utils::OpenHandle(*that))) {
        listeners.set(i, i::Heap::undefined_value());
      }
    }
  }
  i::Debugger::UpdateActiveDebugger();
}


void Debug::DebugBreak() {
  i::StackGuard::DebugBreak();
}


void Debug::SetMessageHandler(v8::DebugMessageHandler handler, void* data) {
  i::Debugger::SetMessageHandler(handler, data);
}


void Debug::SendCommand(const uint16_t* command, int length) {
  i::Debugger::ProcessCommand(i::Vector<const uint16_t>(command, length));
}


namespace internal {


HandleScopeImplementer* HandleScopeImplementer::instance() {
  return &thread_local;
}


char* HandleScopeImplementer::ArchiveThread(char* storage) {
  return thread_local.ArchiveThreadHelper(storage);
}


char* HandleScopeImplementer::ArchiveThreadHelper(char* storage) {
  ImplementationUtilities::HandleScopeData* current =
      ImplementationUtilities::CurrentHandleScope();
  handle_scope_data_ = *current;
  memcpy(storage, this, sizeof(*this));

  Initialize();
  current->Initialize();

  return storage + ArchiveSpacePerThread();
}


int HandleScopeImplementer::ArchiveSpacePerThread() {
  return sizeof(thread_local);
}


char* HandleScopeImplementer::RestoreThread(char* storage) {
  return thread_local.RestoreThreadHelper(storage);
}


char* HandleScopeImplementer::RestoreThreadHelper(char* storage) {
  memcpy(this, storage, sizeof(*this));
  *ImplementationUtilities::CurrentHandleScope() = handle_scope_data_;
  return storage + ArchiveSpacePerThread();
}


void HandleScopeImplementer::Iterate(
    ObjectVisitor* v,
    List<void**>* blocks,
    ImplementationUtilities::HandleScopeData* handle_data) {
  // Iterate over all handles in the blocks except for the last.
  for (int i = blocks->length() - 2; i >= 0; --i) {
    Object** block =
        reinterpret_cast<Object**>(blocks->at(i));
    v->VisitPointers(block, &block[kHandleBlockSize]);
  }

  // Iterate over live handles in the last block (if any).
  if (!blocks->is_empty()) {
    v->VisitPointers(reinterpret_cast<Object**>(blocks->last()),
       reinterpret_cast<Object**>(handle_data->next));
  }
}


void HandleScopeImplementer::Iterate(ObjectVisitor* v) {
  ImplementationUtilities::HandleScopeData* current =
      ImplementationUtilities::CurrentHandleScope();
  Iterate(v, thread_local.Blocks(), current);
}


char* HandleScopeImplementer::Iterate(ObjectVisitor* v, char* storage) {
  HandleScopeImplementer* thread_local =
      reinterpret_cast<HandleScopeImplementer*>(storage);
  List<void**>* blocks_of_archived_thread = thread_local->Blocks();
  ImplementationUtilities::HandleScopeData* handle_data_of_archived_thread =
      &thread_local->handle_scope_data_;
  Iterate(v, blocks_of_archived_thread, handle_data_of_archived_thread);

  return storage + ArchiveSpacePerThread();
}

} }  // namespace v8::internal
