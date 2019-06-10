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
#include "execution.h"
#include "factory.h"
#include "macro-assembler.h"

namespace v8 { namespace internal {


Handle<FixedArray> Factory::NewFixedArray(int size, PretenureFlag pretenure) {
  ASSERT(0 <= size);
  CALL_HEAP_FUNCTION(Heap::AllocateFixedArray(size, pretenure), FixedArray);
}


Handle<DescriptorArray> Factory::NewDescriptorArray(int number_of_descriptors) {
  ASSERT(0 <= number_of_descriptors);
  CALL_HEAP_FUNCTION(DescriptorArray::Allocate(number_of_descriptors),
                     DescriptorArray);
}


// Symbols are created in the old generation (code space).
Handle<String> Factory::LookupSymbol(Vector<const char> string) {
  CALL_HEAP_FUNCTION(Heap::LookupSymbol(string), String);
}


Handle<String> Factory::NewStringFromAscii(Vector<const char> string,
                                           PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::AllocateStringFromAscii(string, pretenure), String);
}

Handle<String> Factory::NewStringFromUtf8(Vector<const char> string,
                                          PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::AllocateStringFromUtf8(string, pretenure), String);
}


Handle<String> Factory::NewStringFromTwoByte(Vector<const uc16> string) {
  CALL_HEAP_FUNCTION(Heap::AllocateStringFromTwoByte(string), String);
}


Handle<String> Factory::NewRawTwoByteString(int length,
                                            PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::AllocateRawTwoByteString(length, pretenure), String);
}


Handle<String> Factory::NewConsString(Handle<String> first,
                                      Handle<String> second) {
  CALL_HEAP_FUNCTION(Heap::AllocateConsString(*first, *second), String);
}


Handle<String> Factory::NewStringSlice(Handle<String> str, int begin, int end) {
  CALL_HEAP_FUNCTION(str->Slice(begin, end), String);
}


Handle<String> Factory::NewExternalStringFromAscii(
    ExternalAsciiString::Resource* resource) {
  CALL_HEAP_FUNCTION(Heap::AllocateExternalStringFromAscii(resource), String);
}


Handle<String> Factory::NewExternalStringFromTwoByte(
    ExternalTwoByteString::Resource* resource) {
  CALL_HEAP_FUNCTION(Heap::AllocateExternalStringFromTwoByte(resource), String);
}


Handle<Context> Factory::NewGlobalContext() {
  CALL_HEAP_FUNCTION(Heap::AllocateGlobalContext(), Context);
}


Handle<Context> Factory::NewFunctionContext(int length,
                                            Handle<JSFunction> closure) {
  CALL_HEAP_FUNCTION(Heap::AllocateFunctionContext(length, *closure), Context);
}


Handle<Context> Factory::NewWithContext(Handle<Context> previous,
                                        Handle<JSObject> extension) {
  CALL_HEAP_FUNCTION(Heap::AllocateWithContext(*previous, *extension), Context);
}


Handle<Struct> Factory::NewStruct(InstanceType type) {
  CALL_HEAP_FUNCTION(Heap::AllocateStruct(type), Struct);
}


Handle<AccessorInfo> Factory::NewAccessorInfo() {
  Handle<AccessorInfo> info =
      Handle<AccessorInfo>::cast(NewStruct(ACCESSOR_INFO_TYPE));
  info->set_flag(0);  // Must clear the flag, it was initialized as undefined.
  return info;
}


Handle<Script> Factory::NewScript(Handle<String> source) {
  Handle<Script> script = Handle<Script>::cast(NewStruct(SCRIPT_TYPE));
  script->set_source(*source);
  script->set_name(Heap::undefined_value());
  script->set_line_offset(Smi::FromInt(0));
  script->set_column_offset(Smi::FromInt(0));
  script->set_wrapper(*Factory::NewProxy(0, TENURED));
  script->set_type(Smi::FromInt(SCRIPT_TYPE_NORMAL));
  return script;
}


Handle<Proxy> Factory::NewProxy(Address addr, PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::AllocateProxy(addr, pretenure), Proxy);
}


Handle<Proxy> Factory::NewProxy(const AccessorDescriptor* desc) {
  return NewProxy((Address) desc, TENURED);
}


Handle<ByteArray> Factory::NewByteArray(int length) {
  ASSERT(0 <= length);
  CALL_HEAP_FUNCTION(Heap::AllocateByteArray(length), ByteArray);
}


Handle<Map> Factory::NewMap(InstanceType type, int instance_size) {
  CALL_HEAP_FUNCTION(Heap::AllocateMap(type, instance_size), Map);
}


Handle<JSObject> Factory::NewFunctionPrototype(Handle<JSFunction> function) {
  CALL_HEAP_FUNCTION(Heap::AllocateFunctionPrototype(*function), JSObject);
}


Handle<Map> Factory::CopyMap(Handle<Map> src) {
  CALL_HEAP_FUNCTION(src->Copy(), Map);
}


Handle<FixedArray> Factory::CopyFixedArray(Handle<FixedArray> array) {
  CALL_HEAP_FUNCTION(array->Copy(), FixedArray);
}


Handle<JSFunction> Factory::BaseNewFunctionFromBoilerplate(
    Handle<JSFunction> boilerplate,
    Handle<Map> function_map) {
  ASSERT(boilerplate->IsBoilerplate());
  ASSERT(!boilerplate->has_initial_map());
  ASSERT(!boilerplate->has_prototype());
  ASSERT(boilerplate->properties() == Heap::empty_fixed_array());
  ASSERT(boilerplate->elements() == Heap::empty_fixed_array());
  CALL_HEAP_FUNCTION(Heap::AllocateFunction(*function_map,
                                            boilerplate->shared(),
                                            Heap::the_hole_value()),
                     JSFunction);
}


Handle<JSFunction> Factory::NewFunctionFromBoilerplate(
    Handle<JSFunction> boilerplate,
    Handle<Context> context) {
  Handle<JSFunction> result =
      BaseNewFunctionFromBoilerplate(boilerplate, Top::function_map());
  result->set_context(*context);
  int number_of_literals = boilerplate->literals()->length();
  if (number_of_literals > 0) {
    Handle<FixedArray> literals =
        Factory::NewFixedArray(number_of_literals, TENURED);
    result->set_literals(*literals);
  }
  ASSERT(!result->IsBoilerplate());
  return result;
}


Handle<Object> Factory::NewNumber(double value,
                                  PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::NumberFromDouble(value, pretenure), Object);
}


Handle<Object> Factory::NewNumberFromInt(int value) {
  CALL_HEAP_FUNCTION(Heap::NumberFromInt32(value), Object);
}


Handle<JSObject> Factory::NewNeanderObject() {
  CALL_HEAP_FUNCTION(Heap::AllocateJSObjectFromMap(Heap::neander_map()),
                     JSObject);
}


Handle<Object> Factory::NewTypeError(const char* type,
                                     Vector< Handle<Object> > args) {
  return NewError("MakeTypeError", type, args);
}


Handle<Object> Factory::NewTypeError(Handle<String> message) {
  return NewError("$TypeError", message);
}


Handle<Object> Factory::NewRangeError(const char* type,
                                      Vector< Handle<Object> > args) {
  return NewError("MakeRangeError", type, args);
}


Handle<Object> Factory::NewRangeError(Handle<String> message) {
  return NewError("$RangeError", message);
}


Handle<Object> Factory::NewSyntaxError(const char* type, Handle<JSArray> args) {
  return NewError("MakeSyntaxError", type, args);
}


Handle<Object> Factory::NewSyntaxError(Handle<String> message) {
  return NewError("$SyntaxError", message);
}


Handle<Object> Factory::NewReferenceError(const char* type,
                                          Vector< Handle<Object> > args) {
  return NewError("MakeReferenceError", type, args);
}


Handle<Object> Factory::NewReferenceError(Handle<String> message) {
  return NewError("$ReferenceError", message);
}


Handle<Object> Factory::NewError(const char* maker, const char* type,
    Vector< Handle<Object> > args) {
  HandleScope scope;
  Handle<JSArray> array = NewJSArray(args.length());
  for (int i = 0; i < args.length(); i++)
    SetElement(array, i, args[i]);
  Handle<Object> result = NewError(maker, type, array);
  return result.EscapeFrom(&scope);
}


Handle<Object> Factory::NewEvalError(const char* type,
                                     Vector< Handle<Object> > args) {
  return NewError("MakeEvalError", type, args);
}


Handle<Object> Factory::NewError(const char* type,
                                 Vector< Handle<Object> > args) {
  return NewError("MakeError", type, args);
}


Handle<Object> Factory::NewError(const char* maker,
                                 const char* type,
                                 Handle<JSArray> args) {
  Handle<String> make_str = Factory::LookupAsciiSymbol(maker);
  Handle<JSFunction> fun =
      Handle<JSFunction>(
          JSFunction::cast(
              Top::security_context_builtins()->GetProperty(*make_str)));
  Handle<Object> type_obj = Factory::LookupAsciiSymbol(type);
  Object** argv[2] = { type_obj.location(),
                       Handle<Object>::cast(args).location() };

  // Invoke the JavaScript factory method. If an exception is thrown while
  // running the factory method, use the exception as the result.
  bool caught_exception;
  Handle<Object> result = Execution::TryCall(fun,
                                             Top::security_context_builtins(),
                                             2,
                                             argv,
                                             &caught_exception);
  return result;
}


Handle<Object> Factory::NewError(Handle<String> message) {
  return NewError("$Error", message);
}


Handle<Object> Factory::NewError(const char* constructor,
                                 Handle<String> message) {
  Handle<String> constr = Factory::LookupAsciiSymbol(constructor);
  Handle<JSFunction> fun =
      Handle<JSFunction>(
          JSFunction::cast(
              Top::security_context_builtins()->GetProperty(*constr)));
  Object** argv[1] = { Handle<Object>::cast(message).location() };

  // Invoke the JavaScript factory method. If an exception is thrown while
  // running the factory method, use the exception as the result.
  bool caught_exception;
  Handle<Object> result = Execution::TryCall(fun,
                                             Top::security_context_builtins(),
                                             1,
                                             argv,
                                             &caught_exception);
  return result;
}


Handle<JSFunction> Factory::NewFunction(Handle<String> name,
                                        InstanceType type,
                                        int instance_size,
                                        Handle<Code> code,
                                        bool force_initial_map) {
  // Allocate the function
  Handle<JSFunction> function = NewFunction(name, the_hole_value());
  function->set_code(*code);

  if (force_initial_map ||
      type != JS_OBJECT_TYPE ||
      instance_size != JSObject::kHeaderSize) {
    Handle<Map> initial_map = NewMap(type, instance_size);
    Handle<JSObject> prototype = NewFunctionPrototype(function);
    initial_map->set_prototype(*prototype);
    function->set_initial_map(*initial_map);
    initial_map->set_constructor(*function);
  } else {
    ASSERT(!function->has_initial_map());
    ASSERT(!function->has_prototype());
  }

  return function;
}


Handle<JSFunction> Factory::NewFunctionBoilerplate(Handle<String> name,
                                                   int number_of_literals,
                                                   Handle<Code> code) {
  Handle<JSFunction> function = NewFunctionBoilerplate(name);
  function->set_code(*code);
  if (number_of_literals > 0) {
    Handle<FixedArray> literals =
        Factory::NewFixedArray(number_of_literals, TENURED);
    function->set_literals(*literals);
  } else {
    function->set_literals(Heap::empty_fixed_array());
  }
  ASSERT(!function->has_initial_map());
  ASSERT(!function->has_prototype());
  return function;
}


Handle<JSFunction> Factory::NewFunctionBoilerplate(Handle<String> name) {
  Handle<SharedFunctionInfo> shared = NewSharedFunctionInfo(name);
  CALL_HEAP_FUNCTION(Heap::AllocateFunction(Heap::boilerplate_function_map(),
                                            *shared,
                                            Heap::the_hole_value()),
                     JSFunction);
}


Handle<JSFunction> Factory::NewFunctionWithPrototype(Handle<String> name,
                                                     InstanceType type,
                                                     int instance_size,
                                                     Handle<JSObject> prototype,
                                                     Handle<Code> code,
                                                     bool force_initial_map) {
  // Allocate the function
  Handle<JSFunction> function = NewFunction(name, prototype);

  function->set_code(*code);

  if (force_initial_map ||
      type != JS_OBJECT_TYPE ||
      instance_size != JSObject::kHeaderSize) {
    Handle<Map> initial_map = NewMap(type, instance_size);
    function->set_initial_map(*initial_map);
    initial_map->set_constructor(*function);
  }

  // Set function.prototype and give the prototype a constructor
  // property that refers to the function.
  SetPrototypeProperty(function, prototype);
  SetProperty(prototype, Factory::constructor_symbol(), function, DONT_ENUM);
  return function;
}

Handle<Code> Factory::NewCode(const CodeDesc& desc, ScopeInfo<>* sinfo,
                              Code::Flags flags) {
  CALL_HEAP_FUNCTION(Heap::CreateCode(desc, sinfo, flags), Code);
}


Handle<Code> Factory::CopyCode(Handle<Code> code) {
  CALL_HEAP_FUNCTION(Heap::CopyCode(*code), Code);
}


#define CALL_GC(RETRY)                                                     \
  do {                                                                     \
    if (!Heap::CollectGarbage(Failure::cast(RETRY)->requested(),           \
                              Failure::cast(RETRY)->allocation_space())) { \
      /* TODO(1181417): Fix this. */                                       \
      V8::FatalProcessOutOfMemory("Factory CALL_GC");                      \
    }                                                                      \
  } while (false)


// Allocate the new array. We cannot use the CALL_HEAP_FUNCTION macro here,
// because the stack-allocated CallbacksDescriptor instance is not GC safe.
Handle<DescriptorArray> Factory::CopyAppendProxyDescriptor(
    Handle<DescriptorArray> array,
    Handle<String> key,
    Handle<Object> value,
    PropertyAttributes attributes) {
  GC_GREEDY_CHECK();
  CallbacksDescriptor desc(*key, *value, attributes);
  Object* obj = array->CopyInsert(&desc);
  if (obj->IsRetryAfterGC()) {
    CALL_GC(obj);
    CallbacksDescriptor desc(*key, *value, attributes);
    obj = array->CopyInsert(&desc);
    if (obj->IsFailure()) {
      // TODO(1181417): Fix this.
      V8::FatalProcessOutOfMemory("CopyAppendProxyDescriptor");
    }
  }
  return Handle<DescriptorArray>(DescriptorArray::cast(obj));
}

#undef CALL_GC


Handle<String> Factory::SymbolFromString(Handle<String> value) {
  CALL_HEAP_FUNCTION(Heap::LookupSymbol(*value), String);
}


Handle<DescriptorArray> Factory::CopyAppendCallbackDescriptors(
    Handle<DescriptorArray> array,
    Handle<Object> descriptors) {
  v8::NeanderArray callbacks(descriptors);
  int nof_callbacks = callbacks.length();
  Handle<DescriptorArray> result =
      NewDescriptorArray(array->number_of_descriptors() + nof_callbacks);

  // Number of descriptors added to the result so far.
  int descriptor_count = 0;

  // Copy the descriptors from the array.
  DescriptorWriter w(*result);
  for (DescriptorReader r(*array); !r.eos(); r.advance()) {
    w.WriteFrom(&r);
    descriptor_count++;
  }

  // Number of duplicates detected.
  int duplicates = 0;

  // Fill in new callback descriptors.  Process the callbacks from
  // back to front so that the last callback with a given name takes
  // precedence over previously added callbacks with that name.
  for (int i = nof_callbacks - 1; i >= 0; i--) {
    Handle<AccessorInfo> entry =
        Handle<AccessorInfo>(AccessorInfo::cast(callbacks.get(i)));
    // Ensure the key is a symbol before writing into the instance descriptor.
    Handle<String> key =
        SymbolFromString(Handle<String>(String::cast(entry->name())));
    // Check if a descriptor with this name already exists before writing.
    if (result->BinarySearch(*key, 0, descriptor_count - 1) ==
        DescriptorArray::kNotFound) {
      CallbacksDescriptor desc(*key, *entry, entry->property_attributes());
      w.Write(&desc);
      descriptor_count++;
    } else {
      duplicates++;
    }
  }

  // If duplicates were detected, allocate a result of the right size
  // and transfer the elements.
  if (duplicates > 0) {
    Handle<DescriptorArray> new_result =
        NewDescriptorArray(result->number_of_descriptors() - duplicates);
    DescriptorWriter w(*new_result);
    DescriptorReader r(*result);
    while (!w.eos()) {
      w.WriteFrom(&r);
      r.advance();
    }
    result = new_result;
  }

  // Sort the result before returning.
  result->Sort();
  return result;
}


Handle<JSObject> Factory::NewJSObject(Handle<JSFunction> constructor,
                                      PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(Heap::AllocateJSObject(*constructor, pretenure), JSObject);
}


Handle<JSObject> Factory::NewObjectLiteral(int expected_number_of_properties) {
  Handle<Map> map = Handle<Map>(Top::object_function()->initial_map());
  map = Factory::CopyMap(map);
  map->set_instance_descriptors(
      DescriptorArray::cast(Heap::empty_fixed_array()));
  map->set_unused_property_fields(expected_number_of_properties);
  CALL_HEAP_FUNCTION(Heap::AllocateJSObjectFromMap(*map, TENURED),
                     JSObject);
}


Handle<JSArray> Factory::NewArrayLiteral(int length) {
  return NewJSArrayWithElements(NewFixedArray(length), TENURED);
}


Handle<JSArray> Factory::NewJSArray(int length,
                                    PretenureFlag pretenure) {
  Handle<JSObject> obj = NewJSObject(Top::array_function(), pretenure);
  CALL_HEAP_FUNCTION(Handle<JSArray>::cast(obj)->Initialize(length), JSArray);
}


Handle<JSArray> Factory::NewJSArrayWithElements(Handle<FixedArray> elements,
                                                PretenureFlag pretenure) {
  Handle<JSArray> result =
      Handle<JSArray>::cast(NewJSObject(Top::array_function(), pretenure));
  result->SetContent(*elements);
  return result;
}


Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfo(Handle<String> name) {
  CALL_HEAP_FUNCTION(Heap::AllocateSharedFunctionInfo(*name),
                     SharedFunctionInfo);
}


Handle<Dictionary> Factory::DictionaryAtNumberPut(Handle<Dictionary> dictionary,
                                                  uint32_t key,
                                                  Handle<Object> value) {
  CALL_HEAP_FUNCTION(dictionary->AtNumberPut(key, *value), Dictionary);
}


Handle<JSFunction> Factory::NewFunctionHelper(Handle<String> name,
                                              Handle<Object> prototype) {
  Handle<SharedFunctionInfo> function_share = NewSharedFunctionInfo(name);
  CALL_HEAP_FUNCTION(Heap::AllocateFunction(*Top::function_map(),
                                            *function_share,
                                            *prototype),
                     JSFunction);
}


Handle<JSFunction> Factory::NewFunction(Handle<String> name,
                                        Handle<Object> prototype) {
  Handle<JSFunction> fun = NewFunctionHelper(name, prototype);
  fun->set_context(Top::context()->global_context());
  return fun;
}


Handle<Object> Factory::ToObject(Handle<Object> object,
                                 Handle<Context> global_context) {
  CALL_HEAP_FUNCTION(object->ToObject(*global_context), Object);
}


Handle<JSObject> Factory::NewArgumentsObject(Handle<Object> callee,
                                             int length) {
  CALL_HEAP_FUNCTION(Heap::AllocateArgumentsObject(*callee, length), JSObject);
}


Handle<JSFunction> Factory::CreateApiFunction(
    Handle<FunctionTemplateInfo> obj,
    bool is_global) {
  Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::HandleApiCall));

  int internal_field_count = 0;
  if (!obj->instance_template()->IsUndefined()) {
    Handle<ObjectTemplateInfo> instance_template =
        Handle<ObjectTemplateInfo>(
            ObjectTemplateInfo::cast(obj->instance_template()));
    internal_field_count =
        Smi::cast(instance_template->internal_field_count())->value();
  }

  int instance_size = kPointerSize * internal_field_count;
  if (is_global) {
    instance_size += JSGlobalObject::kSize;
  } else {
    instance_size += JSObject::kHeaderSize;
  }

  InstanceType type = is_global ? JS_GLOBAL_OBJECT_TYPE : JS_OBJECT_TYPE;

  Handle<JSFunction> result =
      Factory::NewFunction(Factory::empty_symbol(), type, instance_size,
                           code, true);
  // Set class name.
  Handle<Object> class_name = Handle<Object>(obj->class_name());
  if (class_name->IsString()) {
    result->shared()->set_instance_class_name(*class_name);
    result->shared()->set_name(*class_name);
  }

  Handle<Map> map = Handle<Map>(result->initial_map());

  // Mark as undetectable if needed.
  if (obj->undetectable()) {
    map->set_is_undetectable();
  }

  // Mark as hidden for the __proto__ accessor if needed.
  if (obj->hidden_prototype()) {
    map->set_is_hidden_prototype();
  }

  // Mark as needs_access_check if needed.
  if (obj->needs_access_check()) {
    map->set_needs_access_check();
  }

  // If the function template info specifies a lookup handler the
  // initial_map must have set the bit has_special_lookup.
  if (obj->lookup_callback()->IsProxy()) {
    ASSERT(!map->has_special_lookup());
    map->set_special_lookup();
  }

  // Set interceptor information in the map.
  if (!obj->named_property_handler()->IsUndefined()) {
    map->set_has_named_interceptor();
  }
  if (!obj->indexed_property_handler()->IsUndefined()) {
    map->set_has_indexed_interceptor();
  }

  // Set instance call-as-function information in the map.
  if (!obj->instance_call_handler()->IsUndefined()) {
    map->set_has_instance_call_handler();
  }

  result->shared()->set_function_data(*obj);

  // Recursively copy parent templates' accessors, 'data' may be modified.
  Handle<DescriptorArray> array =
      Handle<DescriptorArray>(map->instance_descriptors());
  while (true) {
    Handle<Object> props = Handle<Object>(obj->property_accessors());
    if (!props->IsUndefined()) {
      array = Factory::CopyAppendCallbackDescriptors(array, props);
    }
    Handle<Object> parent = Handle<Object>(obj->parent_template());
    if (parent->IsUndefined()) break;
    obj = Handle<FunctionTemplateInfo>::cast(parent);
  }
  if (array->length() > 0) {
    map->set_instance_descriptors(*array);
  }

  return result;
}


void Factory::ConfigureInstance(Handle<FunctionTemplateInfo> desc,
                                Handle<JSObject> instance,
                                bool* pending_exception) {
  // Configure the instance by adding the properties specified by the
  // instance template.
  Handle<Object> instance_template = Handle<Object>(desc->instance_template());
  if (!instance_template->IsUndefined()) {
    Execution::ConfigureInstance(instance,
                                 instance_template,
                                 pending_exception);
  } else {
    *pending_exception = false;
  }
}


} }  // namespace v8::internal
