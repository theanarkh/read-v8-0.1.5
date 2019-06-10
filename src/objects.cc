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
#include "debug.h"
#include "execution.h"
#include "objects-inl.h"
#include "macro-assembler.h"
#include "scanner.h"
#include "scopeinfo.h"
#include "string-stream.h"

namespace v8 { namespace internal {

#ifdef DEBUG
DEFINE_bool(trace_normalization,
            false,
            "prints when objects are turned into dictionaries.");
#endif

// Getters and setters are stored in a fixed array property.  These are
// constants for their indices.
const int kGetterIndex = 0;
const int kSetterIndex = 1;

bool Object::IsInstanceOf(FunctionTemplateInfo* expected) {
  // There is a constraint on the object; check
  if (!this->IsJSObject()) return false;
  // Fetch the constructor function of the object
  Object* cons_obj = JSObject::cast(this)->map()->constructor();
  if (!cons_obj->IsJSFunction()) return false;
  JSFunction* fun = JSFunction::cast(cons_obj);
  // Iterate through the chain of inheriting function templates to
  // see if the required one occurs.
  for (Object* type = fun->shared()->function_data();
       type->IsFunctionTemplateInfo();
       type = FunctionTemplateInfo::cast(type)->parent_template()) {
    if (type == expected) return true;
  }
  // Didn't find the required type in the inheritance chain.
  return false;
}


static Object* CreateJSValue(JSFunction* constructor, Object* value) {
  Object* result = Heap::AllocateJSObject(constructor);
  if (result->IsFailure()) return result;
  JSValue::cast(result)->set_value(value);
  return result;
}


Object* Object::ToObject(Context* global_context) {
  if (IsNumber()) {
    return CreateJSValue(global_context->number_function(), this);
  } else if (IsBoolean()) {
    return CreateJSValue(global_context->boolean_function(), this);
  } else if (IsString()) {
    return CreateJSValue(global_context->string_function(), this);
  }
  ASSERT(IsJSObject());
  return this;
}


Object* Object::ToObject() {
  Context* global_context = Top::context()->global_context();
  if (IsJSObject()) {
    return this;
  } else if (IsNumber()) {
    return CreateJSValue(global_context->number_function(), this);
  } else if (IsBoolean()) {
    return CreateJSValue(global_context->boolean_function(), this);
  } else if (IsString()) {
    return CreateJSValue(global_context->string_function(), this);
  }

  // Throw a type error.
  return Failure::InternalError();
}


Object* Object::ToBoolean() {
  if (IsTrue()) return Heap::true_value();
  if (IsFalse()) return Heap::false_value();
  if (IsSmi()) {
    return Heap::ToBoolean(Smi::cast(this)->value() != 0);
  }
  if (IsUndefined() || IsNull()) return Heap::false_value();
  // Undetectable object is false
  if (IsUndetectableObject()) {
    return Heap::false_value();
  }
  if (IsString()) {
    return Heap::ToBoolean(String::cast(this)->length() != 0);
  }
  if (IsHeapNumber()) {
    return HeapNumber::cast(this)->HeapNumberToBoolean();
  }
  return Heap::true_value();
}


void Object::Lookup(String* name, LookupResult* result) {
  if (IsJSObject()) return JSObject::cast(this)->Lookup(name, result);
  Object* holder = NULL;
  Context* global_context = Top::context()->global_context();
  if (IsString()) {
    holder = global_context->string_function()->instance_prototype();
  } else if (IsNumber()) {
    holder = global_context->number_function()->instance_prototype();
  } else if (IsBoolean()) {
    holder = global_context->boolean_function()->instance_prototype();
  }
  ASSERT(holder != NULL);  // cannot handle null or undefined.
  JSObject::cast(holder)->Lookup(name, result);
}


Object* Object::GetPropertyWithReceiver(Object* receiver,
                                        String* name,
                                        PropertyAttributes* attributes) {
  LookupResult result;
  Lookup(name, &result);
  return GetProperty(receiver, &result, name, attributes);
}


Object* Object::GetPropertyWithCallback(Object* receiver,
                                        Object* structure,
                                        String* name,
                                        Object* holder) {
  // To accommodate both the old and the new api we switch on the
  // data structure used to store the callbacks.  Eventually proxy
  // callbacks should be phased out.
  if (structure->IsProxy()) {
    AccessorDescriptor* callback =
        reinterpret_cast<AccessorDescriptor*>(Proxy::cast(structure)->proxy());
    Object* value = (callback->getter)(receiver, callback->data);
    RETURN_IF_SCHEDULED_EXCEPTION();
    return value;
  }

  // api style callbacks.
  if (structure->IsAccessorInfo()) {
    AccessorInfo* data = AccessorInfo::cast(structure);
    Object* fun_obj = data->getter();
    v8::AccessorGetter call_fun = v8::ToCData<v8::AccessorGetter>(fun_obj);
    HandleScope scope;
    Handle<JSObject> self(JSObject::cast(receiver));
    Handle<JSObject> holder_handle(JSObject::cast(holder));
    Handle<String> key(name);
    Handle<Object> fun_data(data->data());
    LOG(ApiNamedPropertyAccess("load", *self, name));
    v8::AccessorInfo info(v8::Utils::ToLocal(self),
                          v8::Utils::ToLocal(fun_data),
                          v8::Utils::ToLocal(holder_handle));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = call_fun(v8::Utils::ToLocal(key), info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (result.IsEmpty()) return Heap::undefined_value();
    return *v8::Utils::OpenHandle(*result);
  }

  // __defineGetter__ callback
  if (structure->IsFixedArray()) {
    Object* getter = FixedArray::cast(structure)->get(kGetterIndex);
    if (getter->IsJSFunction()) {
      HandleScope scope;
      Handle<JSFunction> fun(JSFunction::cast(getter));
      Handle<Object> self(receiver);
      bool has_pending_exception;
      Object* result =
          *Execution::Call(fun, self, 0, NULL, &has_pending_exception);
      // Check for pending exception and return the result.
      if (has_pending_exception) return Failure::Exception();
      return result;
    }
    // Getter is not a function.
    return Heap::undefined_value();
  }

  UNREACHABLE();
  return 0;
}


// Only deal with CALLBACKS and INTERCEPTOR
Object* JSObject::GetPropertyWithFailedAccessCheck(Object* receiver,
                                                   LookupResult* result,
                                                   String* name) {
  if (result->IsValid()) {
    switch (result->type()) {
      case CALLBACKS: {
        // Only allow API accessors.
        Object* obj = result->GetCallbackObject();
        if (obj->IsAccessorInfo()) {
          AccessorInfo* info = AccessorInfo::cast(obj);
          if (info->all_can_read()) {
            return GetPropertyWithCallback(receiver,
                                           result->GetCallbackObject(),
                                           name,
                                           result->holder());
          }
        }
        break;
      }
      case NORMAL:
      case FIELD:
      case CONSTANT_FUNCTION: {
        // Search ALL_CAN_READ accessors in prototype chain.
        LookupResult r;
        result->holder()->LookupRealNamedPropertyInPrototypes(name, &r);
        if (r.IsValid()) {
          return GetPropertyWithFailedAccessCheck(receiver, &r, name);
        }
        break;
      }
      case INTERCEPTOR: {
        // If the object has an interceptor, try real named properties.
        // No access check in GetPropertyAttributeWithInterceptor.
        LookupResult r;
        result->holder()->LookupRealNamedProperty(name, &r);
        if (r.IsValid()) {
          return GetPropertyWithFailedAccessCheck(receiver, &r, name);
        }
        break;
      }
      default: {
        break;
      }
    }
  }

  Top::ReportFailedAccessCheck(this, v8::ACCESS_GET);
  return Heap::undefined_value();
}


Object* JSObject::GetLazyProperty(Object* receiver,
                                  LookupResult* result,
                                  String* name,
                                  PropertyAttributes* attributes) {
  HandleScope scope;
  Handle<Object> this_handle(this);
  Handle<Object> receiver_handle(receiver);
  Handle<String> name_handle(name);
  bool pending_exception;
  LoadLazy(Handle<JSFunction>(JSFunction::cast(result->GetValue())),
           &pending_exception);
  if (pending_exception) return Failure::Exception();
  return this_handle->GetPropertyWithReceiver(*receiver_handle,
                                              *name_handle,
                                              attributes);
}


Object* JSObject::SetLazyProperty(LookupResult* result,
                                  String* name,
                                  Object* value,
                                  PropertyAttributes attributes) {
  HandleScope scope;
  Handle<JSObject> this_handle(this);
  Handle<String> name_handle(name);
  Handle<Object> value_handle(value);
  bool pending_exception;
  LoadLazy(Handle<JSFunction>(JSFunction::cast(result->GetValue())),
           &pending_exception);
  if (pending_exception) return Failure::Exception();
  return this_handle->SetProperty(*name_handle, *value_handle, attributes);
}


Object* JSObject::DeleteLazyProperty(LookupResult* result, String* name) {
  HandleScope scope;
  Handle<JSObject> this_handle(this);
  Handle<String> name_handle(name);
  bool pending_exception;
  LoadLazy(Handle<JSFunction>(JSFunction::cast(result->GetValue())),
           &pending_exception);
  if (pending_exception) return Failure::Exception();
  return this_handle->DeleteProperty(*name_handle);
}


Object* Object::GetProperty(Object* receiver,
                            LookupResult* result,
                            String* name,
                            PropertyAttributes* attributes) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;

  // Traverse the prototype chain from the current object (this) to
  // the holder and check for access rights. This avoid traversing the
  // objects more than once in case of interceptors, because the
  // holder will always be the interceptor holder and the search may
  // only continue with a current object just after the interceptor
  // holder in the prototype chain.
  Object* last = result->IsValid() ? result->holder() : Heap::null_value();
  for (Object* current = this; true; current = current->GetPrototype()) {
    if (current->IsAccessCheckNeeded()) {
      // Check if we're allowed to read from the current object. Note
      // that even though we may not actually end up loading the named
      // property from the current object, we still check that we have
      // access to the it.
      JSObject* checked = JSObject::cast(current);
      if (!Top::MayNamedAccess(checked, name, v8::ACCESS_GET)) {
        return checked->GetPropertyWithFailedAccessCheck(receiver,
                                                         result,
                                                         name);
      }
    }
    // Stop traversing the chain once we reach the last object in the
    // chain; either the holder of the result or null in case of an
    // absent property.
    if (current == last) break;
  }

  if (!result->IsProperty()) {
    *attributes = ABSENT;
    return Heap::undefined_value();
  }
  *attributes = result->GetAttributes();
  if (!result->IsLoaded()) {
    return JSObject::cast(this)->GetLazyProperty(receiver,
                                                 result,
                                                 name,
                                                 attributes);
  }
  Object* value;
  JSObject* holder = result->holder();
  switch (result->type()) {
    case NORMAL:
      value =
          holder->property_dictionary()->ValueAt(result->GetDictionaryEntry());
      ASSERT(!value->IsTheHole() || result->IsReadOnly());
      return value->IsTheHole() ? Heap::undefined_value() : value;
    case FIELD:
      value = holder->properties()->get(result->GetFieldIndex());
      ASSERT(!value->IsTheHole() || result->IsReadOnly());
      return value->IsTheHole() ? Heap::undefined_value() : value;
    case CONSTANT_FUNCTION:
      return result->GetConstantFunction();
    case CALLBACKS:
      return GetPropertyWithCallback(receiver,
                                     result->GetCallbackObject(),
                                     name,
                                     holder);
    case INTERCEPTOR: {
      JSObject* recvr = JSObject::cast(receiver);
      return holder->GetPropertyWithInterceptor(recvr, name, attributes);
    }
    default:
      UNREACHABLE();
      return NULL;
  }
}


Object* Object::GetElementWithReceiver(Object* receiver, uint32_t index) {
  // Non-JS objects do not have integer indexed properties.
  if (!IsJSObject()) return Heap::undefined_value();
  return JSObject::cast(this)->GetElementWithReceiver(JSObject::cast(receiver),
                                                      index);
}


Object* Object::GetPrototype() {
  // The object is either a number, a string, a boolean, or a real JS object.
  if (IsJSObject()) return JSObject::cast(this)->map()->prototype();
  Context* context = Top::context()->global_context();

  if (IsNumber()) return context->number_function()->instance_prototype();
  if (IsString()) return context->string_function()->instance_prototype();
  if (IsBoolean()) {
    return context->boolean_function()->instance_prototype();
  } else {
    return Heap::null_value();
  }
}


void Object::ShortPrint() {
  HeapStringAllocator allocator;
  StringStream accumulator(&allocator);
  ShortPrint(&accumulator);
  accumulator.OutputToStdOut();
}


void Object::ShortPrint(StringStream* accumulator) {
  if (IsSmi()) {
    Smi::cast(this)->SmiPrint(accumulator);
  } else if (IsFailure()) {
    Failure::cast(this)->FailurePrint(accumulator);
  } else {
    HeapObject::cast(this)->HeapObjectShortPrint(accumulator);
  }
}


void Smi::SmiPrint() {
  PrintF("%d", value());
}


void Smi::SmiPrint(StringStream* accumulator) {
  accumulator->Add("%d", value());
}


void Failure::FailurePrint(StringStream* accumulator) {
  accumulator->Add("Failure(%d)", value());
}


void Failure::FailurePrint() {
  PrintF("Failure(%d)", value());
}


Failure* Failure::RetryAfterGC(int requested_bytes, AllocationSpace space) {
  ASSERT((space & ~kSpaceTagMask) == 0);
  int requested = requested_bytes >> kObjectAlignmentBits;
  int value = (requested << kSpaceTagSize) | space;
  // We can't very well allocate a heap number in this situation, and if the
  // requested memory is so large it seems reasonable to say that this is an
  // out of memory situation.  This fixes a crash in
  // js1_5/Regress/regress-303213.js.
  if (value >> kSpaceTagSize != requested ||
      !Smi::IsValid(value) ||
      value != ((value << kFailureTypeTagSize) >> kFailureTypeTagSize) ||
      !Smi::IsValid(value << kFailureTypeTagSize)) {
    Top::context()->mark_out_of_memory();
    return Failure::OutOfMemoryException();
  }
  return Construct(RETRY_AFTER_GC, value);
}


// Should a word be prefixed by 'a' or 'an' in order to read naturally in
// English?  Returns false for non-ASCII or words that don't start with
// a capital letter.  The a/an rule follows pronunciation in English.
// We don't use the BBC's overcorrect "an historic occasion" though if
// you speak a dialect you may well say "an 'istoric occasion".
static bool AnWord(String* str) {
  if (str->length() == 0) return false;  // a nothing
  int c0 = str->Get(0);
  int c1 = str->length() > 1 ? str->Get(1) : 0;
  if (c0 == 'U') {
    if (c1 > 'Z') {
      return true;  // an Umpire, but a UTF8String, a U
    }
  } else if (c0 == 'A' || c0 == 'E' || c0 == 'I' || c0 == 'O') {
    return true;   // an Ape, an ABCBook
  } else if ((c1 == 0 || (c1 >= 'A' && c1 <= 'Z')) &&
           (c0 == 'F' || c0 == 'H' || c0 == 'M' || c0 == 'N' || c0 == 'R' ||
            c0 == 'S' || c0 == 'X')) {
    return true;   // an MP3File, an M
  }
  return false;
}


Object* String::Flatten() {
#ifdef DEBUG
  // Do not attempt to flatten in debug mode when allocation is not
  // allowed.  This is to avoid an assertion failure when allocating.
  // Flattening strings is the only case where we always allow
  // allocation because no GC is performed if the allocation fails.
  if (!Heap::IsAllocationAllowed()) return this;
#endif

  switch (representation_tag()) {
    case kSlicedStringTag: {
      SlicedString* ss = SlicedString::cast(this);
      // The SlicedString constructor should ensure that there are no
      // SlicedStrings that are constructed directly on top of other
      // SlicedStrings.
      ASSERT(!ss->buffer()->IsSlicedString());
      Object* ok = String::cast(ss->buffer())->Flatten();
      if (ok->IsFailure()) return ok;
      return this;
    }
    case kConsStringTag: {
      ConsString* cs = ConsString::cast(this);
      if (String::cast(cs->second())->length() == 0) {
        return this;
      }
      // There's little point in putting the flat string in new space if the
      // cons string is in old space.  It can never get GCed until there is
      // an old space GC.
      PretenureFlag tenure = Heap::InNewSpace(this) ? NOT_TENURED : TENURED;
      Object* object = IsAscii() ?
          Heap::AllocateRawAsciiString(length(), tenure) :
          Heap::AllocateRawTwoByteString(length(), tenure);
      if (object->IsFailure()) return object;
      String* result = String::cast(object);
      Flatten(this, result, 0, length(), 0);
      cs->set_first(result);
      cs->set_second(Heap::empty_string());
      return this;
    }
    default:
      return this;
  }
}


void String::StringShortPrint(StringStream* accumulator) {
  int len = length();
  if (len > kMaxMediumStringSize) {
    accumulator->Add("<Very long string[%u]>", len);
    return;
  }

  if (!LooksValid()) {
    accumulator->Add("<Invalid String>");
    return;
  }

  StringInputBuffer buf(this);

  bool truncated = false;
  if (len > 1024) {
    len = 1024;
    truncated = true;
  }
  bool ascii = true;
  for (int i = 0; i < len; i++) {
    int c = buf.GetNext();

    if (c < 32 || c >= 127) {
      ascii = false;
    }
  }
  buf.Reset(this);
  if (ascii) {
    accumulator->Add("<String[%u]: ", length());
    for (int i = 0; i < len; i++) {
      accumulator->Put(buf.GetNext());
    }
    accumulator->Put('>');
  } else {
    // Backslash indicates that the string contains control
    // characters and that backslashes are therefore escaped.
    accumulator->Add("<String[%u]\\: ", length());
    for (int i = 0; i < len; i++) {
      int c = buf.GetNext();
      if (c == '\n') {
        accumulator->Add("\\n");
      } else if (c == '\r') {
        accumulator->Add("\\r");
      } else if (c == '\\') {
        accumulator->Add("\\\\");
      } else if (c < 32 || c > 126) {
        accumulator->Add("\\x%02x", c);
      } else {
        accumulator->Put(c);
      }
    }
    if (truncated) {
      accumulator->Put('.');
      accumulator->Put('.');
      accumulator->Put('.');
    }
    accumulator->Put('>');
  }
  return;
}


void JSObject::JSObjectShortPrint(StringStream* accumulator) {
  switch (map()->instance_type()) {
    case JS_ARRAY_TYPE: {
      double length = JSArray::cast(this)->length()->Number();
      accumulator->Add("<JS array[%u]>", static_cast<uint32_t>(length));
      break;
    }
    case JS_FUNCTION_TYPE: {
      Object* fun_name = JSFunction::cast(this)->shared()->name();
      bool printed = false;
      if (fun_name->IsString()) {
        String* str = String::cast(fun_name);
        if (str->length() > 0) {
          accumulator->Add("<JS Function ");
          accumulator->Put(str);
          accumulator->Put('>');
          printed = true;
        }
      }
      if (!printed) {
        accumulator->Add("<JS Function>");
      }
      break;
    }
    // All other JSObjects are rather similar to each other (JSObject,
    // JSGlobalObject, JSUndetectableObject, JSValue).
    default: {
      Object* constructor = map()->constructor();
      bool printed = false;
      if (constructor->IsHeapObject() &&
          !Heap::Contains(HeapObject::cast(constructor))) {
        accumulator->Add("!!!INVALID CONSTRUCTOR!!!");
      } else {
        bool global_object = IsJSGlobalObject();
        if (constructor->IsJSFunction()) {
          if (!Heap::Contains(JSFunction::cast(constructor)->shared())) {
            accumulator->Add("!!!INVALID SHARED ON CONSTRUCTOR!!!");
          } else {
            Object* constructor_name =
                JSFunction::cast(constructor)->shared()->name();
            if (constructor_name->IsString()) {
              String* str = String::cast(constructor_name);
              if (str->length() > 0) {
                bool vowel = AnWord(str);
                accumulator->Add("<%sa%s ",
                       global_object ? "JS Global Object: " : "",
                       vowel ? "n" : "");
                accumulator->Put(str);
                accumulator->Put('>');
                printed = true;
              }
            }
          }
        }
        if (!printed) {
          accumulator->Add("<JS %sObject", global_object ? "Global " : "");
        }
      }
      if (IsJSValue()) {
        accumulator->Add(" value = ");
        JSValue::cast(this)->value()->ShortPrint(accumulator);
      }
      accumulator->Put('>');
      break;
    }
  }
}


void HeapObject::HeapObjectShortPrint(StringStream* accumulator) {
  // if (!Heap::InNewSpace(this)) PrintF("*", this);
  if (!Heap::Contains(this)) {
    accumulator->Add("!!!INVALID POINTER!!!");
    return;
  }
  if (!Heap::Contains(map())) {
    accumulator->Add("!!!INVALID MAP!!!");
    return;
  }

  accumulator->Add("%p ", this);

  if (IsString()) {
    String::cast(this)->StringShortPrint(accumulator);
    return;
  }
  if (IsJSObject()) {
    JSObject::cast(this)->JSObjectShortPrint(accumulator);
    return;
  }
  switch (map()->instance_type()) {
    case MAP_TYPE:
      accumulator->Add("<Map>");
      break;
    case FIXED_ARRAY_TYPE:
      accumulator->Add("<FixedArray[%u]>", FixedArray::cast(this)->length());
      break;
    case BYTE_ARRAY_TYPE:
      accumulator->Add("<ByteArray[%u]>", ByteArray::cast(this)->length());
      break;
    case SHARED_FUNCTION_INFO_TYPE:
      accumulator->Add("<SharedFunctionInfo>");
      break;
#define MAKE_STRUCT_CASE(NAME, Name, name) \
  case NAME##_TYPE:                        \
    accumulator->Add(#Name);               \
    break;
  STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
    case CODE_TYPE:
      accumulator->Add("<Code>");
      break;
    case ODDBALL_TYPE: {
      if (IsUndefined())
        accumulator->Add("<undefined>");
      else if (IsTheHole())
        accumulator->Add("<the hole>");
      else if (IsNull())
        accumulator->Add("<null>");
      else if (IsTrue())
        accumulator->Add("<true>");
      else if (IsFalse())
        accumulator->Add("<false>");
      else
        accumulator->Add("<Odd Oddball>");
      break;
    }
    case HEAP_NUMBER_TYPE:
      accumulator->Add("<Number: ");
      HeapNumber::cast(this)->HeapNumberPrint(accumulator);
      accumulator->Put('>');
      break;
    case PROXY_TYPE:
      accumulator->Add("<Proxy>");
      break;
    default:
      accumulator->Add("<Other heap object (%d)>", map()->instance_type());
      break;
  }
}


int HeapObject::SlowSizeFromMap(Map* map) {
  // Avoid calling functions such as FixedArray::cast during GC, which
  // read map pointer of this object again.
  InstanceType instance_type = map->instance_type();

  if (instance_type < FIRST_NONSTRING_TYPE
      && (reinterpret_cast<String*>(this)->map_representation_tag(map)
          == kSeqStringTag)) {
    if (reinterpret_cast<String*>(this)->is_ascii_map(map)) {
      return reinterpret_cast<AsciiString*>(this)->AsciiStringSize(map);
    } else {
      return reinterpret_cast<TwoByteString*>(this)->TwoByteStringSize(map);
    }
  }

  switch (instance_type) {
    case FIXED_ARRAY_TYPE:
      return reinterpret_cast<FixedArray*>(this)->FixedArraySize();
    case BYTE_ARRAY_TYPE:
      return reinterpret_cast<ByteArray*>(this)->ByteArraySize();
    case CODE_TYPE:
      return reinterpret_cast<Code*>(this)->CodeSize();
    case MAP_TYPE:
      return Map::kSize;
    default:
      return map->instance_size();
  }
}


void HeapObject::Iterate(ObjectVisitor* v) {
  // Handle header
  IteratePointer(v, kMapOffset);
  // Handle object body
  Map* m = map();
  IterateBody(m->instance_type(), SizeFromMap(m), v);
}


void HeapObject::IterateBody(InstanceType type, int object_size,
                             ObjectVisitor* v) {
  // Avoiding <Type>::cast(this) because it accesses the map pointer field.
  // During GC, the map pointer field is encoded.
  if (type < FIRST_NONSTRING_TYPE) {
    switch (type & kStringRepresentationMask) {
      case kSeqStringTag:
        break;
      case kConsStringTag:
        reinterpret_cast<ConsString*>(this)->ConsStringIterateBody(v);
        break;
      case kSlicedStringTag:
        reinterpret_cast<SlicedString*>(this)->SlicedStringIterateBody(v);
        break;
    }
    return;
  }

  switch (type) {
    case FIXED_ARRAY_TYPE:
      reinterpret_cast<FixedArray*>(this)->FixedArrayIterateBody(v);
      break;
    case JS_OBJECT_TYPE:
    case JS_VALUE_TYPE:
    case JS_ARRAY_TYPE:
    case JS_FUNCTION_TYPE:
    case JS_GLOBAL_OBJECT_TYPE:
      reinterpret_cast<JSObject*>(this)->JSObjectIterateBody(object_size, v);
      break;
    case JS_BUILTINS_OBJECT_TYPE:
      reinterpret_cast<JSObject*>(this)->JSObjectIterateBody(object_size, v);
      break;
    case ODDBALL_TYPE:
      reinterpret_cast<Oddball*>(this)->OddballIterateBody(v);
      break;
    case PROXY_TYPE:
      reinterpret_cast<Proxy*>(this)->ProxyIterateBody(v);
      break;
    case MAP_TYPE:
      reinterpret_cast<Map*>(this)->MapIterateBody(v);
      break;
    case CODE_TYPE:
      reinterpret_cast<Code*>(this)->CodeIterateBody(v);
      break;
    case HEAP_NUMBER_TYPE:
    case FILLER_TYPE:
    case BYTE_ARRAY_TYPE:
      break;
    case SHARED_FUNCTION_INFO_TYPE: {
      SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(this);
      shared->SharedFunctionInfoIterateBody(v);
      break;
    }
#define MAKE_STRUCT_CASE(NAME, Name, name) \
        case NAME##_TYPE:
      STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
      IterateStructBody(object_size, v);
      break;
    default:
      PrintF("Unknown type: %d\n", type);
      UNREACHABLE();
  }
}


void HeapObject::IterateStructBody(int object_size, ObjectVisitor* v) {
  IteratePointers(v, HeapObject::kSize, object_size);
}


Object* HeapNumber::HeapNumberToBoolean() {
  // NaN, +0, and -0 should return the false object
  switch (fpclassify(value())) {
    case FP_NAN:  // fall through
    case FP_ZERO: return Heap::false_value();
    default: return Heap::true_value();
  }
}


void HeapNumber::HeapNumberPrint() {
  PrintF("%.16g", Number());
}


void HeapNumber::HeapNumberPrint(StringStream* accumulator) {
  // The Windows version of vsnprintf can allocate when printing a %g string
  // into a buffer that may not be big enough.  We don't want random memory
  // allocation when producing post-crash stack traces, so we print into a
  // buffer that is plenty big enough for any floating point number, then
  // print that using vsnprintf (which may truncate but never allocate if
  // there is no more space in the buffer).
  char buffer[100];
  OS::SNPrintF(buffer, sizeof(buffer), "%.16g", Number());
  accumulator->Add("%s", buffer);
}


String* JSObject::class_name() {
  if (IsJSFunction()) return Heap::function_class_symbol();
  // If the constructor is not present "Object" is returned.
  String* result = Heap::Object_symbol();
  if (map()->constructor()->IsJSFunction()) {
    JSFunction* constructor = JSFunction::cast(map()->constructor());
    return String::cast(constructor->shared()->instance_class_name());
  }
  return result;
}


void JSObject::JSObjectIterateBody(int object_size, ObjectVisitor* v) {
  // Iterate over all fields in the body. Assumes all are Object*.
  IteratePointers(v, kPropertiesOffset, object_size);
}


Object* JSObject::Copy(PretenureFlag pretenure) {
  // Copy the elements and properties.
  Object* elem = FixedArray::cast(elements())->Copy();
  if (elem->IsFailure()) return elem;
  Object* prop = properties()->Copy();
  if (prop->IsFailure()) return prop;

  // Make the clone.
  Object* clone = (pretenure == NOT_TENURED) ?
      Heap::Allocate(map(), NEW_SPACE) :
      Heap::Allocate(map(), OLD_SPACE);
  if (clone->IsFailure()) return clone;
  JSObject::cast(clone)->CopyBody(this);

  // Set the new elements and properties.
  JSObject::cast(clone)->set_elements(FixedArray::cast(elem));
  JSObject::cast(clone)->set_properties(FixedArray::cast(prop));

  // NOTE: Copy is only used for copying objects and functions from
  // boilerplates. This means if we have a function the prototype is
  // not present.
  ASSERT(!IsJSFunction() || !JSFunction::cast(clone)->has_prototype());

  // Return the new clone.
  return clone;
}


Object* JSObject::AddFastPropertyUsingMap(Map* new_map,
                                          String* name,
                                          Object* value) {
  int index = new_map->PropertyIndexFor(name);
  if (map()->unused_property_fields() > 0) {
    ASSERT(index < properties()->length());
    properties()->set(index, value);
  } else {
    ASSERT(map()->unused_property_fields() == 0);
    int new_unused = new_map->unused_property_fields();
    Object* values =
        properties()->CopySize(properties()->length() + new_unused + 1);
    if (values->IsFailure()) return values;
    FixedArray::cast(values)->set(index, value);
    set_properties(FixedArray::cast(values));
  }
  set_map(new_map);
  return value;
}


Object* JSObject::AddFastProperty(String* name,
                                  Object* value,
                                  PropertyAttributes attributes) {
  // Normalize the object if the name is not a real identifier.
  StringInputBuffer buffer(name);
  if (!Scanner::IsIdentifier(&buffer)) {
    Object* obj = NormalizeProperties();
    if (obj->IsFailure()) return obj;
    return AddSlowProperty(name, value, attributes);
  }

  // Compute the new index for new field.
  int index = map()->NextFreePropertyIndex();

  // Allocate new instance descriptors with (name, index) added
  FieldDescriptor fd(name, index, attributes);
  Object* new_descriptors =
      map()->instance_descriptors()->CopyInsert(&fd, true);
  if (new_descriptors->IsFailure()) return new_descriptors;

  // Only allow map transition if the object's map is NOT equal to the
  // global object_function's map and there is not a transition for name.
  bool allow_map_transition =
        !map()->instance_descriptors()->Contains(name) &&
        (Top::context()->global_context()->object_function()->map() != map());

  if (map()->unused_property_fields() > 0) {
    ASSERT(index < properties()->length());
    // Allocate a new map for the object.
    Object* new_map = map()->Copy();
    if (new_map->IsFailure()) return new_map;
    if (allow_map_transition) {
      // Allocate new instance descriptors for the old map with map transition.
      MapTransitionDescriptor d(name, Map::cast(new_map), attributes);
      Object* old_descriptors = map()->instance_descriptors()->CopyInsert(&d);
      if (old_descriptors->IsFailure()) return old_descriptors;
      // We have now allocate all the necessary object and change can be
      // applied.
      map()->set_instance_descriptors(DescriptorArray::cast(old_descriptors));
    }
    Map::cast(new_map)->
      set_instance_descriptors(DescriptorArray::cast(new_descriptors));
    Map::cast(new_map)->
      set_unused_property_fields(map()->unused_property_fields() - 1);
    set_map(Map::cast(new_map));
    properties()->set(index, value);
  } else {
    ASSERT(map()->unused_property_fields() == 0);

    static const int kFastNofProperties = 8;
    if (properties()->length() > kFastNofProperties) {
      Object* obj = NormalizeProperties();
      if (obj->IsFailure()) return obj;
      return AddSlowProperty(name, value, attributes);
    }

    static const int kExtraFields = 3;
    // Make room for the new value
    Object* values =
        properties()->CopySize(properties()->length() + kExtraFields);
    if (values->IsFailure()) return values;
    FixedArray::cast(values)->set(index, value);

    // Allocate a new map for the object.
    Object* new_map = map()->Copy();
    if (new_map->IsFailure()) return new_map;

    if (allow_map_transition) {
      MapTransitionDescriptor d(name, Map::cast(new_map), attributes);
      // Allocate a new instance descriptors for the old map with map
      // transition.
      Object* old_descriptors = map()->instance_descriptors()->CopyInsert(&d);
      if (old_descriptors->IsFailure()) return old_descriptors;

      // We have now allocate all the necessary object and change can be
      // applied.
      map()->set_instance_descriptors(DescriptorArray::cast(old_descriptors));
    }

    Map::cast(new_map)->
      set_instance_descriptors(DescriptorArray::cast(new_descriptors));
    Map::cast(new_map)->
      set_unused_property_fields(kExtraFields - 1);
    set_map(Map::cast(new_map));
    set_properties(FixedArray::cast(values));
  }

  return value;
}


Object* JSObject::AddConstantFunctionProperty(String* name,
                                              JSFunction* function,
                                              PropertyAttributes attributes) {
  // Allocate new instance descriptors with (name, function) added
  ConstantFunctionDescriptor d(name, function, attributes);
  Object* new_descriptors =
      map()->instance_descriptors()->CopyInsert(&d, true);
  if (new_descriptors->IsFailure()) return new_descriptors;

  // Allocate a new map for the object.
  Object* new_map = map()->Copy();
  if (new_map->IsFailure()) return new_map;

  DescriptorArray* descriptors = DescriptorArray::cast(new_descriptors);
  Map::cast(new_map)->set_instance_descriptors(descriptors);
  set_map(Map::cast(new_map));

  return function;
}


Object* JSObject::ReplaceConstantFunctionProperty(String* name,
                                                  Object* value) {
  // There are two situations to handle here:
  // 1: Replace a constant function with another function.
  // 2: Replace a constant function with an object.
  if (value->IsJSFunction()) {
    JSFunction* function = JSFunction::cast(value);

    // Allocate new instance descriptors with (name, function) added
    Object* new_descriptors = map()->instance_descriptors()->Copy();
    if (new_descriptors->IsFailure()) return new_descriptors;

    // Replace the function entry
    DescriptorArray* p = DescriptorArray::cast(new_descriptors);
    for (DescriptorReader r(p); !r.eos(); r.advance()) {
      if (r.Equals(name)) r.ReplaceConstantFunction(function);
    }

    // Allocate a new map for the object.
    Object* new_map = map()->Copy();
    if (new_map->IsFailure()) return new_map;

    Map::cast(new_map)->
      set_instance_descriptors(DescriptorArray::cast(new_descriptors));
    set_map(Map::cast(new_map));
  } else {
    // Allocate new instance descriptors with updated property index.
    int index = map()->NextFreePropertyIndex();
    Object* new_descriptors =
        map()->instance_descriptors()->CopyReplace(name, index, NONE);
    if (new_descriptors->IsFailure()) return new_descriptors;

    if (map()->unused_property_fields() > 0) {
      ASSERT(index < properties()->length());

      // Allocate a new map for the object.
      Object* new_map = map()->Copy();
      if (new_map->IsFailure()) return new_map;

      Map::cast(new_map)->
        set_instance_descriptors(DescriptorArray::cast(new_descriptors));
      Map::cast(new_map)->
        set_unused_property_fields(map()->unused_property_fields()-1);
      set_map(Map::cast(new_map));
      properties()->set(index, value);
    } else {
      ASSERT(map()->unused_property_fields() == 0);
      static const int kFastNofProperties = 20;
      if (properties()->length() > kFastNofProperties) {
        Object* obj = NormalizeProperties();
        if (obj->IsFailure()) return obj;
        return SetProperty(name, value, NONE);
      }

      static const int kExtraFields = 5;
      // Make room for the more properties.
      Object* values =
          properties()->CopySize(properties()->length() + kExtraFields);
      if (values->IsFailure()) return values;
      FixedArray::cast(values)->set(index, value);

      // Allocate a new map for the object.
      Object* new_map = map()->Copy();
      if (new_map->IsFailure()) return new_map;

      Map::cast(new_map)->
        set_instance_descriptors(DescriptorArray::cast(new_descriptors));
      Map::cast(new_map)->
        set_unused_property_fields(kExtraFields - 1);
      set_map(Map::cast(new_map));
      set_properties(FixedArray::cast(values));
    }
  }
  return value;
}


// Add property in slow mode
Object* JSObject::AddSlowProperty(String* name,
                                  Object* value,
                                  PropertyAttributes attributes) {
  PropertyDetails details = PropertyDetails(attributes, NORMAL);
  Object* result = property_dictionary()->AddStringEntry(name, value, details);
  if (result->IsFailure()) return result;
  if (property_dictionary() != result) {
     set_properties(Dictionary::cast(result));
  }
  return value;
}


Object* JSObject::AddProperty(String* name,
                              Object* value,
                              PropertyAttributes attributes) {
  if (HasFastProperties()) {
    // Ensure the descriptor array does not get too big.
    if (map()->instance_descriptors()->number_of_descriptors() <
        DescriptorArray::kMaxNumberOfDescriptors) {
      if (value->IsJSFunction()) {
        return AddConstantFunctionProperty(name,
                                           JSFunction::cast(value),
                                           attributes);
      } else {
        return AddFastProperty(name, value, attributes);
      }
    } else {
      // Normalize the object to prevent very large instance descriptors.
      // This eliminates unwanted N^2 allocation and lookup behavior.
      Object* obj = NormalizeProperties();
      if (obj->IsFailure()) return obj;
    }
  }
  return AddSlowProperty(name, value, attributes);
}


Object* JSObject::SetPropertyPostInterceptor(String* name,
                                             Object* value,
                                             PropertyAttributes attributes) {
  // Check local property, ignore interceptor.
  LookupResult result;
  LocalLookupRealNamedProperty(name, &result);
  if (result.IsValid()) return SetProperty(&result, name, value, attributes);
  // Add real property.
  return AddProperty(name, value, attributes);
}


Object* JSObject::SetPropertyWithInterceptor(String* name,
                                             Object* value,
                                             PropertyAttributes attributes) {
  HandleScope scope;
  Handle<JSObject> this_handle(this);
  Handle<String> name_handle(name);
  Handle<Object> value_handle(value);
  Handle<InterceptorInfo> interceptor(GetNamedInterceptor());
  if (!interceptor->setter()->IsUndefined()) {
    Handle<Object> data_handle(interceptor->data());
    LOG(ApiNamedPropertyAccess("interceptor-named-set", this, name));
    v8::AccessorInfo info(v8::Utils::ToLocal(this_handle),
                          v8::Utils::ToLocal(data_handle),
                          v8::Utils::ToLocal(this_handle));
    v8::NamedPropertySetter setter =
        v8::ToCData<v8::NamedPropertySetter>(interceptor->setter());
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      Handle<Object> value_unhole(value->IsTheHole() ?
                                  Heap::undefined_value() :
                                  value);
      result = setter(v8::Utils::ToLocal(name_handle),
                      v8::Utils::ToLocal(value_unhole),
                      info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!result.IsEmpty()) return *value_handle;
  }
  Object* raw_result = this_handle->SetPropertyPostInterceptor(*name_handle,
                                                               *value_handle,
                                                               attributes);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


Object* JSObject::SetProperty(String* name,
                              Object* value,
                              PropertyAttributes attributes) {
  LookupResult result;
  LocalLookup(name, &result);
  return SetProperty(&result, name, value, attributes);
}


Object* JSObject::SetPropertyWithCallback(Object* structure,
                                          String* name,
                                          Object* value,
                                          JSObject* holder) {
  HandleScope scope;

  // We should never get here to initialize a const with the hole
  // value since a const declaration would conflict with the setter.
  ASSERT(!value->IsTheHole());
  Handle<Object> value_handle(value);

  // To accommodate both the old and the new api we switch on the
  // data structure used to store the callbacks.  Eventually proxy
  // callbacks should be phased out.
  if (structure->IsProxy()) {
    AccessorDescriptor* callback =
        reinterpret_cast<AccessorDescriptor*>(Proxy::cast(structure)->proxy());
    Object* obj = (callback->setter)(this,  value, callback->data);
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (obj->IsFailure()) return obj;
    return *value_handle;
  }

  if (structure->IsAccessorInfo()) {
    // api style callbacks
    AccessorInfo* data = AccessorInfo::cast(structure);
    Object* call_obj = data->setter();
    v8::AccessorSetter call_fun = v8::ToCData<v8::AccessorSetter>(call_obj);
    if (call_fun == NULL) return value;
    Handle<JSObject> self(this);
    Handle<JSObject> holder_handle(JSObject::cast(holder));
    Handle<String> key(name);
    Handle<Object> fun_data(data->data());
    LOG(ApiNamedPropertyAccess("store", this, name));
    v8::AccessorInfo info(v8::Utils::ToLocal(self),
                          v8::Utils::ToLocal(fun_data),
                          v8::Utils::ToLocal(holder_handle));
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      call_fun(v8::Utils::ToLocal(key),
               v8::Utils::ToLocal(value_handle),
               info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    return *value_handle;
  }

  if (structure->IsFixedArray()) {
    Object* setter = FixedArray::cast(structure)->get(kSetterIndex);
    if (setter->IsJSFunction()) {
      Handle<JSFunction> fun(JSFunction::cast(setter));
      Handle<JSObject> self(this);
      bool has_pending_exception;
      Object** argv[] = { value_handle.location() };
      Execution::Call(fun, self, 1, argv, &has_pending_exception);
      // Check for pending exception and return the result.
      if (has_pending_exception) return Failure::Exception();
    } else {
      Handle<String> key(name);
      Handle<Object> holder_handle(holder);
      Handle<Object> args[2] = { key, holder_handle };
      return Top::Throw(*Factory::NewTypeError("no_setter_in_callback",
                                               HandleVector(args, 2)));
    }
    return *value_handle;
  }

  UNREACHABLE();
  return 0;
}


void JSObject::LookupCallbackSetterInPrototypes(String* name,
                                                LookupResult* result) {
  for (Object* pt = GetPrototype();
       pt != Heap::null_value();
       pt = pt->GetPrototype()) {
    JSObject::cast(pt)->LocalLookupRealNamedProperty(name, result);
    if (result->IsValid()) {
      if (!result->IsTransitionType() && result->IsReadOnly()) {
        result->NotFound();
        return;
      }
      if (result->type() == CALLBACKS) {
        return;
      }
    }
  }
  result->NotFound();
}


void JSObject::LookupInDescriptor(String* name, LookupResult* result) {
  DescriptorArray* descriptors = map()->instance_descriptors();
  int number = descriptors->Search(name);
  if (number != DescriptorArray::kNotFound) {
    result->DescriptorResult(this, descriptors->GetDetails(number), number);
  } else {
    result->NotFound();
  }
}


void JSObject::LocalLookupRealNamedProperty(String* name,
                                            LookupResult* result) {
  if (HasFastProperties()) {
    LookupInDescriptor(name, result);
    if (result->IsValid()) {
      ASSERT(result->holder() == this && result->type() != NORMAL);
      // Disallow caching for uninitialized constants. These can only
      // occur as fields.
      if (result->IsReadOnly() && result->type() == FIELD &&
          properties()->get(result->GetFieldIndex())->IsTheHole()) {
        result->DisallowCaching();
      }
      return;
    }
  } else {
    int entry = property_dictionary()->FindStringEntry(name);
    if (entry != -1) {
      // Make sure to disallow caching for uninitialized constants
      // found in the dictionary-mode objects.
      if (property_dictionary()->ValueAt(entry)->IsTheHole()) {
        result->DisallowCaching();
      }
      result->DictionaryResult(this, entry);
      return;
    }
    // Slow case object skipped during lookup. Do not use inline caching.
    result->DisallowCaching();
  }
  result->NotFound();
}


void JSObject::LookupRealNamedProperty(String* name, LookupResult* result) {
  LocalLookupRealNamedProperty(name, result);
  if (result->IsProperty()) return;

  LookupRealNamedPropertyInPrototypes(name, result);
}


void JSObject::LookupRealNamedPropertyInPrototypes(String* name,
                                                   LookupResult* result) {
  for (Object* pt = GetPrototype();
       pt != Heap::null_value();
       pt = JSObject::cast(pt)->GetPrototype()) {
    JSObject::cast(pt)->LocalLookupRealNamedProperty(name, result);
    if (result->IsValid()) {
      switch (result->type()) {
        case NORMAL:
        case FIELD:
        case CONSTANT_FUNCTION:
        case CALLBACKS:
          return;
        default: break;
      }
    }
  }
  result->NotFound();
}


// We only need to deal with CALLBACKS and INTERCEPTORS
Object* JSObject::SetPropertyWithFailedAccessCheck(LookupResult* result,
                                                   String* name,
                                                   Object* value) {
  if (!result->IsProperty()) {
    LookupCallbackSetterInPrototypes(name, result);
  }

  if (result->IsProperty()) {
    if (!result->IsReadOnly()) {
      switch (result->type()) {
        case CALLBACKS: {
          Object* obj = result->GetCallbackObject();
          if (obj->IsAccessorInfo()) {
            AccessorInfo* info = AccessorInfo::cast(obj);
            if (info->all_can_write()) {
              return SetPropertyWithCallback(result->GetCallbackObject(),
                                             name,
                                             value,
                                             result->holder());
            }
          }
          break;
        }
        case INTERCEPTOR: {
          // Try lookup real named properties. Note that only property can be
          // set is callbacks marked as ALL_CAN_WRITE on the prototype chain.
          LookupResult r;
          LookupRealNamedProperty(name, &r);
          if (r.IsProperty()) {
            return SetPropertyWithFailedAccessCheck(&r, name, value);
          }
          break;
        }
        default: {
          break;
        }
      }
    }
  }

  Top::ReportFailedAccessCheck(this, v8::ACCESS_SET);
  return value;
}


Object* JSObject::SetProperty(LookupResult* result,
                              String* name,
                              Object* value,
                              PropertyAttributes attributes) {
  // Make sure that the top context does not change when doing callbacks or
  // interceptor calls.
  AssertNoContextChange ncc;

  // Check access rights if needed.
  if (IsAccessCheckNeeded()
    && !Top::MayNamedAccess(this, name, v8::ACCESS_SET)) {
    return SetPropertyWithFailedAccessCheck(result, name, value);
  }

  if (result->IsValid()) {
    if (!result->IsLoaded()) {
      return SetLazyProperty(result, name, value, attributes);
    }
    if (result->IsReadOnly() && !result->IsTransitionType()) return value;
    switch (result->type()) {
      case NORMAL:
        property_dictionary()->ValueAtPut(result->GetDictionaryEntry(), value);
        return value;
      case FIELD:
        properties()->set(result->GetFieldIndex(), value);
        return value;
      case MAP_TRANSITION:
        if (attributes == result->GetAttributes()) {
          // Only use map transition if attributes matches.
          return AddFastPropertyUsingMap(result->GetTransitionMap(),
                                         name,
                                         value);
        } else {
          return AddFastProperty(name, value, attributes);
        }
      case CONSTANT_FUNCTION:
        if (value == result->GetConstantFunction()) return value;
        // Only replace the function if necessary.
        return ReplaceConstantFunctionProperty(name, value);
      case CALLBACKS:
        return SetPropertyWithCallback(result->GetCallbackObject(),
                                       name,
                                       value,
                                       result->holder());
      case INTERCEPTOR:
        return SetPropertyWithInterceptor(name, value, attributes);
      case CONSTANT_TRANSITION:
        break;
    }
  }

  // We could not find a local property so let's check whether there is an
  // accessor that wants to handle the property.
  LookupResult accessor_result;
  LookupCallbackSetterInPrototypes(name, &accessor_result);
  if (accessor_result.IsValid()) {
    return SetPropertyWithCallback(accessor_result.GetCallbackObject(),
                                   name,
                                   value,
                                   accessor_result.holder());
  }

  // The property was not found
  return AddProperty(name, value, attributes);
}


// Set a real local property, even if it is READ_ONLY.  If the property is not
// present, add it with attributes NONE.  This code is the same as in
// SetProperty, except for the check for IsReadOnly and the check for a
// callback setter.
Object* JSObject::IgnoreAttributesAndSetLocalProperty(String* name,
                                                      Object* value) {
  // Make sure that the top context does not change when doing callbacks or
  // interceptor calls.
  AssertNoContextChange ncc;

  LookupResult result;
  LocalLookup(name, &result);

  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, name, v8::ACCESS_SET)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_SET);
    return value;
  }

  if (result.IsValid()) {
    switch (result.type()) {
      case NORMAL:
        property_dictionary()->ValueAtPut(result.GetDictionaryEntry(), value);
        return value;
      case FIELD:
        properties()->set(result.GetFieldIndex(), value);
        return value;
      case MAP_TRANSITION:
        return AddFastPropertyUsingMap(result.GetTransitionMap(), name, value);
      case CONSTANT_FUNCTION:
        return ReplaceConstantFunctionProperty(name, value);
      case CALLBACKS:
        return SetPropertyWithCallback(result.GetCallbackObject(), name, value,
                                       result.holder());
      case INTERCEPTOR:
        return SetPropertyWithInterceptor(name, value, NONE);
      case CONSTANT_TRANSITION:
        break;
    }
  }

  // The property was not found
  return AddProperty(name, value, NONE);
}


PropertyAttributes JSObject::GetPropertyAttributePostInterceptor(
      JSObject* receiver,
      String* name,
      bool continue_search) {
  // Check local property, ignore interceptor.
  LookupResult result;
  LocalLookupRealNamedProperty(name, &result);
  if (result.IsProperty()) return result.GetAttributes();

  if (continue_search) {
    // Continue searching via the prototype chain.
    Object* pt = GetPrototype();
    if (pt != Heap::null_value()) {
      return JSObject::cast(pt)->
        GetPropertyAttributeWithReceiver(receiver, name);
    }
  }
  return ABSENT;
}


PropertyAttributes JSObject::GetPropertyAttributeWithInterceptor(
      JSObject* receiver,
      String* name,
      bool continue_search) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;

  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetNamedInterceptor());
  Handle<JSObject> receiver_handle(receiver);
  Handle<JSObject> holder_handle(this);
  Handle<String> name_handle(name);
  Handle<Object> data_handle(interceptor->data());
  v8::AccessorInfo info(v8::Utils::ToLocal(receiver_handle),
                        v8::Utils::ToLocal(data_handle),
                        v8::Utils::ToLocal(holder_handle));
  if (!interceptor->query()->IsUndefined()) {
    v8::NamedPropertyQuery query =
        v8::ToCData<v8::NamedPropertyQuery>(interceptor->query());
    LOG(ApiNamedPropertyAccess("interceptor-named-has", *holder_handle, name));
    v8::Handle<v8::Boolean> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = query(v8::Utils::ToLocal(name_handle), info);
    }
    if (!result.IsEmpty()) {
      // Convert the boolean result to a property attribute
      // specification.
      return result->IsTrue() ? NONE : ABSENT;
    }
  } else if (!interceptor->getter()->IsUndefined()) {
    v8::NamedPropertyGetter getter =
        v8::ToCData<v8::NamedPropertyGetter>(interceptor->getter());
    LOG(ApiNamedPropertyAccess("interceptor-named-get-has", this, name));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = getter(v8::Utils::ToLocal(name_handle), info);
    }
    if (!result.IsEmpty()) return NONE;
  }
  return holder_handle->GetPropertyAttributePostInterceptor(*receiver_handle,
                                                            *name_handle,
                                                            continue_search);
}


PropertyAttributes JSObject::GetPropertyAttributeWithReceiver(
      JSObject* receiver,
      String* key) {
  uint32_t index;
  if (key->AsArrayIndex(&index)) {
    if (HasElementWithReceiver(receiver, index)) return NONE;
    return ABSENT;
  }
  // Named property.
  LookupResult result;
  Lookup(key, &result);
  return GetPropertyAttribute(receiver, &result, key, true);
}


PropertyAttributes JSObject::GetPropertyAttribute(JSObject* receiver,
                                                  LookupResult* result,
                                                  String* name,
                                                  bool continue_search) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, name, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return ABSENT;
  }
  if (result->IsValid()) {
    switch (result->type()) {
      case NORMAL:  // fall through
      case FIELD:
      case CONSTANT_FUNCTION:
      case CALLBACKS:
        return result->GetAttributes();
      case INTERCEPTOR:
        return result->holder()->
          GetPropertyAttributeWithInterceptor(receiver, name, continue_search);
      default:
        break;
    }
  }
  return ABSENT;
}


PropertyAttributes JSObject::GetLocalPropertyAttribute(String* name) {
  // Check whether the name is an array index.
  uint32_t index;
  if (name->AsArrayIndex(&index)) {
    if (HasLocalElement(index)) return NONE;
    return ABSENT;
  }
  // Named property.
  LookupResult result;
  LocalLookup(name, &result);
  return GetPropertyAttribute(this, &result, name, false);
}


Object* JSObject::NormalizeProperties() {
  if (!HasFastProperties()) return this;

  // Allocate new content
  Object* obj =
      Dictionary::Allocate(map()->NumberOfDescribedProperties() * 2 + 4);
  if (obj->IsFailure()) return obj;
  Dictionary* dictionary = Dictionary::cast(obj);

  for (DescriptorReader r(map()->instance_descriptors());
       !r.eos();
       r.advance()) {
    PropertyDetails details = r.GetDetails();
    switch (details.type()) {
      case CONSTANT_FUNCTION: {
        PropertyDetails d =
            PropertyDetails(details.attributes(), NORMAL, details.index());
        Object* value = r.GetConstantFunction();
        Object* result = dictionary->AddStringEntry(r.GetKey(), value, d);
        if (result->IsFailure()) return result;
        dictionary = Dictionary::cast(result);
        break;
      }
      case FIELD: {
        PropertyDetails d =
            PropertyDetails(details.attributes(), NORMAL, details.index());
        Object* value = properties()->get(r.GetFieldIndex());
        Object* result = dictionary->AddStringEntry(r.GetKey(), value, d);
        if (result->IsFailure()) return result;
        dictionary = Dictionary::cast(result);
        break;
      }
      case CALLBACKS: {
        PropertyDetails d =
            PropertyDetails(details.attributes(), CALLBACKS, details.index());
        Object* value = r.GetCallbacksObject();
        Object* result = dictionary->AddStringEntry(r.GetKey(), value, d);
        if (result->IsFailure()) return result;
        dictionary = Dictionary::cast(result);
        break;
      }
      default:
        break;
    }
  }

  // Copy the next enumeration index from instance descriptor.
  int index = map()->instance_descriptors()->NextEnumerationIndex();
  dictionary->SetNextEnumerationIndex(index);

  // Descriptors with type MAP_TRANSITION is ignored.

  // Allocate new map.
  obj = map()->Copy();
  if (obj->IsFailure()) return obj;

  set_map(Map::cast(obj));
  map()->
    set_instance_descriptors(DescriptorArray::cast(Heap::empty_fixed_array()));

  // We have now allocate all the necessary object and change can be applied.
  map()->set_unused_property_fields(0);
  set_properties(dictionary);

  Counters::props_to_dictionary.Increment();

#ifdef DEBUG
  if (FLAG_trace_normalization) {
    PrintF("Object properties have been normalized:\n");
    Print();
  }
#endif
  return this;
}


Object* JSObject::TransformToFastProperties(int unused_property_fields) {
  if (HasFastProperties()) return this;
  return property_dictionary()->
    TransformPropertiesToFastFor(this, unused_property_fields);
}


Object* JSObject::NormalizeElements() {
  if (!HasFastElements()) return this;

  // Get number of entries.
  FixedArray* array = FixedArray::cast(elements());

  // Compute the effective length.
  int length = IsJSArray() ?
               Smi::cast(JSArray::cast(this)->length())->value() :
               array->length();
  Object* obj = Dictionary::Allocate(length);
  if (obj->IsFailure()) return obj;
  Dictionary* dictionary = Dictionary::cast(obj);
  // Copy entries.
  for (int i = 0; i < length; i++) {
    Object* value = array->get(i);
    if (!value->IsTheHole()) {
      PropertyDetails details = PropertyDetails(NONE, NORMAL);
      Object* result = dictionary->AddNumberEntry(i, array->get(i), details);
      if (result->IsFailure()) return result;
      dictionary = Dictionary::cast(result);
    }
  }
  // Switch to using the dictionary as the backing storage for elements.
  set_elements(dictionary);

  Counters::elements_to_dictionary.Increment();

#ifdef DEBUG
  if (FLAG_trace_normalization) {
    PrintF("Object elements have been normalized:\n");
    Print();
  }
#endif

  return this;
}


Object* JSObject::DeletePropertyPostInterceptor(String* name) {
  // Check local property, ignore interceptor.
  LookupResult result;
  LocalLookupRealNamedProperty(name, &result);
  if (!result.IsValid()) return Heap::true_value();

  // Normalize object if needed.
  Object* obj = NormalizeProperties();
  if (obj->IsFailure()) return obj;

  ASSERT(!HasFastProperties());
  // Attempt to remove the property from the property dictionary.
  Dictionary* dictionary = property_dictionary();
  int entry = dictionary->FindStringEntry(name);
  if (entry != -1) return dictionary->DeleteProperty(entry);
  return Heap::true_value();
}


Object* JSObject::DeletePropertyWithInterceptor(String* name) {
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetNamedInterceptor());
  Handle<String> name_handle(name);
  Handle<JSObject> this_handle(this);
  if (!interceptor->deleter()->IsUndefined()) {
    v8::NamedPropertyDeleter deleter =
        v8::ToCData<v8::NamedPropertyDeleter>(interceptor->deleter());
    Handle<Object> data_handle(interceptor->data());
    LOG(ApiNamedPropertyAccess("interceptor-named-delete", *this_handle, name));
    v8::AccessorInfo info(v8::Utils::ToLocal(this_handle),
                          v8::Utils::ToLocal(data_handle),
                          v8::Utils::ToLocal(this_handle));
    v8::Handle<v8::Boolean> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = deleter(v8::Utils::ToLocal(name_handle), info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!result.IsEmpty()) {
      ASSERT(result->IsBoolean());
      return *v8::Utils::OpenHandle(*result);
    }
  }
  Object* raw_result = this_handle->DeletePropertyPostInterceptor(*name_handle);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


Object* JSObject::DeleteElementPostInterceptor(uint32_t index) {
  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
      static_cast<uint32_t>(Smi::cast(JSArray::cast(this)->length())->value()) :
      static_cast<uint32_t>(FixedArray::cast(elements())->length());
    if (index < length) {
      FixedArray::cast(elements())->set_the_hole(index);
    }
    return Heap::true_value();
  }
  ASSERT(!HasFastElements());
  Dictionary* dictionary = element_dictionary();
  int entry = dictionary->FindNumberEntry(index);
  if (entry != -1) return dictionary->DeleteProperty(entry);
  return Heap::true_value();
}


Object* JSObject::DeleteElementWithInterceptor(uint32_t index) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetIndexedInterceptor());
  if (interceptor->deleter()->IsUndefined()) return Heap::false_value();
  v8::IndexedPropertyDeleter deleter =
      v8::ToCData<v8::IndexedPropertyDeleter>(interceptor->deleter());
  Handle<JSObject> this_handle(this);
  Handle<Object> data_handle(interceptor->data());
  LOG(ApiIndexedPropertyAccess("interceptor-indexed-delete", this, index));
  v8::AccessorInfo info(v8::Utils::ToLocal(this_handle),
                        v8::Utils::ToLocal(data_handle),
                        v8::Utils::ToLocal(this_handle));
  v8::Handle<v8::Boolean> result;
  {
    // Leaving JavaScript.
    VMState state(OTHER);
    result = deleter(index, info);
  }
  RETURN_IF_SCHEDULED_EXCEPTION();
  if (!result.IsEmpty()) {
    ASSERT(result->IsBoolean());
    return *v8::Utils::OpenHandle(*result);
  }
  Object* raw_result = this_handle->DeleteElementPostInterceptor(index);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


Object* JSObject::DeleteElement(uint32_t index) {
  if (HasIndexedInterceptor()) {
    return DeleteElementWithInterceptor(index);
  }

  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
      static_cast<uint32_t>(Smi::cast(JSArray::cast(this)->length())->value()) :
      static_cast<uint32_t>(FixedArray::cast(elements())->length());
    if (index < length) {
      FixedArray::cast(elements())->set_the_hole(index);
    }
    return Heap::true_value();
  } else {
    Dictionary* dictionary = element_dictionary();
    int entry = dictionary->FindNumberEntry(index);
    if (entry != -1) return dictionary->DeleteProperty(entry);
  }
  return Heap::true_value();
}


Object* JSObject::DeleteProperty(String* name) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, name, v8::ACCESS_DELETE)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_DELETE);
    return Heap::false_value();
  }

  // ECMA-262, 3rd, 8.6.2.5
  ASSERT(name->IsString());

  uint32_t index;
  if (name->AsArrayIndex(&index)) {
    return DeleteElement(index);
  } else {
    LookupResult result;
    LocalLookup(name, &result);
    if (!result.IsValid()) return Heap::true_value();
    if (result.IsDontDelete()) return Heap::false_value();
    // Check for interceptor.
    if (result.type() == INTERCEPTOR) {
      return DeletePropertyWithInterceptor(name);
    }
    if (!result.IsLoaded()) {
      return JSObject::cast(this)->DeleteLazyProperty(&result, name);
    }
    // Normalize object if needed.
    Object* obj = NormalizeProperties();
    if (obj->IsFailure()) return obj;
    // Make sure the properties are normalized before removing the entry.
    Dictionary* dictionary = property_dictionary();
    int entry = dictionary->FindStringEntry(name);
    if (entry != -1) return dictionary->DeleteProperty(entry);
    return Heap::true_value();
  }
}


// Check whether this object references another object.
bool JSObject::ReferencesObject(Object* obj) {
  AssertNoAllocation no_alloc;

  // Is the object the constructor for this object?
  if (map()->constructor() == obj) {
    return true;
  }

  // Is the object the prototype for this object?
  if (map()->prototype() == obj) {
    return true;
  }

  // Check if the object is among the named properties.
  Object* key = SlowReverseLookup(obj);
  if (key != Heap::undefined_value()) {
    return true;
  }

  // Check if the object is among the indexed properties.
  if (HasFastElements()) {
    int length = IsJSArray()
        ? Smi::cast(JSArray::cast(this)->length())->value()
        : FixedArray::cast(elements())->length();
    for (int i = 0; i < length; i++) {
      Object* element = FixedArray::cast(elements())->get(i);
      if (!element->IsTheHole() && element == obj) {
        return true;
      }
    }
  } else {
    key = element_dictionary()->SlowReverseLookup(obj);
    if (key != Heap::undefined_value()) {
      return true;
    }
  }

  // For functions check the context. Boilerplate functions do
  // not have to be traversed since they have no real context.
  if (IsJSFunction() && !JSFunction::cast(this)->IsBoilerplate()) {
    // Get the constructor function for arguments array.
    JSObject* arguments_boilerplate =
        Top::context()->global_context()->arguments_boilerplate();
    JSFunction* arguments_function =
        JSFunction::cast(arguments_boilerplate->map()->constructor());

    // Get the context and don't check if it is the global context.
    JSFunction* f = JSFunction::cast(this);
    Context* context = f->context();
    if (context->IsGlobalContext()) {
      return false;
    }

    // Check the non-special context slots.
    for (int i = Context::MIN_CONTEXT_SLOTS; i < context->length(); i++) {
      // Only check JS objects.
      if (context->get(i)->IsJSObject()) {
        JSObject* ctxobj = JSObject::cast(context->get(i));
        // If it is an arguments array check the content.
        if (ctxobj->map()->constructor() == arguments_function) {
          if (ctxobj->ReferencesObject(obj)) {
            return true;
          }
        } else if (ctxobj == obj) {
          return true;
        }
      }
    }

    // Check the context extension if any.
    if (context->extension() != NULL) {
      return context->extension()->ReferencesObject(obj);
    }
  }

  // No references to object.
  return false;
}


// Tests for the fast common case for property enumeration:
// - this object has an enum cache
// - this object has no elements
// - no prototype has enumerable properties/elements
// - neither this object nor any prototype has interceptors
bool JSObject::IsSimpleEnum() {
  JSObject* arguments_boilerplate =
      Top::context()->global_context()->arguments_boilerplate();
  JSFunction* arguments_function =
      JSFunction::cast(arguments_boilerplate->map()->constructor());
  if (IsAccessCheckNeeded()) return false;
  if (map()->constructor() == arguments_function) return false;

  for (Object* o = this;
       o != Heap::null_value();
       o = JSObject::cast(o)->GetPrototype()) {
    JSObject* curr = JSObject::cast(o);
    if (!curr->HasFastProperties()) return false;
    if (!curr->map()->instance_descriptors()->HasEnumCache()) return false;
    if (curr->NumberOfEnumElements() > 0) return false;
    if (curr->HasNamedInterceptor()) return false;
    if (curr->HasIndexedInterceptor()) return false;
    if (curr != this) {
      FixedArray* curr_fixed_array =
          FixedArray::cast(curr->map()->instance_descriptors()->GetEnumCache());
      if (curr_fixed_array->length() > 0) {
        return false;
      }
    }
  }
  return true;
}


int Map::NumberOfDescribedProperties() {
  int result = 0;
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (!r.IsTransition()) result++;
  }
  return result;
}


int Map::PropertyIndexFor(String* name) {
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (r.Equals(name)) return r.GetFieldIndex();
  }
  return -1;
}


int Map::NextFreePropertyIndex() {
  int index = -1;
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (r.type() == FIELD) {
      if (r.GetFieldIndex() > index) index = r.GetFieldIndex();
    }
  }
  return index+1;
}

Object* Map::EnsureNoMapTransitions() {
  // Remove all map transitions.

  // Compute the size of the map transition entries to be removed.
  int nof = 0;
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (r.IsTransition()) nof++;
  }

  if (nof == 0) return this;

  // Allocate the new descriptor array.
  Object* result = DescriptorArray::Allocate(
      instance_descriptors()->number_of_descriptors() - nof);
  if (result->IsFailure()) return result;

  // Copy the content.
  DescriptorWriter w(DescriptorArray::cast(result));
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (!r.IsTransition()) w.WriteFrom(&r);
  }
  ASSERT(w.eos());

  set_instance_descriptors(DescriptorArray::cast(result));

  return this;
}


AccessorDescriptor* Map::FindAccessor(String* name) {
  for (DescriptorReader r(instance_descriptors()); !r.eos(); r.advance()) {
    if (r.Equals(name) && r.type() == CALLBACKS) return r.GetCallbacks();
  }
  return NULL;
}


void JSObject::LocalLookup(String* name, LookupResult* result) {
  ASSERT(name->IsString());

  // Do not use inline caching if the object is a non-global object
  // that requires access checks.
  if (!IsJSGlobalObject() && IsAccessCheckNeeded()) {
    result->DisallowCaching();
  }

  // Check __proto__ before interceptor.
  if (name->Equals(Heap::Proto_symbol())) {
    result->ConstantResult(this);
    return;
  }

  // Check for lookup interceptor except when bootstrapping.
  if (HasNamedInterceptor() && !Bootstrapper::IsActive()) {
    result->InterceptorResult(this);
    return;
  }

  LocalLookupRealNamedProperty(name, result);
}


void JSObject::Lookup(String* name, LookupResult* result) {
  // Ecma-262 3rd 8.6.2.4
  for (Object* current = this;
       current != Heap::null_value();
       current = JSObject::cast(current)->GetPrototype()) {
    JSObject::cast(current)->LocalLookup(name, result);
    if (result->IsValid() && !result->IsTransitionType()) {
      return;
    }
  }
  result->NotFound();
}


Object* JSObject::DefineGetterSetter(String* name,
                                     PropertyAttributes attributes) {
  // Make sure that the top context does not change when doing callbacks or
  // interceptor calls.
  AssertNoContextChange ncc;

  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, name, v8::ACCESS_SET)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_SET);
    return Heap::undefined_value();
  }

  // TryFlatten before operating on the string.
  name->TryFlatten();

  // Make sure name is not an index.
  uint32_t index;
  if (name->AsArrayIndex(&index)) return Heap::undefined_value();

  // Lookup the name.
  LookupResult result;
  LocalLookup(name, &result);
  if (result.IsValid()) {
    if (result.IsReadOnly()) return Heap::undefined_value();
    if (result.type() == CALLBACKS) {
      Object* obj = result.GetCallbackObject();
      if (obj->IsFixedArray()) return obj;
    }
  }

  // Normalize object to make this operation simple.
  Object* ok = NormalizeProperties();
  if (ok->IsFailure()) return ok;

  // Allocate the fixed array to hold getter and setter.
  Object* array = Heap::AllocateFixedArray(2);
  if (array->IsFailure()) return array;

  // Update the dictionary with the new CALLBACKS property.
  PropertyDetails details = PropertyDetails(attributes, CALLBACKS);
  Object* dict =
      property_dictionary()->SetOrAddStringEntry(name, array, details);
  if (dict->IsFailure()) return dict;

  // Set the potential new dictionary on the object.
  set_properties(Dictionary::cast(dict));
  return array;
}


Object* JSObject::DefineAccessor(String* name, bool is_getter, JSFunction* fun,
                                 PropertyAttributes attributes) {
  Object* array = DefineGetterSetter(name, attributes);
  if (array->IsFailure() || array->IsUndefined()) return array;
  FixedArray::cast(array)->set(is_getter ? 0 : 1, fun);
  return this;
}


Object* JSObject::LookupAccessor(String* name, bool is_getter) {
  // Make sure that the top context does not change when doing callbacks or
  // interceptor calls.
  AssertNoContextChange ncc;

  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, name, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return Heap::undefined_value();
  }

  // Make sure name is not an index.
  uint32_t index;
  if (name->AsArrayIndex(&index)) return Heap::undefined_value();

  // Make the lookup and include prototypes.
  for (Object* obj = this;
       obj != Heap::null_value();
       obj = JSObject::cast(obj)->GetPrototype()) {
    LookupResult result;
    JSObject::cast(obj)->LocalLookup(name, &result);
    if (result.IsValid()) {
      if (result.IsReadOnly()) return Heap::undefined_value();
      if (result.type() == CALLBACKS) {
        Object* obj = result.GetCallbackObject();
        if (obj->IsFixedArray()) {
          return FixedArray::cast(obj)->get(is_getter
                                            ? kGetterIndex
                                            : kSetterIndex);
        }
      }
    }
  }
  return Heap::undefined_value();
}


Object* JSObject::SlowReverseLookup(Object* value) {
  if (HasFastProperties()) {
    for (DescriptorReader r(map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      if (r.type() == FIELD) {
        if (properties()->get(r.GetFieldIndex()) == value) {
          return r.GetKey();
        }
      } else if (r.type() == CONSTANT_FUNCTION) {
        if (r.GetConstantFunction() == value) {
          return r.GetKey();
        }
      }
    }
    return Heap::undefined_value();
  } else {
    return property_dictionary()->SlowReverseLookup(value);
  }
}


Object* Map::Copy() {
  Object* result = Heap::AllocateMap(instance_type(), instance_size());
  if (result->IsFailure()) return result;
  Map::cast(result)->set_prototype(prototype());
  Map::cast(result)->set_constructor(constructor());
  Map::cast(result)->set_instance_descriptors(instance_descriptors());
  // Please note instance_type and instance_size are set when allocated.
  Map::cast(result)->set_unused_property_fields(unused_property_fields());
  Map::cast(result)->set_bit_field(bit_field());
  Map::cast(result)->ClearCodeCache();
  return result;
}


Object* Map::UpdateCodeCache(String* name, Code* code) {
  ASSERT(code->state() == MONOMORPHIC);
  FixedArray* cache = code_cache();

  // When updating the code cache we disregard the type encoded in the
  // flags. This allows call constant stubs to overwrite call field
  // stubs, etc.
  Code::Flags flags = Code::RemoveTypeFromFlags(code->flags());

  // First check whether we can update existing code cache without
  // extending it.
  int length = cache->length();
  for (int i = 0; i < length; i += 2) {
    Object* key = cache->get(i);
    if (key->IsUndefined()) {
      cache->set(i + 0, name);
      cache->set(i + 1, code);
      return this;
    }
    if (name->Equals(String::cast(key))) {
      Code::Flags found = Code::cast(cache->get(i + 1))->flags();
      if (Code::RemoveTypeFromFlags(found) == flags) {
        cache->set(i + 1, code);
        return this;
      }
    }
  }

  // Extend the code cache with some new entries (at least one).
  int new_length = length + ((length >> 1) & ~1) + 2;
  ASSERT((new_length & 1) == 0);  // must be a multiple of two
  Object* result = cache->CopySize(new_length);
  if (result->IsFailure()) return result;

  // Add the (name, code) pair to the new cache.
  cache = FixedArray::cast(result);
  cache->set(length + 0, name);
  cache->set(length + 1, code);
  set_code_cache(cache);
  return this;
}


Object* Map::FindInCodeCache(String* name, Code::Flags flags) {
  FixedArray* cache = code_cache();
  int length = cache->length();
  for (int i = 0; i < length; i += 2) {
    Object* key = cache->get(i);
    if (key->IsUndefined()) {
      return key;
    }
    if (name->Equals(String::cast(key))) {
      Code* code = Code::cast(cache->get(i + 1));
      if (code->flags() == flags) return code;
    }
  }
  return Heap::undefined_value();
}


bool Map::IncludedInCodeCache(Code* code) {
  FixedArray* array = code_cache();
  int len = array->length();
  for (int i = 0; i < len; i += 2) {
    if (array->get(i+1) == code) return true;
  }
  return false;
}


void FixedArray::FixedArrayIterateBody(ObjectVisitor* v) {
  IteratePointers(v, kHeaderSize, kHeaderSize + length() * kPointerSize);
}


static bool HasKey(FixedArray* array, Object* key) {
  int len0 = array->length();
  for (int i = 0; i < len0; i++) {
    Object* element = array->get(i);
    if (element->IsSmi() && key->IsSmi() && (element == key)) return true;
    if (element->IsString() &&
        key->IsString() && String::cast(element)->Equals(String::cast(key))) {
      return true;
    }
  }
  return false;
}


Object* FixedArray::AddKeysFromJSArray(JSArray* array) {
  // Remove array holes from array if any.
  Object* object = array->RemoveHoles();
  if (object->IsFailure()) return object;
  JSArray* compacted_array = JSArray::cast(object);

  // Allocate a temporary fixed array.
  int compacted_array_length = Smi::cast(compacted_array->length())->value();
  object = Heap::AllocateFixedArray(compacted_array_length);
  if (object->IsFailure()) return object;
  FixedArray* key_array = FixedArray::cast(object);

  // Copy the elements from the JSArray to the temporary fixed array.
  for (int i = 0; i < compacted_array_length; i++) {
    key_array->set(i, compacted_array->GetElement(i));
  }

  // Compute the union of this and the temporary fixed array.
  return UnionOfKeys(key_array);
}


Object* FixedArray::UnionOfKeys(FixedArray* other) {
  int len0 = length();
  int len1 = other->length();
  // Optimize if either is empty.
  if (len0 == 0) return other;
  if (len1 == 0) return this;

  // Compute how many elements are not in this.
  int extra = 0;
  for (int y = 0; y < len1; y++) {
    if (!HasKey(this, other->get(y))) extra++;
  }

  // Allocate the result
  Object* obj = Heap::AllocateFixedArray(len0 + extra);
  if (obj->IsFailure()) return obj;
  // Fill in the content
  FixedArray* result = FixedArray::cast(obj);
  for (int i = 0; i < len0; i++) {
    result->set(i, get(i));
  }
  // Fill in the extra keys.
  int index = 0;
  for (int y = 0; y < len1; y++) {
    if (!HasKey(this, other->get(y))) {
      result->set(len0 + index, other->get(y));
      index++;
    }
  }
  ASSERT(extra == index);
  return result;
}


Object* FixedArray::Copy() {
  int len = length();
  if (len == 0) return this;
  Object* obj = Heap::AllocateFixedArray(len);
  if (obj->IsFailure()) return obj;
  FixedArray* result = FixedArray::cast(obj);
  WriteBarrierMode mode = result->GetWriteBarrierMode();
  // Copy the content
  for (int i = 0; i < len; i++) {
    result->set(i, get(i), mode);
  }
  result->set_map(map());
  return result;
}

Object* FixedArray::CopySize(int new_length) {
  if (new_length == 0) return Heap::empty_fixed_array();
  Object* obj = Heap::AllocateFixedArray(new_length);
  if (obj->IsFailure()) return obj;
  FixedArray* result = FixedArray::cast(obj);
  WriteBarrierMode mode = result->GetWriteBarrierMode();
  // Copy the content
  int len = length();
  if (new_length < len) len = new_length;
  for (int i = 0; i < len; i++) {
    result->set(i, get(i), mode);
  }
  result->set_map(map());
  return result;
}


void FixedArray::CopyTo(int pos, FixedArray* dest, int dest_pos, int len) {
  WriteBarrierMode mode = dest->GetWriteBarrierMode();
  for (int index = 0; index < len; index++) {
    dest->set(dest_pos+index, get(pos+index), mode);
  }
}


Object* DescriptorArray::Allocate(int number_of_descriptors) {
  // Allocate the descriptor array.
  Object* array = Heap::AllocateFixedArray(ToKeyIndex(number_of_descriptors));
  if (array->IsFailure()) return array;
  DescriptorArray* result = DescriptorArray::cast(array);

  // Allocate the content array and set it in the descriptor array.
  array = Heap::AllocateFixedArray(number_of_descriptors << 1);
  if (array->IsFailure()) return array;
  result->set(kContentArrayIndex, array);

  // Initialize the next enumeration index.
  result->SetNextEnumerationIndex(PropertyDetails::kInitialIndex);

  return result;
}


void DescriptorArray::SetEnumCache(FixedArray* bridge_storage,
                                   FixedArray* new_cache) {
  ASSERT(bridge_storage->length() >= kEnumCacheBridgeLength);
  if (HasEnumCache()) {
    FixedArray::cast(get(kEnumerationIndexIndex))->
      set(kEnumCacheBridgeCacheIndex, new_cache);
  } else {
    if (length() == 0) return;  // Do nothing for empty descriptor array.
    FixedArray::cast(bridge_storage)->
      set(kEnumCacheBridgeCacheIndex, new_cache);
    fast_set(FixedArray::cast(bridge_storage),
             kEnumCacheBridgeEnumIndex,
             get(kEnumerationIndexIndex));
    set(kEnumerationIndexIndex, bridge_storage);
  }
}


void DescriptorArray::ReplaceConstantFunction(int descriptor_number,
                                              JSFunction* value) {
  ASSERT(!Heap::InNewSpace(value));
  FixedArray* content_array = GetContentArray();
  fast_set(content_array, ToValueIndex(descriptor_number), value);
}


Object* DescriptorArray::CopyInsert(Descriptor* desc,
                                     bool remove_map_transitions) {
  int transitions = 0;
  if (remove_map_transitions) {
    // Compute space from map transitions.
    for (DescriptorReader r(this); !r.eos(); r.advance()) {
      if (r.IsTransition()) transitions++;
    }
  }

  // Ensure the key is a symbol.
  Object* result = desc->KeyToSymbol();
  if (result->IsFailure()) return result;

  result = Allocate(number_of_descriptors() - transitions + 1);
  if (result->IsFailure()) return result;

  // Set the enumeration index in the descriptors and set the enumeration index
  // in the result.
  int index = NextEnumerationIndex();
  desc->SetEnumerationIndex(index);
  DescriptorArray::cast(result)->SetNextEnumerationIndex(index + 1);

  // Write the old content and the descriptor information
  DescriptorWriter w(DescriptorArray::cast(result));
  DescriptorReader r(this);
  while (!r.eos() && r.GetKey()->Hash() <= desc->key()->Hash()) {
    if (!r.IsTransition() || !remove_map_transitions) {
      w.WriteFrom(&r);
    }
    r.advance();
  }
  w.Write(desc);
  while (!r.eos()) {
    if (!r.IsTransition() || !remove_map_transitions) {
      w.WriteFrom(&r);
    }
    r.advance();
  }
  ASSERT(w.eos());

  return result;
}


Object* DescriptorArray::CopyReplace(String* name,
                                     int index,
                                     PropertyAttributes attributes) {
  // Allocate the new descriptor array.
  Object* result = DescriptorArray::Allocate(number_of_descriptors());
  if (result->IsFailure()) return result;

  // Make sure only symbols are added to the instance descriptor.
  if (!name->IsSymbol()) {
    Object* result = Heap::LookupSymbol(name);
    if (result->IsFailure()) return result;
    name = String::cast(result);
  }

  DescriptorWriter w(DescriptorArray::cast(result));
  for (DescriptorReader r(this); !r.eos(); r.advance()) {
    if (r.Equals(name)) {
      FieldDescriptor d(name, index, attributes);
      d.SetEnumerationIndex(r.GetDetails().index());
      w.Write(&d);
    } else {
      w.WriteFrom(&r);
    }
  }

  // Copy the next enumeration index.
  DescriptorArray::cast(result)->
    SetNextEnumerationIndex(NextEnumerationIndex());

  ASSERT(w.eos());
  return result;
}


bool DescriptorArray::IsSortedNoDuplicates() {
  String* current_key = NULL;
  uint32_t current = 0;
  for (DescriptorReader r(this); !r.eos(); r.advance()) {
    String* key = r.GetKey();
    if (key == current_key) return false;
    current_key = key;
    uint32_t hash = r.GetKey()->Hash();
    if (hash < current) return false;
    current = hash;
  }
  return true;
}


void DescriptorArray::Sort() {
  // In-place heap sort.
  int len = number_of_descriptors();

  // Bottom-up max-heap construction.
  for (int i = 1; i < len; ++i) {
    int child_index = i;
    while (child_index > 0) {
      int parent_index = ((child_index + 1) >> 1) - 1;
      uint32_t parent_hash = GetKey(parent_index)->Hash();
      uint32_t child_hash = GetKey(child_index)->Hash();
      if (parent_hash < child_hash) {
        Swap(parent_index, child_index);
      } else {
        break;
      }
      child_index = parent_index;
    }
  }

  // Extract elements and create sorted array.
  for (int i = len - 1; i > 0; --i) {
    // Put max element at the back of the array.
    Swap(0, i);
    // Sift down the new top element.
    int parent_index = 0;
    while (true) {
      int child_index = ((parent_index + 1) << 1) - 1;
      if (child_index >= i) break;
      uint32_t child1_hash = GetKey(child_index)->Hash();
      uint32_t child2_hash = GetKey(child_index + 1)->Hash();
      uint32_t parent_hash = GetKey(parent_index)->Hash();
      if (child_index + 1 >= i || child1_hash > child2_hash) {
        if (parent_hash > child1_hash) break;
        Swap(parent_index, child_index);
        parent_index = child_index;
      } else {
        if (parent_hash > child2_hash) break;
        Swap(parent_index, child_index + 1);
        parent_index = child_index + 1;
      }
    }
  }

  SLOW_ASSERT(IsSortedNoDuplicates());
}


int DescriptorArray::BinarySearch(String* name, int low, int high) {
  uint32_t hash = name->Hash();

  while (low <= high) {
    int mid = (low + high) / 2;
    String* mid_name = GetKey(mid);
    uint32_t mid_hash = mid_name->Hash();

    if (mid_hash > hash) {
      high = mid - 1;
      continue;
    }
    if (mid_hash < hash) {
      low = mid + 1;
      continue;
    }
    // Found an element with the same hash-code.
    ASSERT(hash == mid_hash);
    // There might be more, so we find the first one and
    // check them all to see if we have a match.
    if (name == mid_name) return mid;
    while ((mid > low) && (GetKey(mid - 1)->Hash() == hash)) mid--;
    for (; (mid <= high) && (GetKey(mid)->Hash() == hash); mid++) {
      if (GetKey(mid)->Equals(name)) return mid;
    }
    break;
  }
  return kNotFound;
}


static StaticResource<StringInputBuffer> string_input_buffer;


bool String::LooksValid() {
  if (!Heap::Contains(this))
    return false;
  switch (representation_tag()) {
    case kSeqStringTag:
    case kConsStringTag:
    case kSlicedStringTag:
    case kExternalStringTag:
      return true;
    default:
      return false;
  }
}


SmartPointer<char> String::ToCString(AllowNullsFlag allow_nulls,
                                     RobustnessFlag robust_flag,
                                     int offset,
                                     int length,
                                     int* length_return) {
  ASSERT(NativeAllocationChecker::allocation_allowed());
  if (robust_flag == ROBUST_STRING_TRAVERSAL && !LooksValid()) {
    return SmartPointer<char>(NULL);
  }

  // Negative length means the to the end of the string.
  if (length < 0) length = kMaxInt - offset;

  // Compute the size of the UTF-8 string. Start at the specified offset.
  Access<StringInputBuffer> buffer(&string_input_buffer);
  buffer->Reset(offset, this);
  int character_position = offset;
  int utf8_bytes = 0;
  while (buffer->has_more()) {
    uint16_t character = buffer->GetNext();
    if (character_position < offset + length) {
      utf8_bytes += unibrow::Utf8::Length(character);
    }
    character_position++;
  }

  if (length_return) {
    *length_return = utf8_bytes;
  }

  char* result = NewArray<char>(utf8_bytes + 1);

  // Convert the UTF-16 string to a UTF-8 buffer. Start at the specified offset.
  buffer->Rewind();
  buffer->Seek(offset);
  character_position = offset;
  int utf8_byte_position = 0;
  while (buffer->has_more()) {
    uint16_t character = buffer->GetNext();
    if (character_position < offset + length) {
      if (allow_nulls == DISALLOW_NULLS && character == 0) {
        character = ' ';
      }
      utf8_byte_position +=
          unibrow::Utf8::Encode(result + utf8_byte_position, character);
    }
    character_position++;
  }
  result[utf8_byte_position] = 0;
  return SmartPointer<char>(result);
}


SmartPointer < char >String::ToCString(AllowNullsFlag allow_nulls,
                                       RobustnessFlag robust_flag,
                                       int* length_return) {
  return ToCString(allow_nulls, robust_flag, 0, -1, length_return);
}


const uc16* String::GetTwoByteData() {
  return GetTwoByteData(0);
}


const uc16* String::GetTwoByteData(unsigned start) {
  ASSERT(!IsAscii());
  switch (representation_tag()) {
    case kSeqStringTag:
      return TwoByteString::cast(this)->TwoByteStringGetData(start);
    case kExternalStringTag:
      return ExternalTwoByteString::cast(this)->
        ExternalTwoByteStringGetData(start);
    case kSlicedStringTag: {
      SlicedString* sliced_string = SlicedString::cast(this);
      String* buffer = String::cast(sliced_string->buffer());
      if (buffer->StringIsConsString()) {
        ConsString* cons_string = ConsString::cast(buffer);
        // Flattened string.
        ASSERT(String::cast(cons_string->second())->length() == 0);
        buffer = String::cast(cons_string->first());
      }
      return buffer->GetTwoByteData(start + sliced_string->start());
    }
    case kConsStringTag:
      UNREACHABLE();
      return NULL;
  }
  UNREACHABLE();
  return NULL;
}


uc16* String::ToWideCString(RobustnessFlag robust_flag) {
  ASSERT(NativeAllocationChecker::allocation_allowed());

  if (robust_flag == ROBUST_STRING_TRAVERSAL && !LooksValid()) {
    return NULL;
  }

  Access<StringInputBuffer> buffer(&string_input_buffer);
  buffer->Reset(this);

  uc16* result = NewArray<uc16>(length() + 1);

  int i = 0;
  while (buffer->has_more()) {
    uint16_t character = buffer->GetNext();
    result[i++] = character;
  }
  result[i] = 0;
  return result;
}


const uc16* TwoByteString::TwoByteStringGetData(unsigned start) {
  return reinterpret_cast<uc16*>(
      reinterpret_cast<char*>(this) - kHeapObjectTag + kHeaderSize) + start;
}


void TwoByteString::TwoByteStringReadBlockIntoBuffer(ReadBlockBuffer* rbb,
                                                     unsigned* offset_ptr,
                                                     unsigned max_chars) {
  unsigned chars_read = 0;
  unsigned offset = *offset_ptr;
  while (chars_read < max_chars) {
    uint16_t c = *reinterpret_cast<uint16_t*>(
        reinterpret_cast<char*>(this) -
            kHeapObjectTag + kHeaderSize + offset * kShortSize);
    if (c <= kMaxAsciiCharCode) {
      // Fast case for ASCII characters.   Cursor is an input output argument.
      if (!unibrow::CharacterStream::EncodeAsciiCharacter(c,
                                                          rbb->util_buffer,
                                                          rbb->capacity,
                                                          rbb->cursor)) {
        break;
      }
    } else {
      if (!unibrow::CharacterStream::EncodeNonAsciiCharacter(c,
                                                             rbb->util_buffer,
                                                             rbb->capacity,
                                                             rbb->cursor)) {
        break;
      }
    }
    offset++;
    chars_read++;
  }
  *offset_ptr = offset;
  rbb->remaining += chars_read;
}


const unibrow::byte* AsciiString::AsciiStringReadBlock(unsigned* remaining,
                                                       unsigned* offset_ptr,
                                                       unsigned max_chars) {
  // Cast const char* to unibrow::byte* (signedness difference).
  const unibrow::byte* b = reinterpret_cast<unibrow::byte*>(this) -
      kHeapObjectTag + kHeaderSize + *offset_ptr * kCharSize;
  *remaining = max_chars;
  *offset_ptr += max_chars;
  return b;
}


// This will iterate unless the block of string data spans two 'halves' of
// a ConsString, in which case it will recurse.  Since the block of string
// data to be read has a maximum size this limits the maximum recursion
// depth to something sane.  Since C++ does not have tail call recursion
// elimination, the iteration must be explicit. Since this is not an
// -IntoBuffer method it can delegate to one of the efficient
// *AsciiStringReadBlock routines.
const unibrow::byte* ConsString::ConsStringReadBlock(ReadBlockBuffer* rbb,
                                                     unsigned* offset_ptr,
                                                     unsigned max_chars) {
  ConsString* current = this;
  unsigned offset = *offset_ptr;
  int offset_correction = 0;

  while (true) {
    String* left = String::cast(current->first());
    unsigned left_length = (unsigned)left->length();
    if (left_length > offset &&
        (max_chars <= left_length - offset ||
         (rbb->capacity <= left_length - offset &&
          (max_chars = left_length - offset, true)))) {  // comma operator!
      // Left hand side only - iterate unless we have reached the bottom of
      // the cons tree.  The assignment on the left of the comma operator is
      // in order to make use of the fact that the -IntoBuffer routines can
      // produce at most 'capacity' characters.  This enables us to postpone
      // the point where we switch to the -IntoBuffer routines (below) in order
      // to maximize the chances of delegating a big chunk of work to the
      // efficient *AsciiStringReadBlock routines.
      if (left->StringIsConsString()) {
        current = ConsString::cast(left);
        continue;
      } else {
        const unibrow::byte* answer =
            String::ReadBlock(left, rbb, &offset, max_chars);
        *offset_ptr = offset + offset_correction;
        return answer;
      }
    } else if (left_length <= offset) {
      // Right hand side only - iterate unless we have reached the bottom of
      // the cons tree.
      String* right = String::cast(current->second());
      offset -= left_length;
      offset_correction += left_length;
      if (right->StringIsConsString()) {
        current = ConsString::cast(right);
        continue;
      } else {
        const unibrow::byte* answer =
            String::ReadBlock(right, rbb, &offset, max_chars);
        *offset_ptr = offset + offset_correction;
        return answer;
      }
    } else {
      // The block to be read spans two sides of the ConsString, so we call the
      // -IntoBuffer version, which will recurse.  The -IntoBuffer methods
      // are able to assemble data from several part strings because they use
      // the util_buffer to store their data and never return direct pointers
      // to their storage.  We don't try to read more than the buffer capacity
      // here or we can get too much recursion.
      ASSERT(rbb->remaining == 0);
      ASSERT(rbb->cursor == 0);
      current->ConsStringReadBlockIntoBuffer(
          rbb,
          &offset,
          max_chars > rbb->capacity ? rbb->capacity : max_chars);
      *offset_ptr = offset + offset_correction;
      return rbb->util_buffer;
    }
  }
}


const unibrow::byte* SlicedString::SlicedStringReadBlock(ReadBlockBuffer* rbb,
                                                         unsigned* offset_ptr,
                                                         unsigned max_chars) {
  String* backing = String::cast(buffer());
  unsigned offset = start() + *offset_ptr;
  unsigned length = backing->length();
  if (max_chars > length - offset) {
    max_chars = length - offset;
  }
  const unibrow::byte* answer =
      String::ReadBlock(backing, rbb, &offset, max_chars);
  *offset_ptr = offset - start();
  return answer;
}


uint16_t ExternalAsciiString::ExternalAsciiStringGet(int index) {
  ASSERT(index >= 0 && index < length());
  return resource()->data()[index];
}


const unibrow::byte* ExternalAsciiString::ExternalAsciiStringReadBlock(
      unsigned* remaining,
      unsigned* offset_ptr,
      unsigned max_chars) {
  // Cast const char* to unibrow::byte* (signedness difference).
  const unibrow::byte* b =
      reinterpret_cast<const unibrow::byte*>(resource()->data()) + *offset_ptr;
  *remaining = max_chars;
  *offset_ptr += max_chars;
  return b;
}


const uc16* ExternalTwoByteString::ExternalTwoByteStringGetData(
      unsigned start) {
  return resource()->data() + start;
}


uint16_t ExternalTwoByteString::ExternalTwoByteStringGet(int index) {
  ASSERT(index >= 0 && index < length());
  return resource()->data()[index];
}


void ExternalTwoByteString::ExternalTwoByteStringReadBlockIntoBuffer(
      ReadBlockBuffer* rbb,
      unsigned* offset_ptr,
      unsigned max_chars) {
  unsigned chars_read = 0;
  unsigned offset = *offset_ptr;
  const uint16_t* data = resource()->data();
  while (chars_read < max_chars) {
    uint16_t c = data[offset];
    if (c <= kMaxAsciiCharCode) {
      // Fast case for ASCII characters.   Cursor is an input output argument.
      if (!unibrow::CharacterStream::EncodeAsciiCharacter(c,
                                                          rbb->util_buffer,
                                                          rbb->capacity,
                                                          rbb->cursor))
        break;
    } else {
      if (!unibrow::CharacterStream::EncodeNonAsciiCharacter(c,
                                                             rbb->util_buffer,
                                                             rbb->capacity,
                                                             rbb->cursor))
        break;
    }
    offset++;
    chars_read++;
  }
  *offset_ptr = offset;
  rbb->remaining += chars_read;
}


void AsciiString::AsciiStringReadBlockIntoBuffer(ReadBlockBuffer* rbb,
                                                 unsigned* offset_ptr,
                                                 unsigned max_chars) {
  unsigned capacity = rbb->capacity - rbb->cursor;
  if (max_chars > capacity) max_chars = capacity;
  memcpy(rbb->util_buffer + rbb->cursor,
         reinterpret_cast<char*>(this) - kHeapObjectTag + kHeaderSize +
             *offset_ptr * kCharSize,
         max_chars);
  rbb->remaining += max_chars;
  *offset_ptr += max_chars;
  rbb->cursor += max_chars;
}


void ExternalAsciiString::ExternalAsciiStringReadBlockIntoBuffer(
      ReadBlockBuffer* rbb,
      unsigned* offset_ptr,
      unsigned max_chars) {
  unsigned capacity = rbb->capacity - rbb->cursor;
  if (max_chars > capacity) max_chars = capacity;
  memcpy(rbb->util_buffer + rbb->cursor,
         resource()->data() + *offset_ptr,
         max_chars);
  rbb->remaining += max_chars;
  *offset_ptr += max_chars;
  rbb->cursor += max_chars;
}


// This method determines the type of string involved and then copies
// a whole chunk of characters into a buffer, or returns a pointer to a buffer
// where they can be found.  The pointer is not necessarily valid across a GC
// (see AsciiStringReadBlock).
const unibrow::byte* String::ReadBlock(String* input,
                                       ReadBlockBuffer* rbb,
                                       unsigned* offset_ptr,
                                       unsigned max_chars) {
  ASSERT(*offset_ptr <= static_cast<unsigned>(input->length()));
  if (max_chars == 0) {
    rbb->remaining = 0;
    return NULL;
  }
  switch (input->representation_tag()) {
    case kSeqStringTag:
      if (input->is_ascii()) {
        return AsciiString::cast(input)->AsciiStringReadBlock(&rbb->remaining,
                                                              offset_ptr,
                                                              max_chars);
      } else {
        TwoByteString::cast(input)->TwoByteStringReadBlockIntoBuffer(rbb,
                                                                     offset_ptr,
                                                                     max_chars);
        return rbb->util_buffer;
      }
    case kConsStringTag:
      return ConsString::cast(input)->ConsStringReadBlock(rbb,
                                                          offset_ptr,
                                                          max_chars);
    case kSlicedStringTag:
      return SlicedString::cast(input)->SlicedStringReadBlock(rbb,
                                                              offset_ptr,
                                                              max_chars);
    case kExternalStringTag:
      if (input->is_ascii()) {
        return ExternalAsciiString::cast(input)->ExternalAsciiStringReadBlock(
            &rbb->remaining,
            offset_ptr,
            max_chars);
      } else {
        ExternalTwoByteString::cast(input)->
            ExternalTwoByteStringReadBlockIntoBuffer(rbb,
                                                     offset_ptr,
                                                     max_chars);
        return rbb->util_buffer;
      }
    default:
      break;
  }

  UNREACHABLE();
  return 0;
}


void StringInputBuffer::Seek(unsigned pos) {
  Reset(pos, input_);
}


void SafeStringInputBuffer::Seek(unsigned pos) {
  Reset(pos, input_);
}


// This method determines the type of string involved and then copies
// a whole chunk of characters into a buffer.  It can be used with strings
// that have been glued together to form a ConsString and which must cooperate
// to fill up a buffer.
void String::ReadBlockIntoBuffer(String* input,
                                 ReadBlockBuffer* rbb,
                                 unsigned* offset_ptr,
                                 unsigned max_chars) {
  ASSERT(*offset_ptr <= (unsigned)input->length());
  if (max_chars == 0) return;

  switch (input->representation_tag()) {
    case kSeqStringTag:
      if (input->is_ascii()) {
        AsciiString::cast(input)->AsciiStringReadBlockIntoBuffer(rbb,
                                                                 offset_ptr,
                                                                 max_chars);
        return;
      } else {
        TwoByteString::cast(input)->TwoByteStringReadBlockIntoBuffer(rbb,
                                                                     offset_ptr,
                                                                     max_chars);
        return;
      }
    case kConsStringTag:
      ConsString::cast(input)->ConsStringReadBlockIntoBuffer(rbb,
                                                             offset_ptr,
                                                             max_chars);
      return;
    case kSlicedStringTag:
      SlicedString::cast(input)->SlicedStringReadBlockIntoBuffer(rbb,
                                                                 offset_ptr,
                                                                 max_chars);
      return;
    case kExternalStringTag:
      if (input->is_ascii()) {
         ExternalAsciiString::cast(input)->
             ExternalAsciiStringReadBlockIntoBuffer(rbb, offset_ptr, max_chars);
       } else {
         ExternalTwoByteString::cast(input)->
             ExternalTwoByteStringReadBlockIntoBuffer(rbb,
                                                      offset_ptr,
                                                      max_chars);
       }
       return;
    default:
      break;
  }

  UNREACHABLE();
  return;
}


const unibrow::byte* String::ReadBlock(String* input,
                                       unibrow::byte* util_buffer,
                                       unsigned capacity,
                                       unsigned* remaining,
                                       unsigned* offset_ptr) {
  ASSERT(*offset_ptr <= (unsigned)input->length());
  unsigned chars = input->length() - *offset_ptr;
  ReadBlockBuffer rbb(util_buffer, 0, capacity, 0);
  const unibrow::byte* answer = ReadBlock(input, &rbb, offset_ptr, chars);
  ASSERT(rbb.remaining <= static_cast<unsigned>(input->length()));
  *remaining = rbb.remaining;
  return answer;
}


const unibrow::byte* String::ReadBlock(String** raw_input,
                                       unibrow::byte* util_buffer,
                                       unsigned capacity,
                                       unsigned* remaining,
                                       unsigned* offset_ptr) {
  Handle<String> input(raw_input);
  ASSERT(*offset_ptr <= (unsigned)input->length());
  unsigned chars = input->length() - *offset_ptr;
  if (chars > capacity) chars = capacity;
  ReadBlockBuffer rbb(util_buffer, 0, capacity, 0);
  ReadBlockIntoBuffer(*input, &rbb, offset_ptr, chars);
  ASSERT(rbb.remaining <= static_cast<unsigned>(input->length()));
  *remaining = rbb.remaining;
  return rbb.util_buffer;
}


// This will iterate unless the block of string data spans two 'halves' of
// a ConsString, in which case it will recurse.  Since the block of string
// data to be read has a maximum size this limits the maximum recursion
// depth to something sane.  Since C++ does not have tail call recursion
// elimination, the iteration must be explicit.
void ConsString::ConsStringReadBlockIntoBuffer(ReadBlockBuffer* rbb,
                                               unsigned* offset_ptr,
                                               unsigned max_chars) {
  ConsString* current = this;
  unsigned offset = *offset_ptr;
  int offset_correction = 0;

  while (true) {
    String* left = String::cast(current->first());
    unsigned left_length = (unsigned)left->length();
    if (left_length > offset &&
      max_chars <= left_length - offset) {
      // Left hand side only - iterate unless we have reached the bottom of
      // the cons tree.
      if (left->StringIsConsString()) {
        current = ConsString::cast(left);
        continue;
      } else {
        String::ReadBlockIntoBuffer(left, rbb, &offset, max_chars);
        *offset_ptr = offset + offset_correction;
        return;
      }
    } else if (left_length <= offset) {
      // Right hand side only - iterate unless we have reached the bottom of
      // the cons tree.
      offset -= left_length;
      offset_correction += left_length;
      String* right = String::cast(current->second());
      if (right->StringIsConsString()) {
        current = ConsString::cast(right);
        continue;
      } else {
        String::ReadBlockIntoBuffer(right, rbb, &offset, max_chars);
        *offset_ptr = offset + offset_correction;
        return;
      }
    } else {
      // The block to be read spans two sides of the ConsString, so we recurse.
      // First recurse on the left.
      max_chars -= left_length - offset;
      String::ReadBlockIntoBuffer(left, rbb, &offset, left_length - offset);
      // We may have reached the max or there may not have been enough space
      // in the buffer for the characters in the left hand side.
      if (offset == left_length) {
        // Recurse on the right.
        String* right = String::cast(current->second());
        offset -= left_length;
        offset_correction += left_length;
        String::ReadBlockIntoBuffer(right, rbb, &offset, max_chars);
      }
      *offset_ptr = offset + offset_correction;
      return;
    }
  }
}


void SlicedString::SlicedStringReadBlockIntoBuffer(ReadBlockBuffer* rbb,
                                                   unsigned* offset_ptr,
                                                   unsigned max_chars) {
  String* backing = String::cast(buffer());
  unsigned offset = start() + *offset_ptr;
  unsigned length = backing->length();
  if (max_chars > length - offset) {
    max_chars = length - offset;
  }
  String::ReadBlockIntoBuffer(backing, rbb, &offset, max_chars);
  *offset_ptr = offset - start();
}


void ConsString::ConsStringIterateBody(ObjectVisitor* v) {
  IteratePointers(v, kFirstOffset, kSecondOffset + kPointerSize);
}


uint16_t ConsString::ConsStringGet(int index) {
  ASSERT(index >= 0 && index < this->length());

  // Check for a flattened cons string
  if (String::cast(second())->length() == 0) {
    return String::cast(first())->Get(index);
  }

  String* string = String::cast(this);

  while (true) {
    if (string->StringIsConsString()) {
      ConsString* cons_string = ConsString::cast(string);
      String* left = String::cast(cons_string->first());
      if (left->length() > index) {
        string = left;
      } else {
        index -= left->length();
        string = String::cast(cons_string->second());
      }
    } else {
      return string->Get(index);
    }
  }

  UNREACHABLE();
  return 0;
}


Object* SlicedString::SlicedStringFlatten() {
  // The SlicedString constructor should ensure that there are no
  // SlicedStrings that are constructed directly on top of other
  // SlicedStrings.
  String* buf = String::cast(buffer());
  ASSERT(!buf->StringIsSlicedString());
  if (buf->StringIsConsString()) {
    Object* ok = buf->Flatten();
    if (ok->IsFailure()) return ok;
  }
  return this;
}


void String::Flatten(String* src, String* sink, int f, int t, int so) {
  String* source = src;
  int from = f;
  int to = t;
  int sink_offset = so;
  while (true) {
    ASSERT(0 <= from && from <= to && to <= source->length());
    ASSERT(0 <= sink_offset && sink_offset < sink->length());
    switch (source->representation_tag()) {
      case kSeqStringTag:
      case kExternalStringTag: {
        Access<StringInputBuffer> buffer(&string_input_buffer);
        buffer->Reset(from, source);
        int j = sink_offset;
        for (int i = from; i < to; i++) {
          sink->Set(j++, buffer->GetNext());
        }
        return;
      }
      case kSlicedStringTag: {
        SlicedString* sliced_string = SlicedString::cast(source);
        int start = sliced_string->start();
        from += start;
        to += start;
        source = String::cast(sliced_string->buffer());
      }
      break;
      case kConsStringTag: {
        ConsString* cons_string = ConsString::cast(source);
        String* first = String::cast(cons_string->first());
        int boundary = first->length();
        if (to - boundary > boundary - from) {
          // Right hand side is longer.  Recurse over left.
          if (from < boundary) {
            Flatten(first, sink, from, boundary, sink_offset);
            sink_offset += boundary - from;
            from = 0;
          } else {
            from -= boundary;
          }
          to -= boundary;
          source = String::cast(cons_string->second());
        } else {
          // Left hand side is longer.  Recurse over right.
          if (to > boundary) {
            String* second = String::cast(cons_string->second());
            Flatten(second,
                    sink,
                    0,
                    to - boundary,
                    sink_offset + boundary - from);
            to = boundary;
          }
          source = first;
        }
      }
      break;
    }
  }
}


void SlicedString::SlicedStringIterateBody(ObjectVisitor* v) {
  IteratePointer(v, kBufferOffset);
}


uint16_t SlicedString::SlicedStringGet(int index) {
  ASSERT(index >= 0 && index < this->length());
  // Delegate to the buffer string.
  return String::cast(buffer())->Get(start() + index);
}


bool String::SlowEquals(String* other) {
  // Fast check: negative check with lengths.
  int len = length();
  if (len != other->length()) return false;
  if (len == 0) return true;

  // Fast check: if hash code is computed for both strings
  // a fast negative check can be performed.
  if (HasHashCode() && other->HasHashCode()) {
    if (Hash() != other->Hash()) return false;
  }

  // Fast case: avoid input buffers for small strings.
  const int kMaxLenthForFastCaseCheck = 5;
  for (int i = 0; i < kMaxLenthForFastCaseCheck; i++) {
    if (Get(i) != other->Get(i)) return false;
    if (i + 1 == len) return true;
  }

  // General slow case check.
  static StringInputBuffer buf1;
  static StringInputBuffer buf2;
  buf1.Reset(kMaxLenthForFastCaseCheck, this);
  buf2.Reset(kMaxLenthForFastCaseCheck, other);
  while (buf1.has_more()) {
    if (buf1.GetNext() != buf2.GetNext()) {
      return false;
    }
  }
  return true;
}


bool String::MarkAsUndetectable() {
  if (this->IsSymbol()) return false;

  Map* map = this->map();
  if (map == Heap::short_string_map()) {
    this->set_map(Heap::undetectable_short_string_map());
    return true;
  } else if (map == Heap::medium_string_map()) {
    this->set_map(Heap::undetectable_medium_string_map());
    return true;
  } else if (map == Heap::long_string_map()) {
    this->set_map(Heap::undetectable_long_string_map());
    return true;
  } else if (map == Heap::short_ascii_string_map()) {
    this->set_map(Heap::undetectable_short_ascii_string_map());
    return true;
  } else if (map == Heap::medium_ascii_string_map()) {
    this->set_map(Heap::undetectable_medium_ascii_string_map());
    return true;
  } else if (map == Heap::long_ascii_string_map()) {
    this->set_map(Heap::undetectable_long_ascii_string_map());
    return true;
  }
  // Rest cannot be marked as undetectable
  return false;
}


bool String::IsEqualTo(Vector<const char> str) {
  int slen = length();
  Access<Scanner::Utf8Decoder> decoder(Scanner::utf8_decoder());
  decoder->Reset(str.start(), str.length());
  int i;
  for (i = 0; i < slen && decoder->has_more(); i++) {
    uc32 r = decoder->GetNext();
    if (Get(i) != r) return false;
  }
  return i == slen && !decoder->has_more();
}


uint32_t String::ComputeAndSetHash() {
  // Should only be call if hash code has not yet been computed.
  ASSERT(!(length_field() & kHashComputedMask));

  // Compute the hash code.
  StringInputBuffer buffer(this);
  int hash = ComputeHashCode(&buffer, length());

  // Store the hash code in the object.
  set_length_field(hash);

  // Check the hash code is there.
  ASSERT(length_field() & kHashComputedMask);
  return hash;
}


bool String::ComputeArrayIndex(unibrow::CharacterStream* buffer,
                               uint32_t* index,
                               int length) {
  if (length == 0) return false;
  uc32 ch = buffer->GetNext();

  // If the string begins with a '0' character, it must only consist
  // of it to be a legal array index.
  if (ch == '0') {
    *index = 0;
    return length == 1;
  }

  // Convert string to uint32 array index; character by character.
  int d = ch - '0';
  if (d < 0 || d > 9) return false;
  uint32_t result = d;
  while (buffer->has_more()) {
    d = buffer->GetNext() - '0';
    if (d < 0 || d > 9) return false;
    // Check that the new result is below the 32 bit limit.
    if (result > 429496729U - ((d > 5) ? 1 : 0)) return false;
    result = (result * 10) + d;
  }

  *index = result;
  return true;
}


bool String::SlowAsArrayIndex(uint32_t* index) {
  StringInputBuffer buffer(this);
  return ComputeArrayIndex(&buffer, index, length());
}


static inline uint32_t HashField(uint32_t hash, bool is_array_index) {
  return (hash << String::kLongLengthShift) | (is_array_index ? 3 : 1);
}


uint32_t String::ComputeHashCode(unibrow::CharacterStream* buffer,
                                 int length) {
  // Large string (please note large strings cannot be an array index).
  if (length > kMaxMediumStringSize) return HashField(length, false);

  // Note: the Jenkins one-at-a-time hash function
  uint32_t hash = 0;
  while (buffer->has_more()) {
    uc32 r = buffer->GetNext();
    hash += r;
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);

  // Short string.
  if (length <= kMaxShortStringSize) {
    // Make hash value consistent with value returned from String::Hash.
    buffer->Rewind();
    uint32_t index;
    hash = HashField(hash, ComputeArrayIndex(buffer, &index, length));
    hash = (hash & 0x00FFFFFF) | (length << kShortLengthShift);
    return hash;
  }

  // Medium string (please note medium strings cannot be an array index).
  ASSERT(length <= kMaxMediumStringSize);
  // Make hash value consistent with value returned from String::Hash.
  hash = HashField(hash, false);
  hash = (hash & 0x0000FFFF) | (length << kMediumLengthShift);
  return hash;
}


Object* String::Slice(int start, int end) {
  if (start == 0 && end == length()) return this;
  int representation = representation_tag();
  if (representation == kSlicedStringTag) {
    // Translate slices of a SlicedString into slices of the
    // underlying string buffer.
    SlicedString* str = SlicedString::cast(this);
    return Heap::AllocateSlicedString(String::cast(str->buffer()),
                                      str->start() + start,
                                      str->start() + end);
  }
  Object* answer = Heap::AllocateSlicedString(this, start, end);
  if (answer->IsFailure()) {
    return answer;
  }
  // Due to the way we retry after GC on allocation failure we are not allowed
  // to fail on allocation after this point.  This is the one-allocation rule.

  // Try to flatten a cons string that is under the sliced string.
  // This is to avoid memory leaks and possible stack overflows caused by
  // building 'towers' of sliced strings on cons strings.
  // This may fail due to an allocation failure (when a GC is needed), but it
  // will succeed often enough to avoid the problem.  We only have to do this
  // if Heap::AllocateSlicedString actually returned a SlicedString.  It will
  // return flat strings for small slices for efficiency reasons.
  if (String::cast(answer)->StringIsSlicedString() &&
      representation == kConsStringTag) {
    TryFlatten();
    // If the flatten succeeded we might as well make the sliced string point
    // to the flat string rather than the cons string.
    if (String::cast(ConsString::cast(this)->second())->length() == 0) {
      SlicedString::cast(answer)->set_buffer(ConsString::cast(this)->first());
    }
  }
  return answer;
}


void String::PrintOn(FILE* file) {
  int length = this->length();
  for (int i = 0; i < length; i++) {
    fprintf(file, "%c", Get(i));
  }
}


void Map::MapIterateBody(ObjectVisitor* v) {
  // Assumes all Object* members are contiguously allocated!
  IteratePointers(v, kPrototypeOffset, kCodeCacheOffset + kPointerSize);
}


int JSFunction::NumberOfLiterals() {
  return literals()->length();
}


Object* JSFunction::SetInstancePrototype(Object* value) {
  ASSERT(value->IsJSObject());

  if (has_initial_map()) {
    initial_map()->set_prototype(value);
  } else {
    // Put the value in the initial map field until an initial map is
    // needed.  At that point, a new initial map is created and the
    // prototype is put into the initial map where it belongs.
    set_prototype_or_initial_map(value);
  }
  return value;
}



Object* JSFunction::SetPrototype(Object* value) {
  Object* construct_prototype = value;

  // If the value is not a JSObject, store the value in the map's
  // constructor field so it can be accessed.  Also, set the prototype
  // used for constructing objects to the original object prototype.
  // See ECMA-262 13.2.2.
  if (!value->IsJSObject()) {
    // Copy the map so this does not affect unrelated functions.
    // Remove map transitions so we do not lose the prototype
    // information on map transitions.
    Object* copy = map()->Copy();
    if (copy->IsFailure()) return copy;
    Object* new_map = Map::cast(copy)->EnsureNoMapTransitions();
    if (new_map->IsFailure()) return new_map;
    set_map(Map::cast(new_map));

    map()->set_constructor(value);
    map()->set_non_instance_prototype(true);
    construct_prototype = *Top::initial_object_prototype();
  } else {
    map()->set_non_instance_prototype(false);
  }

  return SetInstancePrototype(construct_prototype);
}


Object* JSFunction::SetInstanceClassName(String* name) {
  shared()->set_instance_class_name(name);
  return this;
}


void Oddball::OddballIterateBody(ObjectVisitor* v) {
  // Assumes all Object* members are contiguously allocated!
  IteratePointers(v, kToStringOffset, kToNumberOffset + kPointerSize);
}


Object* Oddball::Initialize(const char* to_string, Object* to_number) {
  Object* symbol = Heap::LookupAsciiSymbol(to_string);
  if (symbol->IsFailure()) return symbol;
  set_to_string(String::cast(symbol));
  set_to_number(to_number);
  return this;
}


bool SharedFunctionInfo::HasSourceCode() {
  return !script()->IsUndefined() &&
         !Script::cast(script())->source()->IsUndefined();
}


Object* SharedFunctionInfo::GetSourceCode() {
  HandleScope scope;
  if (script()->IsUndefined()) return Heap::undefined_value();
  Object* source = Script::cast(script())->source();
  if (source->IsUndefined()) return Heap::undefined_value();
  return *SubString(Handle<String>(String::cast(source)),
                    start_position(), end_position());
}


// Support function for printing the source code to a StringStream
// without any allocation in the heap.
void SharedFunctionInfo::SourceCodePrint(StringStream* accumulator,
                                         int max_length) {
  // For some native functions there is no source.
  if (script()->IsUndefined() ||
      Script::cast(script())->source()->IsUndefined()) {
    accumulator->Add("<No Source>");
    return;
  }

  // Get the slice of the source for this function.
  // Don't use String::cast because we don't want more assertion errors while
  // we are already creating a stack dump.
  String* script_source =
      reinterpret_cast<String*>(Script::cast(script())->source());

  if (!script_source->LooksValid()) {
    accumulator->Add("<Invalid Source>");
    return;
  }

  if (!is_toplevel()) {
    accumulator->Add("function ");
    Object* name = this->name();
    if (name->IsString() && String::cast(name)->length() > 0) {
      accumulator->PrintName(name);
    }
  }

  int len = end_position() - start_position();
  if (len > max_length) {
    accumulator->Put(script_source,
                     start_position(),
                     start_position() + max_length);
    accumulator->Add("...\n");
  } else {
    accumulator->Put(script_source, start_position(), end_position());
  }
}


void SharedFunctionInfo::SharedFunctionInfoIterateBody(ObjectVisitor* v) {
  IteratePointers(v, kNameOffset, kCodeOffset + kPointerSize);
  IteratePointers(v, kInstanceClassNameOffset, kScriptOffset + kPointerSize);
  IteratePointer(v, kDebugInfoOffset);
}


void ObjectVisitor::BeginCodeIteration(Code* code) {
  ASSERT(code->ic_flag() == Code::IC_TARGET_IS_OBJECT);
}


void ObjectVisitor::VisitCodeTarget(RelocInfo* rinfo) {
  ASSERT(is_code_target(rinfo->rmode()));
  VisitPointer(rinfo->target_object_address());
}


void ObjectVisitor::VisitDebugTarget(RelocInfo* rinfo) {
  ASSERT(is_js_return(rinfo->rmode()) && rinfo->is_call_instruction());
  VisitPointer(rinfo->call_object_address());
}


// Convert relocatable targets from address to code object address. This is
// mainly IC call targets but for debugging straight-line code can be replaced
// with a call instruction which also has to be relocated.
void Code::ConvertICTargetsFromAddressToObject() {
  ASSERT(ic_flag() == IC_TARGET_IS_ADDRESS);

  for (RelocIterator it(this, RelocInfo::kCodeTargetMask);
       !it.done(); it.next()) {
    Address ic_addr = it.rinfo()->target_address();
    ASSERT(ic_addr != NULL);
    HeapObject* code = HeapObject::FromAddress(ic_addr - Code::kHeaderSize);
    ASSERT(code->IsHeapObject());
    it.rinfo()->set_target_object(code);
  }

  if (Debug::has_break_points()) {
    for (RelocIterator it(this, RelocMask(js_return)); !it.done(); it.next()) {
      if (it.rinfo()->is_call_instruction()) {
        Address addr = it.rinfo()->call_address();
        ASSERT(addr != NULL);
        HeapObject* code = HeapObject::FromAddress(addr - Code::kHeaderSize);
        ASSERT(code->IsHeapObject());
        it.rinfo()->set_call_object(code);
      }
    }
  }
  set_ic_flag(IC_TARGET_IS_OBJECT);
}


void Code::CodeIterateBody(ObjectVisitor* v) {
  v->BeginCodeIteration(this);

  int mode_mask = RelocInfo::kCodeTargetMask |
                  RelocMask(embedded_object) |
                  RelocMask(external_reference) |
                  RelocMask(js_return) |
                  RelocMask(runtime_entry);

  for (RelocIterator it(this, mode_mask); !it.done(); it.next()) {
    RelocMode rmode = it.rinfo()->rmode();
    if (rmode == embedded_object) {
      v->VisitPointer(it.rinfo()->target_object_address());
    } else if (is_code_target(rmode)) {
      v->VisitCodeTarget(it.rinfo());
    } else if (rmode == external_reference) {
      v->VisitExternalReference(it.rinfo()->target_reference_address());
    } else if (Debug::has_break_points() &&
               is_js_return(rmode) && it.rinfo()->is_call_instruction()) {
      v->VisitDebugTarget(it.rinfo());
    } else if (rmode == runtime_entry) {
      v->VisitRuntimeEntry(it.rinfo());
    }
  }

  ScopeInfo<>::IterateScopeInfo(this, v);

  v->EndCodeIteration(this);
}


void Code::ConvertICTargetsFromObjectToAddress() {
  ASSERT(ic_flag() == IC_TARGET_IS_OBJECT);

  for (RelocIterator it(this, RelocInfo::kCodeTargetMask);
       !it.done(); it.next()) {
    // We cannot use the safe cast (Code::cast) here, because we may be in
    // the middle of relocating old objects during GC and the map pointer in
    // the code object may be mangled
    Code* code = reinterpret_cast<Code*>(it.rinfo()->target_object());
    ASSERT((code != NULL) && code->IsHeapObject());
    it.rinfo()->set_target_address(code->instruction_start());
  }

  if (Debug::has_break_points()) {
    for (RelocIterator it(this, RelocMask(js_return)); !it.done(); it.next()) {
      if (it.rinfo()->is_call_instruction()) {
        Code* code = reinterpret_cast<Code*>(it.rinfo()->call_object());
        ASSERT((code != NULL) && code->IsHeapObject());
        it.rinfo()->set_call_address(code->instruction_start());
      }
    }
  }
  set_ic_flag(IC_TARGET_IS_ADDRESS);
}


void Code::Relocate(int delta) {
  for (RelocIterator it(this, RelocInfo::kApplyMask); !it.done(); it.next()) {
    it.rinfo()->apply(delta);
  }
}


void Code::CopyFrom(const CodeDesc& desc) {
  // copy code
  memmove(instruction_start(), desc.buffer, desc.instr_size);

  // fill gap with zero bytes
  { byte* p = instruction_start() + desc.instr_size;
    byte* q = relocation_start();
    while (p < q) {
      *p++ = 0;
    }
  }

  // copy reloc info
  memmove(relocation_start(),
          desc.buffer + desc.buffer_size - desc.reloc_size,
          desc.reloc_size);

  // unbox handles and relocate
  int delta = instruction_start() - desc.buffer;
  int mode_mask = RelocInfo::kCodeTargetMask |
                  RelocMask(embedded_object) |
                  RelocInfo::kApplyMask;
  for (RelocIterator it(this, mode_mask); !it.done(); it.next()) {
    RelocMode mode = it.rinfo()->rmode();
    if (mode == embedded_object) {
      Object** p = reinterpret_cast<Object**>(it.rinfo()->target_object());
      it.rinfo()->set_target_object(*p);
    } else if (is_code_target(mode)) {
      // rewrite code handles in inline cache targets to direct
      // pointers to the first instruction in the code object
      Object** p = reinterpret_cast<Object**>(it.rinfo()->target_object());
      Code* code = Code::cast(*p);
      it.rinfo()->set_target_address(code->instruction_start());
    } else {
      it.rinfo()->apply(delta);
    }
  }
}


// Locate the source position which is closest to the address in the code. This
// is using the source position information embedded in the relocation info.
// The position returned is relative to the beginning of the script where the
// source for this function is found.
int Code::SourcePosition(Address pc) {
  int distance = kMaxInt;
  int position = kNoPosition;  // Initially no position found.
  // Run through all the relocation info to find the best matching source
  // position. All the code needs to be considered as the sequence of the
  // instructions in the code does not necessarily follow the same order as the
  // source.
  RelocIterator it(this, RelocInfo::kPositionMask);
  while (!it.done()) {
    if (it.rinfo()->pc() < pc && (pc - it.rinfo()->pc()) < distance) {
      position = it.rinfo()->data();
      distance = pc - it.rinfo()->pc();
    }
    it.next();
  }
  return position;
}


// Same as Code::SourcePosition above except it only looks for statement
// positions.
int Code::SourceStatementPosition(Address pc) {
  // First find the position as close as possible using all position
  // information.
  int position = SourcePosition(pc);
  // Now find the closest statement position before the position.
  int statement_position = 0;
  RelocIterator it(this, RelocInfo::kPositionMask);
  while (!it.done()) {
    if (is_statement_position(it.rinfo()->rmode())) {
      int p = it.rinfo()->data();
      if (statement_position < p && p <= position) {
        statement_position = p;
      }
    }
    it.next();
  }
  return statement_position;
}


void JSObject::SetFastElements(FixedArray* elems) {
#ifdef DEBUG
  // Check the provided array is filled with the_hole.
  uint32_t len = static_cast<uint32_t>(elems->length());
  for (uint32_t i = 0; i < len; i++) ASSERT(elems->get(i)->IsTheHole());
#endif
  FixedArray::WriteBarrierMode mode = elems->GetWriteBarrierMode();
  if (HasFastElements()) {
    FixedArray* old_elements = FixedArray::cast(elements());
    uint32_t old_length = static_cast<uint32_t>(old_elements->length());
    // Fill out the new array with this content and array holes.
    for (uint32_t i = 0; i < old_length; i++) {
      elems->set(i, old_elements->get(i), mode);
    }
  } else {
    Dictionary* dictionary = Dictionary::cast(elements());
    for (int i = 0; i < dictionary->Capacity(); i++) {
      Object* key = dictionary->KeyAt(i);
      if (key->IsNumber()) {
        uint32_t entry = static_cast<uint32_t>(key->Number());
        elems->set(entry, dictionary->ValueAt(i), mode);
      }
    }
  }
  set_elements(elems);
}


Object* JSObject::SetSlowElements(Object* len) {
  uint32_t new_length = static_cast<uint32_t>(len->Number());

  if (!HasFastElements()) {
    if (IsJSArray()) {
      uint32_t old_length =
          static_cast<uint32_t>(JSArray::cast(this)->length()->Number());
      element_dictionary()->RemoveNumberEntries(new_length, old_length),
      JSArray::cast(this)->set_length(len);
    }
    return this;
  }

  // Make sure we never try to shrink dense arrays into sparse arrays.
  ASSERT(static_cast<uint32_t>(FixedArray::cast(elements())->length()) <=
                               new_length);
  Object* obj = NormalizeElements();
  if (obj->IsFailure()) return obj;

  // Update length for JSArrays.
  if (IsJSArray()) JSArray::cast(this)->set_length(len);
  return this;
}


Object* JSArray::Initialize(int capacity) {
  ASSERT(capacity >= 0);
  set_length(Smi::FromInt(0));
  FixedArray* new_elements;
  if (capacity == 0) {
    new_elements = Heap::empty_fixed_array();
  } else {
    Object* obj = Heap::AllocateFixedArrayWithHoles(capacity);
    if (obj->IsFailure()) return obj;
    new_elements = FixedArray::cast(obj);
  }
  set_elements(new_elements);
  return this;
}


void JSArray::SetContent(FixedArray* storage) {
  set_length(Smi::FromInt(storage->length()));
  set_elements(storage);
}


// Computes the new capacity when expanding the elements of a JSObject.
static int NewElementsCapacity(int old_capacity) {
  // (old_capacity + 50%) + 16
  return old_capacity + (old_capacity >> 1) + 16;
}


static Object* ArrayLengthRangeError() {
  HandleScope scope;
  return Top::Throw(*Factory::NewRangeError("invalid_array_length",
                                            HandleVector<Object>(NULL, 0)));
}


Object* JSObject::SetElementsLength(Object* len) {
  Object* smi_length = len->ToSmi();
  if (smi_length->IsSmi()) {
    int value = Smi::cast(smi_length)->value();
    if (value < 0) return ArrayLengthRangeError();
    if (HasFastElements()) {
      int old_capacity = FixedArray::cast(elements())->length();
      if (value <= old_capacity) {
        if (IsJSArray()) {
          int old_length = FastD2I(JSArray::cast(this)->length()->Number());
          // NOTE: We may be able to optimize this by removing the
          // last part of the elements backing storage array and
          // setting the capacity to the new size.
          for (int i = value; i < old_length; i++) {
            FixedArray::cast(elements())->set_the_hole(i);
          }
          JSArray::cast(this)->set_length(smi_length);
        }
        return this;
      }
      int min = NewElementsCapacity(old_capacity);
      int new_capacity = value > min ? value : min;
      if (KeepInFastCase(new_capacity) ||
          new_capacity <= kMaxFastElementsLength) {
        Object* obj = Heap::AllocateFixedArrayWithHoles(new_capacity);
        if (obj->IsFailure()) return obj;
        if (IsJSArray()) JSArray::cast(this)->set_length(smi_length);
        SetFastElements(FixedArray::cast(obj));
        return this;
      }
    } else {
      if (IsJSArray()) {
        if (value == 0) {
          // If the length of a slow array is reset to zero, we clear
          // the array and flush backing storage. This has the added
          // benefit that the array returns to fast mode.
          initialize_elements();
        } else {
          // Remove deleted elements.
          uint32_t old_length =
              static_cast<uint32_t>(JSArray::cast(this)->length()->Number());
          element_dictionary()->RemoveNumberEntries(value, old_length);
        }
        JSArray::cast(this)->set_length(smi_length);
      }
      return this;
    }
  }

  // General slow case.
  if (len->IsNumber()) {
    uint32_t length;
    if (Array::IndexFromObject(len, &length)) {
      return SetSlowElements(len);
    } else {
      return ArrayLengthRangeError();
    }
  }

  // len is not a number so make the array size one and
  // set only element to len.
  Object* obj = Heap::AllocateFixedArray(1);
  if (obj->IsFailure()) return obj;
  FixedArray::cast(obj)->set(0, len);
  if (IsJSArray()) JSArray::cast(this)->set_length(Smi::FromInt(1));
  set_elements(FixedArray::cast(obj));
  return this;
}


bool JSObject::HasElementPostInterceptor(JSObject* receiver, uint32_t index) {
  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
        static_cast<uint32_t>(
            Smi::cast(JSArray::cast(this)->length())->value()) :
        static_cast<uint32_t>(FixedArray::cast(elements())->length());
    if ((index < length) &&
        !FixedArray::cast(elements())->get(index)->IsTheHole()) {
      return true;
    }
  } else {
    if (element_dictionary()->FindNumberEntry(index) != -1) return true;
  }

  // Handle [] on String objects.
  if (this->IsStringObjectWithCharacterAt(index)) return true;

  Object* pt = GetPrototype();
  if (pt == Heap::null_value()) return false;
  return JSObject::cast(pt)->HasElementWithReceiver(receiver, index);
}


bool JSObject::HasElementWithInterceptor(JSObject* receiver, uint32_t index) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetIndexedInterceptor());
  Handle<JSObject> receiver_handle(receiver);
  Handle<JSObject> holder_handle(this);
  Handle<Object> data_handle(interceptor->data());
  v8::AccessorInfo info(v8::Utils::ToLocal(receiver_handle),
                        v8::Utils::ToLocal(data_handle),
                        v8::Utils::ToLocal(holder_handle));
  if (!interceptor->query()->IsUndefined()) {
    v8::IndexedPropertyQuery query =
        v8::ToCData<v8::IndexedPropertyQuery>(interceptor->query());
    LOG(ApiIndexedPropertyAccess("interceptor-indexed-has", this, index));
    v8::Handle<v8::Boolean> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = query(index, info);
    }
    if (!result.IsEmpty()) return result->IsTrue();
  } else if (!interceptor->getter()->IsUndefined()) {
    v8::IndexedPropertyGetter getter =
        v8::ToCData<v8::IndexedPropertyGetter>(interceptor->getter());
    LOG(ApiIndexedPropertyAccess("interceptor-indexed-has-get", this, index));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = getter(index, info);
    }
    if (!result.IsEmpty()) return !result->IsUndefined();
  }
  return holder_handle->HasElementPostInterceptor(*receiver_handle, index);
}


bool JSObject::HasLocalElement(uint32_t index) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayIndexedAccess(this, index, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return false;
  }

  // Check for lookup interceptor
  if (HasIndexedInterceptor()) {
    return HasElementWithInterceptor(this, index);
  }

  // Handle [] on String objects.
  if (this->IsStringObjectWithCharacterAt(index)) return true;

  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
        static_cast<uint32_t>(
            Smi::cast(JSArray::cast(this)->length())->value()) :
        static_cast<uint32_t>(FixedArray::cast(elements())->length());
    return (index < length) &&
           !FixedArray::cast(elements())->get(index)->IsTheHole();
  } else {
    return element_dictionary()->FindNumberEntry(index) != -1;
  }
}


bool JSObject::HasElementWithReceiver(JSObject* receiver, uint32_t index) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayIndexedAccess(this, index, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return false;
  }

  // Check for lookup interceptor
  if (HasIndexedInterceptor()) {
    return HasElementWithInterceptor(receiver, index);
  }

  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
        static_cast<uint32_t>(
            Smi::cast(JSArray::cast(this)->length())->value()) :
        static_cast<uint32_t>(FixedArray::cast(elements())->length());
    if ((index < length) &&
        !FixedArray::cast(elements())->get(index)->IsTheHole()) return true;
  } else {
    if (element_dictionary()->FindNumberEntry(index) != -1) return true;
  }

  // Handle [] on String objects.
  if (this->IsStringObjectWithCharacterAt(index)) return true;

  Object* pt = GetPrototype();
  if (pt == Heap::null_value()) return false;
  return JSObject::cast(pt)->HasElementWithReceiver(receiver, index);
}


Object* JSObject::SetElementPostInterceptor(uint32_t index, Object* value) {
  if (HasFastElements()) return SetFastElement(index, value);

  // Dictionary case.
  ASSERT(!HasFastElements());

  FixedArray* elms = FixedArray::cast(elements());
  Object* result = Dictionary::cast(elms)->AtNumberPut(index, value);
  if (result->IsFailure()) return result;
  if (elms != FixedArray::cast(result)) {
    set_elements(FixedArray::cast(result));
  }

  if (IsJSArray()) {
    return JSArray::cast(this)->JSArrayUpdateLengthFromIndex(index, value);
  }

  return value;
}


Object* JSObject::SetElementWithInterceptor(uint32_t index, Object* value) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetIndexedInterceptor());
  Handle<JSObject> this_handle(this);
  Handle<Object> value_handle(value);
  if (!interceptor->setter()->IsUndefined()) {
    v8::IndexedPropertySetter setter =
        v8::ToCData<v8::IndexedPropertySetter>(interceptor->setter());
    Handle<Object> data_handle(interceptor->data());
    LOG(ApiIndexedPropertyAccess("interceptor-indexed-set", this, index));
    v8::AccessorInfo info(v8::Utils::ToLocal(this_handle),
                          v8::Utils::ToLocal(data_handle),
                          v8::Utils::ToLocal(this_handle));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = setter(index, v8::Utils::ToLocal(value_handle), info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!result.IsEmpty()) return *value_handle;
  }
  Object* raw_result =
      this_handle->SetElementPostInterceptor(index, *value_handle);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


// Adding n elements in fast case is O(n*n).
// Note: revisit design to have dual undefined values to capture absent
// elements.
Object* JSObject::SetFastElement(uint32_t index, Object* value) {
  ASSERT(HasFastElements());

  FixedArray* elms = FixedArray::cast(elements());
  uint32_t elms_length = static_cast<uint32_t>(elms->length());

  // Check whether there is extra space in fixed array..
  if (index < elms_length) {
    elms->set(index, value);
    if (IsJSArray()) {
      // Update the length of the array if needed.
      uint32_t array_length = 0;
      CHECK(Array::IndexFromObject(JSArray::cast(this)->length(),
                                   &array_length));
      if (index >= array_length) {
        JSArray::cast(this)->set_length(Smi::FromInt(index + 1));
      }
    }
    return this;
  }

  // Allow gap in fast case.
  if ((index - elms_length) < kMaxGap) {
    // Try allocating extra space.
    int new_capacity = NewElementsCapacity(index+1);
    if (KeepInFastCase(new_capacity) ||
        new_capacity <= kMaxFastElementsLength) {
      ASSERT(static_cast<uint32_t>(new_capacity) > index);
      Object* obj = Heap::AllocateFixedArrayWithHoles(new_capacity);
      if (obj->IsFailure()) return obj;
      SetFastElements(FixedArray::cast(obj));
      if (IsJSArray()) JSArray::cast(this)->set_length(Smi::FromInt(index + 1));
      FixedArray::cast(elements())->set(index, value);
      return this;
    }
  }

  // Otherwise default to slow case.
  Object* obj = NormalizeElements();
  if (obj->IsFailure()) return obj;
  ASSERT(!HasFastElements());
  return SetElement(index, value);
}


Object* JSObject::SetElement(uint32_t index, Object* value) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayIndexedAccess(this, index, v8::ACCESS_SET)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_SET);
    return value;
  }

  // Check for lookup interceptor
  if (HasIndexedInterceptor()) {
    return SetElementWithInterceptor(index, value);
  }

  // Fast case.
  if (HasFastElements()) return SetFastElement(index, value);

  // Dictionary case.
  ASSERT(!HasFastElements());

  // Insert element in the dictionary.
  FixedArray* elms = FixedArray::cast(elements());
  Dictionary* dictionary = Dictionary::cast(elms);
  Object* result = dictionary->AtNumberPut(index, value);
  if (result->IsFailure()) return result;
  if (elms != FixedArray::cast(result)) {
    set_elements(FixedArray::cast(result));
  }

  // Update the array length if this JSObject is an array.
  if (IsJSArray()) {
    JSArray* array = JSArray::cast(this);
    Object* return_value = array->JSArrayUpdateLengthFromIndex(index, value);
    if (return_value->IsFailure()) return return_value;
  }

  // Attempt to put this object back in fast case.
  if (ShouldHaveFastElements()) {
    uint32_t new_length = 0;
    if (IsJSArray()) {
      CHECK(Array::IndexFromObject(JSArray::cast(this)->length(), &new_length));
    } else {
      new_length = Dictionary::cast(elements())->max_number_key() + 1;
    }
    Object* obj = Heap::AllocateFixedArrayWithHoles(new_length);
    if (obj->IsFailure()) return obj;
    SetFastElements(FixedArray::cast(obj));
#ifdef DEBUG
    if (FLAG_trace_normalization) {
      PrintF("Object elements are fast case again:\n");
      Print();
    }
#endif
  }

  return value;
}


Object* JSArray::JSArrayUpdateLengthFromIndex(uint32_t index, Object* value) {
  uint32_t old_len = 0;
  CHECK(Array::IndexFromObject(length(), &old_len));
  // Check to see if we need to update the length. For now, we make
  // sure that the length stays within 32-bits (unsigned).
  if (index >= old_len && index != 0xffffffff) {
    Object* len =
        Heap::NumberFromDouble(static_cast<double>(index) + 1);
    if (len->IsFailure()) return len;
    set_length(len);
  }
  return value;
}


Object* JSObject::GetElementPostInterceptor(JSObject* receiver,
                                            uint32_t index) {
  // Get element works for both JSObject and JSArray since
  // JSArray::length cannot change.
  if (HasFastElements()) {
    FixedArray* elms = FixedArray::cast(elements());
    if (index < static_cast<uint32_t>(elms->length())) {
      Object* value = elms->get(index);
      if (!value->IsTheHole()) return value;
    }
  } else {
    Dictionary* dictionary = element_dictionary();
    int entry = dictionary->FindNumberEntry(index);
    if (entry != -1) {
      return dictionary->ValueAt(entry);
    }
  }

  // Continue searching via the prototype chain.
  Object* pt = GetPrototype();
  if (pt == Heap::null_value()) return Heap::undefined_value();
  return pt->GetElementWithReceiver(receiver, index);
}


Object* JSObject::GetElementWithInterceptor(JSObject* receiver,
                                            uint32_t index) {
  // Make sure that the top context does not change when doing
  // callbacks or interceptor calls.
  AssertNoContextChange ncc;
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetIndexedInterceptor());
  Handle<JSObject> this_handle(receiver);
  Handle<JSObject> holder_handle(this);

  if (!interceptor->getter()->IsUndefined()) {
    Handle<Object> data_handle(interceptor->data());
    v8::IndexedPropertyGetter getter =
        v8::ToCData<v8::IndexedPropertyGetter>(interceptor->getter());
    LOG(ApiIndexedPropertyAccess("interceptor-indexed-get", this, index));
    v8::AccessorInfo info(v8::Utils::ToLocal(this_handle),
                          v8::Utils::ToLocal(data_handle),
                          v8::Utils::ToLocal(holder_handle));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = getter(index, info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!result.IsEmpty()) return *v8::Utils::OpenHandle(*result);
  }

  Object* raw_result =
      holder_handle->GetElementPostInterceptor(*this_handle, index);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


Object* JSObject::GetElementWithReceiver(JSObject* receiver, uint32_t index) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayIndexedAccess(this, index, v8::ACCESS_GET)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_GET);
    return Heap::undefined_value();
  }

  if (HasIndexedInterceptor()) {
    return GetElementWithInterceptor(receiver, index);
  }

  // Get element works for both JSObject and JSArray since
  // JSArray::length cannot change.
  if (HasFastElements()) {
    FixedArray* elms = FixedArray::cast(elements());
    if (index < static_cast<uint32_t>(elms->length())) {
      Object* value = elms->get(index);
      if (!value->IsTheHole()) return value;
    }
  } else {
    Dictionary* dictionary = element_dictionary();
    int entry = dictionary->FindNumberEntry(index);
    if (entry != -1) {
      return dictionary->ValueAt(entry);
    }
  }

  Object* pt = GetPrototype();
  if (pt == Heap::null_value()) return Heap::undefined_value();
  return pt->GetElementWithReceiver(receiver, index);
}


bool JSObject::HasDenseElements() {
  int capacity = 0;
  int number_of_elements = 0;

  if (HasFastElements()) {
    FixedArray* elms = FixedArray::cast(elements());
    capacity = elms->length();
    for (int i = 0; i < capacity; i++) {
      if (!elms->get(i)->IsTheHole()) number_of_elements++;
    }
  } else {
    Dictionary* dictionary = Dictionary::cast(elements());
    capacity = dictionary->Capacity();
    number_of_elements = dictionary->NumberOfElements();
  }

  if (capacity == 0) return true;
  return (number_of_elements > (capacity / 2));
}


bool JSObject::KeepInFastCase(int new_capacity) {
  ASSERT(HasFastElements());
  // Keep the array in fast case if the current backing storage is
  // almost filled and if the new capacity is no more than twice the
  // old capacity.
  int elements_length = FixedArray::cast(elements())->length();
  return HasDenseElements() && ((new_capacity / 2) <= elements_length);
}


bool JSObject::ShouldHaveFastElements() {
  ASSERT(!HasFastElements());
  Dictionary* dictionary = Dictionary::cast(elements());
  // If the elements are sparse, we should not go back to fast case.
  if (!HasDenseElements()) return false;
  // If an element has been added at a very high index in the elements
  // dictionary, we cannot go back to fast case.
  if (dictionary->requires_slow_elements()) return false;
  // An object requiring access checks is never allowed to have fast
  // elements.  If it had fast elements we would skip security checks.
  if (IsAccessCheckNeeded()) return false;
  // If the dictionary backing storage takes up roughly half as much
  // space as a fast-case backing storage would the array should have
  // fast elements.
  uint32_t length = 0;
  if (IsJSArray()) {
    CHECK(Array::IndexFromObject(JSArray::cast(this)->length(), &length));
  } else {
    length = dictionary->max_number_key();
  }
  return static_cast<uint32_t>(dictionary->Capacity()) >=
      (length / (2 * Dictionary::kElementSize));
}


Object* Dictionary::RemoveHoles() {
  int capacity = Capacity();
  Object* obj = Allocate(NumberOfElements());
  if (obj->IsFailure()) return obj;
  Dictionary* dict = Dictionary::cast(obj);
  uint32_t pos = 0;
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      dict->AddNumberEntry(pos++, ValueAt(i), DetailsAt(i));
    }
  }
  return dict;
}


void Dictionary::CopyValuesTo(FixedArray* elements) {
  int pos = 0;
  int capacity = Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) elements->set(pos++, ValueAt(i));
  }
  ASSERT(pos == elements->length());
}


Object* JSArray::RemoveHoles() {
  if (HasFastElements()) {
    int len = Smi::cast(length())->value();
    int pos = 0;
    FixedArray* elms = FixedArray::cast(elements());
    for (int index = 0; index < len; index++) {
      Object* e = elms->get(index);
      if (!e->IsTheHole()) {
        if (index != pos) elms->set(pos, e);
        pos++;
      }
    }
    set_length(Smi::FromInt(pos));
    for (int index = pos; index < len; index++) {
      elms->set_the_hole(index);
    }
    return this;
  }

  // Compact the sparse array if possible.
  Dictionary* dict = element_dictionary();
  int length = dict->NumberOfElements();

  // Try to make this a fast array again.
  if (length <= kMaxFastElementsLength) {
    Object* obj = Heap::AllocateFixedArray(length);
    if (obj->IsFailure()) return obj;
    dict->CopyValuesTo(FixedArray::cast(obj));
    set_length(Smi::FromInt(length));
    set_elements(FixedArray::cast(obj));
    return this;
  }

  // Make another dictionary with smaller indices.
  Object* obj = dict->RemoveHoles();
  if (obj->IsFailure()) return obj;
  set_length(Smi::FromInt(length));
  set_elements(Dictionary::cast(obj));
  return this;
}


InterceptorInfo* JSObject::GetNamedInterceptor() {
  ASSERT(map()->has_named_interceptor());
  JSFunction* constructor = JSFunction::cast(map()->constructor());
  Object* template_info = constructor->shared()->function_data();
  Object* result =
      FunctionTemplateInfo::cast(template_info)->named_property_handler();
  return InterceptorInfo::cast(result);
}


InterceptorInfo* JSObject::GetIndexedInterceptor() {
  ASSERT(map()->has_indexed_interceptor());
  JSFunction* constructor = JSFunction::cast(map()->constructor());
  Object* template_info = constructor->shared()->function_data();
  Object* result =
      FunctionTemplateInfo::cast(template_info)->indexed_property_handler();
  return InterceptorInfo::cast(result);
}


Object* JSObject::GetPropertyPostInterceptor(JSObject* receiver,
                                             String* name,
                                             PropertyAttributes* attributes) {
  // Check local property in holder, ignore interceptor.
  LookupResult result;
  LocalLookupRealNamedProperty(name, &result);
  if (result.IsValid()) return GetProperty(receiver, &result, name, attributes);
  // Continue searching via the prototype chain.
  Object* pt = GetPrototype();
  *attributes = ABSENT;
  if (pt == Heap::null_value()) return Heap::undefined_value();
  return pt->GetPropertyWithReceiver(receiver, name, attributes);
}


Object* JSObject::GetPropertyWithInterceptor(JSObject* receiver,
                                             String* name,
                                             PropertyAttributes* attributes) {
  HandleScope scope;
  Handle<InterceptorInfo> interceptor(GetNamedInterceptor());
  Handle<JSObject> receiver_handle(receiver);
  Handle<JSObject> holder_handle(this);
  Handle<String> name_handle(name);
  Handle<Object> data_handle(interceptor->data());

  if (!interceptor->getter()->IsUndefined()) {
    v8::NamedPropertyGetter getter =
        v8::ToCData<v8::NamedPropertyGetter>(interceptor->getter());
    LOG(ApiNamedPropertyAccess("interceptor-named-get", *holder_handle, name));
    v8::AccessorInfo info(v8::Utils::ToLocal(receiver_handle),
                          v8::Utils::ToLocal(data_handle),
                          v8::Utils::ToLocal(holder_handle));
    v8::Handle<v8::Value> result;
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = getter(v8::Utils::ToLocal(name_handle), info);
    }
    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!result.IsEmpty()) {
      *attributes = NONE;
      return *v8::Utils::OpenHandle(*result);
    }
  }

  Object* raw_result = holder_handle->GetPropertyPostInterceptor(
      *receiver_handle,
      *name_handle,
      attributes);
  RETURN_IF_SCHEDULED_EXCEPTION();
  return raw_result;
}


bool JSObject::HasRealNamedProperty(String* key) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, key, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return false;
  }

  LookupResult result;
  LocalLookupRealNamedProperty(key, &result);
  if (result.IsValid()) {
    switch (result.type()) {
      case NORMAL:    // fall through.
      case FIELD:     // fall through.
      case CALLBACKS:  // fall through.
      case CONSTANT_FUNCTION: return true;
      default: return false;
    }
  }

  return false;
}


bool JSObject::HasRealElementProperty(uint32_t index) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayIndexedAccess(this, index, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return false;
  }

  // Handle [] on String objects.
  if (this->IsStringObjectWithCharacterAt(index)) return true;

  if (HasFastElements()) {
    uint32_t length = IsJSArray() ?
        static_cast<uint32_t>(
            Smi::cast(JSArray::cast(this)->length())->value()) :
        static_cast<uint32_t>(FixedArray::cast(elements())->length());
    return (index < length) &&
        !FixedArray::cast(elements())->get(index)->IsTheHole();
  }
  return element_dictionary()->FindNumberEntry(index) != -1;
}


bool JSObject::HasRealNamedCallbackProperty(String* key) {
  // Check access rights if needed.
  if (IsAccessCheckNeeded() &&
      !Top::MayNamedAccess(this, key, v8::ACCESS_HAS)) {
    Top::ReportFailedAccessCheck(this, v8::ACCESS_HAS);
    return false;
  }

  LookupResult result;
  LocalLookupRealNamedProperty(key, &result);
  return result.IsValid() && (result.type() == CALLBACKS);
}


int JSObject::NumberOfLocalProperties(PropertyAttributes filter) {
  if (HasFastProperties()) {
    int result = 0;
    for (DescriptorReader r(map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      PropertyDetails details = r.GetDetails();
      if (!details.IsTransition() && (details.attributes() & filter) == 0) {
        result++;
      }
    }
    return result;
  } else {
    return property_dictionary()->NumberOfElementsFilterAttributes(filter);
  }
}


int JSObject::NumberOfEnumProperties() {
  return NumberOfLocalProperties(static_cast<PropertyAttributes>(DONT_ENUM));
}


void FixedArray::Swap(int i, int j) {
  Object* temp = get(i);
  set(i, get(j));
  set(j, temp);
}


static void InsertionSortPairs(FixedArray* content, FixedArray* smis) {
  int len = smis->length();
  for (int i = 1; i < len; i++) {
    int j = i;
    while (j > 0 &&
           Smi::cast(smis->get(j-1))->value() >
               Smi::cast(smis->get(j))->value()) {
      smis->Swap(j-1, j);
      content->Swap(j-1, j);
      j--;
    }
  }
}


void HeapSortPairs(FixedArray* content, FixedArray* smis) {
  // In-place heap sort.
  ASSERT(content->length() == smis->length());
  int len = smis->length();

  // Bottom-up max-heap construction.
  for (int i = 1; i < len; ++i) {
    int child_index = i;
    while (child_index > 0) {
      int parent_index = ((child_index + 1) >> 1) - 1;
      int parent_value = Smi::cast(smis->get(parent_index))->value();
      int child_value = Smi::cast(smis->get(child_index))->value();
      if (parent_value < child_value) {
        content->Swap(parent_index, child_index);
        smis->Swap(parent_index, child_index);
      } else {
        break;
      }
      child_index = parent_index;
    }
  }

  // Extract elements and create sorted array.
  for (int i = len - 1; i > 0; --i) {
    // Put max element at the back of the array.
    content->Swap(0, i);
    smis->Swap(0, i);
    // Sift down the new top element.
    int parent_index = 0;
    while (true) {
      int child_index = ((parent_index + 1) << 1) - 1;
      if (child_index >= i) break;
      uint32_t child1_value = Smi::cast(smis->get(child_index))->value();
      uint32_t child2_value = Smi::cast(smis->get(child_index + 1))->value();
      uint32_t parent_value = Smi::cast(smis->get(parent_index))->value();
      if (child_index + 1 >= i || child1_value > child2_value) {
        if (parent_value > child1_value) break;
        content->Swap(parent_index, child_index);
        smis->Swap(parent_index, child_index);
        parent_index = child_index;
      } else {
        if (parent_value > child2_value) break;
        content->Swap(parent_index, child_index + 1);
        smis->Swap(parent_index, child_index + 1);
        parent_index = child_index + 1;
      }
    }
  }
}


// Sort this array and the smis as pairs wrt. the (distinct) smis.
void FixedArray::SortPairs(FixedArray* smis) {
  ASSERT(this->length() == smis->length());
  int len = smis->length();
  // For small arrays, simply use insertion sort.
  if (len <= 10) {
    InsertionSortPairs(this, smis);
    return;
  }
  // Check the range of indices.
  int min_index = Smi::cast(smis->get(0))->value();
  int max_index = min_index;
  int i;
  for (i = 1; i < len; i++) {
    if (Smi::cast(smis->get(i))->value() < min_index) {
      min_index = Smi::cast(smis->get(i))->value();
    } else if (Smi::cast(smis->get(i))->value() > max_index) {
      max_index = Smi::cast(smis->get(i))->value();
    }
  }
  if (max_index - min_index + 1 == len) {
    // Indices form a contiguous range, unless there are duplicates.
    // Do an in-place linear time sort assuming distinct smis, but
    // avoid hanging in case they are not.
    for (i = 0; i < len; i++) {
      int p;
      int j = 0;
      // While the current element at i is not at its correct position p,
      // swap the elements at these two positions.
      while ((p = Smi::cast(smis->get(i))->value() - min_index) != i &&
             j++ < len) {
        this->Swap(i, p);
        smis->Swap(i, p);
      }
    }
  } else {
    HeapSortPairs(this, smis);
    return;
  }
}


// Fill in the names of local properties into the supplied storage. The main
// purpose of this function is to provide reflection information for the object
// mirrors.
void JSObject::GetLocalPropertyNames(FixedArray* storage) {
  ASSERT(storage->length() ==
         NumberOfLocalProperties(static_cast<PropertyAttributes>(NONE)));
  int index = 0;
  if (HasFastProperties()) {
    for (DescriptorReader r(map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      if (!r.IsTransition()) {
        storage->set(index++, r.GetKey());
      }
    }
    ASSERT(storage->length() == index);
  } else {
    property_dictionary()->CopyKeysTo(storage);
  }
}


int JSObject::NumberOfLocalElements(PropertyAttributes filter) {
  return GetLocalElementKeys(NULL, filter);
}


int JSObject::NumberOfEnumElements() {
  return NumberOfLocalElements(static_cast<PropertyAttributes>(DONT_ENUM));
}


int JSObject::GetLocalElementKeys(FixedArray* storage,
                                  PropertyAttributes filter) {
  int counter = 0;
  if (HasFastElements()) {
    int length = IsJSArray()
        ? Smi::cast(JSArray::cast(this)->length())->value()
        : FixedArray::cast(elements())->length();
    for (int i = 0; i < length; i++) {
      if (!FixedArray::cast(elements())->get(i)->IsTheHole()) {
        if (storage) {
          storage->set(counter,
                       Smi::FromInt(i),
                       FixedArray::SKIP_WRITE_BARRIER);
        }
        counter++;
      }
    }
    ASSERT(!storage || storage->length() >= counter);
  } else {
    if (storage)
      element_dictionary()->CopyKeysTo(storage, filter);
    counter = element_dictionary()->NumberOfElementsFilterAttributes(filter);
  }

  if (this->IsJSValue()) {
    Object* val = JSValue::cast(this)->value();
    if (val->IsString()) {
      String* str = String::cast(val);
      if (storage) {
        for (int i = 0; i < str->length(); i++) {
          storage->set(counter + i,
                       Smi::FromInt(i),
                       FixedArray::SKIP_WRITE_BARRIER);
        }
      }
      counter += str->length();
    }
  }
  ASSERT(!storage || storage->length() == counter);
  return counter;
}


int JSObject::GetEnumElementKeys(FixedArray* storage) {
  return GetLocalElementKeys(storage,
                             static_cast<PropertyAttributes>(DONT_ENUM));
}


// The NumberKey uses carries the uint32_t as key.
// This avoids allocation in HasProperty.
class Dictionary::NumberKey : public Dictionary::Key {
 public:
  explicit NumberKey(uint32_t number) {
    number_ = number;
  }

 private:
  bool IsMatch(Object* other) {
    return number_ == ToUint32(other);
  }

  // Thomas Wang, Integer Hash Functions.
  // http://www.concentric.net/~Ttwang/tech/inthash.htm
  static uint32_t ComputeHash(uint32_t key) {
    uint32_t hash = key;
    hash = ~hash + (hash << 15);  // hash = (hash << 15) - hash - 1;
    hash = hash ^ (hash >> 12);
    hash = hash + (hash << 2);
    hash = hash ^ (hash >> 4);
    hash = hash * 2057;  // hash = (hash + (hash << 3)) + (hash << 11);
    hash = hash ^ (hash >> 16);
    return hash;
  }

  uint32_t Hash() { return ComputeHash(number_); }

  HashFunction GetHashFunction() { return NumberHash; }

  Object* GetObject() {
    return Heap::NumberFromDouble(number_);
  }

  static uint32_t NumberHash(Object* obj) {
    return ComputeHash(ToUint32(obj));
  }

  static uint32_t ToUint32(Object* obj) {
    ASSERT(obj->IsNumber());
    return static_cast<uint32_t>(obj->Number());
  }

  bool IsStringKey() { return false; }

  uint32_t number_;
};

// StringKey simply carries a string object as key.
class Dictionary::StringKey : public Dictionary::Key {
 public:
  explicit StringKey(String* string) {
    string_ = string;
  }

 private:
  bool IsMatch(Object* other) {
    if (!other->IsString()) return false;
    return string_->Equals(String::cast(other));
  }

  uint32_t Hash() { return StringHash(string_); }

  HashFunction GetHashFunction() { return StringHash; }

  Object* GetObject() { return string_; }

  static uint32_t StringHash(Object* obj) {
    return String::cast(obj)->Hash();
  }

  bool IsStringKey() { return true; }

  String* string_;
};

// Utf8Key carries a vector of chars as key.
class SymbolTable::Utf8Key : public SymbolTable::Key {
 public:
  explicit Utf8Key(Vector<const char> string)
      : string_(string), hash_(0) { }

  bool IsMatch(Object* other) {
    if (!other->IsString()) return false;
    return String::cast(other)->IsEqualTo(string_);
  }

  HashFunction GetHashFunction() {
    return StringHash;
  }

  uint32_t Hash() {
    if (hash_ != 0) return hash_;
    unibrow::Utf8InputBuffer<> buffer(string_.start(),
                                      static_cast<unsigned>(string_.length()));
    chars_ = buffer.Length();
    hash_ = String::ComputeHashCode(&buffer, chars_);
    return hash_;
  }

  Object* GetObject() {
    if (hash_ == 0) Hash();
    unibrow::Utf8InputBuffer<> buffer(string_.start(),
                                      static_cast<unsigned>(string_.length()));
    return Heap::AllocateSymbol(&buffer, chars_, hash_);
  }

  static uint32_t StringHash(Object* obj) {
    return String::cast(obj)->Hash();
  }

  bool IsStringKey() { return true; }

  Vector<const char> string_;
  uint32_t hash_;
  int chars_;  // Caches the number of characters when computing the hash code.
};


// StringKey carries a string object as key.
class SymbolTable::StringKey : public SymbolTable::Key {
 public:
  explicit StringKey(String* string) : string_(string) { }

  HashFunction GetHashFunction() {
    return StringHash;
  }

  bool IsMatch(Object* other) {
    if (!other->IsString()) return false;
    return String::cast(other)->Equals(string_);
  }

  uint32_t Hash() { return string_->Hash(); }

  Object* GetObject() {
    // Transform string to symbol if possible.
    Map* map = Heap::SymbolMapForString(string_);
    if (map != NULL) {
      string_->set_map(map);
      return string_;
    }
    // Otherwise allocate a new symbol.
    StringInputBuffer buffer(string_);
    return Heap::AllocateSymbol(&buffer, string_->length(), string_->Hash());
  }

  static uint32_t StringHash(Object* obj) {
    return String::cast(obj)->Hash();
  }

  bool IsStringKey() { return true; }

  String* string_;
};


template<int prefix_size, int element_size>
void HashTable<prefix_size, element_size>::IteratePrefix(ObjectVisitor* v) {
  IteratePointers(v, 0, kElementsStartOffset);
}


template<int prefix_size, int element_size>
void HashTable<prefix_size, element_size>::IterateElements(ObjectVisitor* v) {
  IteratePointers(v,
                  kElementsStartOffset,
                  kHeaderSize + length() * kPointerSize);
}


template<int prefix_size, int element_size>
Object* HashTable<prefix_size, element_size>::Allocate(int at_least_space_for) {
  int capacity = NextPowerOf2(at_least_space_for);
  if (capacity < 4) capacity = 4;  // Guarantee min capacity.
  Object* obj = Heap::AllocateHashTable(EntryToIndex(capacity));
  if (!obj->IsFailure()) {
    HashTable::cast(obj)->SetNumberOfElements(0);
    HashTable::cast(obj)->SetCapacity(capacity);
  }
  return obj;
}


// Find entry for key otherwise return -1.
template <int prefix_size, int element_size>
int HashTable<prefix_size, element_size>::FindEntry(Key* key) {
  uint32_t nof = NumberOfElements();
  if (nof == 0) return -1;  // Bail out if empty.

  uint32_t capacity = Capacity();
  uint32_t hash = key->Hash();
  uint32_t entry = GetProbe(hash, 0, capacity);

  Object* element = KeyAt(entry);
  uint32_t passed_elements = 0;
  if (!element->IsNull()) {
    if (!element->IsUndefined() && key->IsMatch(element)) return entry;
    if (++passed_elements == nof) return -1;
  }
  for (uint32_t i = 1; !element->IsUndefined(); i++) {
    entry = GetProbe(hash, i, capacity);
    element = KeyAt(entry);
    if (!element->IsNull()) {
      if (!element->IsUndefined() && key->IsMatch(element)) return entry;
      if (++passed_elements == nof) return -1;
    }
  }
  return -1;
}


template<int prefix_size, int element_size>
Object* HashTable<prefix_size, element_size>::EnsureCapacity(int n, Key* key) {
  int capacity = Capacity();
  int nof = NumberOfElements() + n;
  // Make sure 20% is free
  if (nof + (nof >> 2) <= capacity) return this;

  Object* obj = Allocate(nof * 2);
  if (obj->IsFailure()) return obj;
  HashTable* dict = HashTable::cast(obj);
  WriteBarrierMode mode = dict->GetWriteBarrierMode();

  // Copy prefix to new array.
  for (int i = kPrefixStartIndex; i < kPrefixStartIndex + prefix_size; i++) {
    dict->set(i, get(i), mode);
  }
  // Rehash the elements.
  uint32_t (*Hash)(Object* key) = key->GetHashFunction();
  for (int i = 0; i < capacity; i++) {
    uint32_t from_index = EntryToIndex(i);
    Object* key = get(from_index);
    if (IsKey(key)) {
      uint32_t insertion_index =
          EntryToIndex(dict->FindInsertionEntry(key, Hash(key)));
      for (int j = 0; j < element_size; j++) {
        dict->set(insertion_index + j, get(from_index + j), mode);
      }
    }
  }
  dict->SetNumberOfElements(NumberOfElements());
  return dict;
}


template<int prefix_size, int element_size>
uint32_t HashTable<prefix_size, element_size>::FindInsertionEntry(
      Object* key,
      uint32_t hash) {
  uint32_t capacity = Capacity();
  uint32_t entry = GetProbe(hash, 0, capacity);
  Object* element = KeyAt(entry);

  for (uint32_t i = 1; !(element->IsUndefined() || element->IsNull()); i++) {
    entry = GetProbe(hash, i, capacity);
    element = KeyAt(entry);
  }

  return entry;
}


// Force instantiation of SymbolTable's base class
template class HashTable<0, 1>;


// Force instantiation of Dictionary's base class
template class HashTable<2, 3>;


Object* SymbolTable::LookupString(String* string, Object** s) {
  StringKey key(string);
  return LookupKey(&key, s);
}


Object* SymbolTable::LookupSymbol(Vector<const char> str, Object** s) {
  Utf8Key key(str);
  return LookupKey(&key, s);
}


Object* SymbolTable::LookupKey(Key* key, Object** s) {
  int entry = FindEntry(key);

  // Symbol already in table.
  if (entry != -1) {
    *s = KeyAt(entry);
    return this;
  }

  // Adding new symbol. Grow table if needed.
  Object* obj = EnsureCapacity(1, key);
  if (obj->IsFailure()) return obj;

  // Create symbol object.
  Object* symbol = key->GetObject();
  if (symbol->IsFailure()) return symbol;

  // If the symbol table grew as part of EnsureCapacity, obj is not
  // the current symbol table and therefore we cannot use
  // SymbolTable::cast here.
  SymbolTable* table = reinterpret_cast<SymbolTable*>(obj);

  // Add the new symbol and return it along with the symbol table.
  entry = table->FindInsertionEntry(symbol, key->Hash());
  table->set(EntryToIndex(entry), symbol);
  table->ElementAdded();
  *s = symbol;
  return table;
}


Object* Dictionary::Allocate(int at_least_space_for) {
  Object* obj = DictionaryBase::Allocate(at_least_space_for);
  // Initialize the next enumeration index.
  if (!obj->IsFailure()) {
    Dictionary::cast(obj)->
        SetNextEnumerationIndex(PropertyDetails::kInitialIndex);
  }
  return obj;
}

Object* Dictionary::GenerateNewEnumerationIndices() {
  int length = NumberOfElements();

  // Allocate and initialize iteration order array.
  Object* obj = Heap::AllocateFixedArray(length);
  if (obj->IsFailure()) return obj;
  FixedArray* iteration_order = FixedArray::cast(obj);
  for (int i = 0; i < length; i++) iteration_order->set(i, Smi::FromInt(i));

  // Allocate array with enumeration order.
  obj = Heap::AllocateFixedArray(length);
  if (obj->IsFailure()) return obj;
  FixedArray* enumeration_order = FixedArray::cast(obj);

  // Fill the enumeration order array with property details.
  int capacity = Capacity();
  int pos = 0;
  for (int i = 0; i < capacity; i++) {
    if (IsKey(KeyAt(i))) {
      enumeration_order->set(pos++, Smi::FromInt(DetailsAt(i).index()));
    }
  }

  // Sort the arrays wrt. enumeration order.
  iteration_order->SortPairs(enumeration_order);

  // Overwrite the enumeration_order with the enumeration indices.
  for (int i = 0; i < length; i++) {
    int index = Smi::cast(iteration_order->get(i))->value();
    int enum_index = PropertyDetails::kInitialIndex + i;
    enumeration_order->set(index, Smi::FromInt(enum_index));
  }

  // Update the dictionary with new indices.
  capacity = Capacity();
  pos = 0;
  for (int i = 0; i < capacity; i++) {
    if (IsKey(KeyAt(i))) {
      int enum_index = Smi::cast(enumeration_order->get(pos++))->value();
      PropertyDetails details = DetailsAt(i);
      PropertyDetails new_details =
          PropertyDetails(details.attributes(), details.type(), enum_index);
      DetailsAtPut(i, new_details);
    }
  }

  // Set the next enumeration index.
  SetNextEnumerationIndex(PropertyDetails::kInitialIndex+length);
  return this;
}


Object* Dictionary::EnsureCapacity(int n, Key* key) {
  // Check whether there is enough enumeration indices for adding n elements.
  if (key->IsStringKey() &&
      !PropertyDetails::IsValidIndex(NextEnumerationIndex() + n)) {
    // If not, we generate new indices for the properties.
    Object* result = GenerateNewEnumerationIndices();
    if (result->IsFailure()) return result;
  }
  return DictionaryBase::EnsureCapacity(n, key);
}


void Dictionary::RemoveNumberEntries(uint32_t from, uint32_t to) {
  // Do nothing if the interval [from, to) is empty.
  if (from >= to) return;

  int removed_entries = 0;
  Object* sentinel = Heap::null_value();
  int capacity = Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* key = KeyAt(i);
    if (key->IsNumber()) {
      uint32_t number = static_cast<uint32_t>(key->Number());
      if (from <= number && number < to) {
        SetEntry(i, sentinel, sentinel, Smi::FromInt(0));
        removed_entries++;
      }
    }
  }

  // Update the number of elements.
  SetNumberOfElements(NumberOfElements() - removed_entries);
}


Object* Dictionary::DeleteProperty(int entry) {
  PropertyDetails details = DetailsAt(entry);
  if (details.IsDontDelete()) return Heap::false_value();
  SetEntry(entry, Heap::null_value(), Heap::null_value(), Smi::FromInt(0));
  ElementRemoved();
  return Heap::true_value();
}


int Dictionary::FindStringEntry(String* key) {
  StringKey k(key);
  return FindEntry(&k);
}


int Dictionary::FindNumberEntry(uint32_t index) {
  NumberKey k(index);
  return FindEntry(&k);
}


Object* Dictionary::AtPut(Key* key, Object* value) {
  int entry = FindEntry(key);

  // If the entry is present set the value;
  if (entry != -1) {
    ValueAtPut(entry, value);
    return this;
  }

  // Check whether the dictionary should be extended.
  Object* obj = EnsureCapacity(1, key);
  if (obj->IsFailure()) return obj;
  Object* k = key->GetObject();
  if (k->IsFailure()) return k;
  PropertyDetails details = PropertyDetails(NONE, NORMAL);
  Dictionary::cast(obj)->AddEntry(k, value, details, key->Hash());
  return obj;
}


Object* Dictionary::Add(Key* key, Object* value, PropertyDetails details) {
  // Check whether the dictionary should be extended.
  Object* obj = EnsureCapacity(1, key);
  if (obj->IsFailure()) return obj;
  // Compute the key object.
  Object* k = key->GetObject();
  if (k->IsFailure()) return k;
  Dictionary::cast(obj)->AddEntry(k, value, details, key->Hash());
  return obj;
}


// Add a key, value pair to the dictionary.
void Dictionary::AddEntry(Object* key,
                          Object* value,
                          PropertyDetails details,
                          uint32_t hash) {
  uint32_t entry = FindInsertionEntry(key, hash);
  // Insert element at empty or deleted entry
  if (details.index() == 0 && key->IsString()) {
    // Assign an enumeration index to the property and update
    // SetNextEnumerationIndex.
    int index = NextEnumerationIndex();
    details = PropertyDetails(details.attributes(), details.type(), index);
    SetNextEnumerationIndex(index + 1);
  }
  SetEntry(entry, key, value, details);
  ASSERT(KeyAt(entry)->IsNumber() || KeyAt(entry)->IsString());
  ElementAdded();
}


void Dictionary::UpdateMaxNumberKey(uint32_t key) {
  // If the dictionary requires slow elements an element has already
  // been added at a high index.
  if (requires_slow_elements()) return;
  // Check if this index is high enough that we should require slow
  // elements.
  if (key > kRequiresSlowElementsLimit) {
    set(kPrefixStartIndex, Smi::FromInt(kRequiresSlowElementsMask));
    return;
  }
  // Update max key value.
  Object* max_index_object = get(kPrefixStartIndex);
  if (!max_index_object->IsSmi() || max_number_key() < key) {
    set(kPrefixStartIndex, Smi::FromInt(key << kRequiresSlowElementsTagSize));
  }
}


Object* Dictionary::AddStringEntry(String* key,
                                   Object* value,
                                   PropertyDetails details) {
  StringKey k(key);
  SLOW_ASSERT(FindEntry(&k) == -1);
  return Add(&k, value, details);
}


Object* Dictionary::AddNumberEntry(uint32_t key,
                                   Object* value,
                                   PropertyDetails details) {
  NumberKey k(key);
  UpdateMaxNumberKey(key);
  SLOW_ASSERT(FindEntry(&k) == -1);
  return Add(&k, value, details);
}


Object* Dictionary::AtStringPut(String* key, Object* value) {
  StringKey k(key);
  return AtPut(&k, value);
}


Object* Dictionary::AtNumberPut(uint32_t key, Object* value) {
  NumberKey k(key);
  UpdateMaxNumberKey(key);
  return AtPut(&k, value);
}


Object* Dictionary::SetOrAddStringEntry(String* key,
                                        Object* value,
                                        PropertyDetails details) {
  StringKey k(key);
  int entry = FindEntry(&k);
  if (entry == -1) return AddStringEntry(key, value, details);
  // Preserve enumeration index.
  details = PropertyDetails(details.attributes(),
                            details.type(),
                            DetailsAt(entry).index());
  SetEntry(entry, key, value, details);
  return this;
}


int Dictionary::NumberOfElementsFilterAttributes(PropertyAttributes filter) {
  int capacity = Capacity();
  int result = 0;
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      PropertyAttributes attr = DetailsAt(i).attributes();
      if ((attr & filter) == 0) result++;
    }
  }
  return result;
}


int Dictionary::NumberOfEnumElements() {
  return NumberOfElementsFilterAttributes(
      static_cast<PropertyAttributes>(DONT_ENUM));
}


void Dictionary::CopyKeysTo(FixedArray* storage, PropertyAttributes filter) {
  ASSERT(storage->length() >= NumberOfEnumElements());
  int capacity = Capacity();
  int index = 0;
  for (int i = 0; i < capacity; i++) {
     Object* k = KeyAt(i);
     if (IsKey(k)) {
       PropertyAttributes attr = DetailsAt(i).attributes();
       if ((attr & filter) == 0) storage->set(index++, k);
     }
  }
  ASSERT(storage->length() >= index);
}


void Dictionary::CopyEnumKeysTo(FixedArray* storage, FixedArray* sort_array) {
  ASSERT(storage->length() >= NumberOfEnumElements());
  int capacity = Capacity();
  int index = 0;
  for (int i = 0; i < capacity; i++) {
     Object* k = KeyAt(i);
     if (IsKey(k)) {
       PropertyDetails details = DetailsAt(i);
       if (!details.IsDontEnum()) {
         storage->set(index, k);
         sort_array->set(index, Smi::FromInt(details.index()));
         index++;
       }
     }
  }
  storage->SortPairs(sort_array);
  ASSERT(storage->length() >= index);
}


void Dictionary::CopyKeysTo(FixedArray* storage) {
  ASSERT(storage->length() >= NumberOfElementsFilterAttributes(
      static_cast<PropertyAttributes>(NONE)));
  int capacity = Capacity();
  int index = 0;
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      storage->set(index++, k);
    }
  }
  ASSERT(storage->length() >= index);
}


// Backwards lookup (slow).
Object* Dictionary::SlowReverseLookup(Object* value) {
  int capacity = Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k) && ValueAt(i) == value) {
      return k;
    }
  }
  return Heap::undefined_value();
}


Object* Dictionary::TransformPropertiesToFastFor(JSObject* obj,
                                                 int unused_property_fields) {
  // Make sure we preserve dictionary representation if there are too many
  // descriptors.
  if (NumberOfElements() > DescriptorArray::kMaxNumberOfDescriptors) return obj;

  // Figure out if it is necessary to generate new enumeration indices.
  int max_enumeration_index =
      NextEnumerationIndex() +
          (DescriptorArray::kMaxNumberOfDescriptors - NumberOfElements());
  if (!PropertyDetails::IsValidIndex(max_enumeration_index)) {
    Object* result = GenerateNewEnumerationIndices();
    if (result->IsFailure()) return result;
  }

  int instance_descriptor_length = 0;
  int number_of_fields = 0;

  // Compute the length of the instance descriptor.
  int capacity = Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      Object* value = ValueAt(i);
      PropertyType type = DetailsAt(i).type();
      ASSERT(type != FIELD);
      instance_descriptor_length++;
      if (type == NORMAL && !value->IsJSFunction()) number_of_fields += 1;
    }
  }

  // Allocate the instance descriptor.
  Object* instance_descriptors =
      DescriptorArray::Allocate(instance_descriptor_length);
  if (instance_descriptors->IsFailure()) return instance_descriptors;

  int number_of_allocated_fields = number_of_fields + unused_property_fields;

  // Allocate the fixed array for the fields.
  Object* fields = Heap::AllocateFixedArray(number_of_allocated_fields);
  if (fields->IsFailure()) return fields;

  // Fill in the instance descriptor and the fields.
  DescriptorWriter w(DescriptorArray::cast(instance_descriptors));
  int current_offset = 0;
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      Object* value = ValueAt(i);
      // Ensure the key is a symbol before writing into the instance descriptor.
      Object* key = Heap::LookupSymbol(String::cast(k));
      if (key->IsFailure()) return key;
      PropertyDetails details = DetailsAt(i);
      PropertyType type = details.type();
      if (value->IsJSFunction()) {
        ConstantFunctionDescriptor d(String::cast(key),
                                     JSFunction::cast(value),
                                     details.attributes(),
                                     details.index());
        w.Write(&d);
      } else if (type == NORMAL) {
        FixedArray::cast(fields)->set(current_offset, value);
        FieldDescriptor d(String::cast(key),
                          current_offset++,
                          details.attributes(),
                          details.index());
        w.Write(&d);
      } else if (type == CALLBACKS) {
        CallbacksDescriptor d(String::cast(key),
                              value,
                              details.attributes(),
                              details.index());
        w.Write(&d);
      } else {
        UNREACHABLE();
      }
    }
  }
  ASSERT(current_offset == number_of_fields);

  // Sort the instance descriptors.
  DescriptorArray::cast(instance_descriptors)->Sort();

  // Allocate new map.
  Object* new_map = obj->map()->Copy();
  if (new_map->IsFailure()) return new_map;

  // Transform the object.
  Map::cast(new_map)->
      set_instance_descriptors(DescriptorArray::cast(instance_descriptors));
  Map::cast(new_map)->set_unused_property_fields(unused_property_fields);
  obj->set_map(Map::cast(new_map));
  obj->set_properties(FixedArray::cast(fields));
  ASSERT(obj->IsJSObject());

  // Transfer next enumeration index from dictionary to instance descriptors.
  DescriptorArray::cast(instance_descriptors)->
      SetNextEnumerationIndex(NextEnumerationIndex());

  // Check it really works.
  ASSERT(obj->HasFastProperties());
  return obj;
}


// Check if there is a break point at this code position.
bool DebugInfo::HasBreakPoint(int code_position) {
  // Get the break point info object for this code position.
  Object* break_point_info = GetBreakPointInfo(code_position);

  // If there is no break point info object or no break points in the break
  // point info object there is no break point at this code position.
  if (break_point_info->IsUndefined()) return false;
  return BreakPointInfo::cast(break_point_info)->GetBreakPointCount() > 0;
}


// Get the break point info object for this code position.
Object* DebugInfo::GetBreakPointInfo(int code_position) {
  // Find the index of the break point info object for this code position.
  int index = GetBreakPointInfoIndex(code_position);

  // Return the break point info object if any.
  if (index == kNoBreakPointInfo) return Heap::undefined_value();
  return BreakPointInfo::cast(break_points()->get(index));
}


// Clear a break point at the specified code position.
void DebugInfo::ClearBreakPoint(Handle<DebugInfo> debug_info,
                                int code_position,
                                Handle<Object> break_point_object) {
  Handle<Object> break_point_info(debug_info->GetBreakPointInfo(code_position));
  if (break_point_info->IsUndefined()) return;
  BreakPointInfo::ClearBreakPoint(
      Handle<BreakPointInfo>::cast(break_point_info),
      break_point_object);
}


void DebugInfo::SetBreakPoint(Handle<DebugInfo> debug_info,
                              int code_position,
                              int source_position,
                              int statement_position,
                              Handle<Object> break_point_object) {
  Handle<Object> break_point_info(debug_info->GetBreakPointInfo(code_position));
  if (!break_point_info->IsUndefined()) {
    BreakPointInfo::SetBreakPoint(
        Handle<BreakPointInfo>::cast(break_point_info),
        break_point_object);
    return;
  }

  // Adding a new break point for a code position which did not have any
  // break points before. Try to find a free slot.
  int index = kNoBreakPointInfo;
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (debug_info->break_points()->get(i)->IsUndefined()) {
      index = i;
      break;
    }
  }
  if (index == kNoBreakPointInfo) {
    // No free slot - extend break point info array.
    Handle<FixedArray> old_break_points =
        Handle<FixedArray>(FixedArray::cast(debug_info->break_points()));
    debug_info->set_break_points(*Factory::NewFixedArray(
        old_break_points->length() +
            Debug::kEstimatedNofBreakPointsInFunction));
    Handle<FixedArray> new_break_points =
        Handle<FixedArray>(FixedArray::cast(debug_info->break_points()));
    for (int i = 0; i < old_break_points->length(); i++) {
      new_break_points->set(i, old_break_points->get(i));
    }
    index = old_break_points->length();
  }
  ASSERT(index != kNoBreakPointInfo);

  // Allocate new BreakPointInfo object and set the break point.
  Handle<BreakPointInfo> new_break_point_info =
      Handle<BreakPointInfo>::cast(Factory::NewStruct(BREAK_POINT_INFO_TYPE));
  new_break_point_info->set_code_position(Smi::FromInt(code_position));
  new_break_point_info->set_source_position(Smi::FromInt(source_position));
  new_break_point_info->
      set_statement_position(Smi::FromInt(statement_position));
  new_break_point_info->set_break_point_objects(Heap::undefined_value());
  BreakPointInfo::SetBreakPoint(new_break_point_info, break_point_object);
  debug_info->break_points()->set(index, *new_break_point_info);
}


// Get the break point objects for a code position.
Object* DebugInfo::GetBreakPointObjects(int code_position) {
  Object* break_point_info = GetBreakPointInfo(code_position);
  if (break_point_info->IsUndefined()) {
    return Heap::undefined_value();
  }
  return BreakPointInfo::cast(break_point_info)->break_point_objects();
}


// Get the total number of break points.
int DebugInfo::GetBreakPointCount() {
  if (break_points()->IsUndefined()) return 0;
  int count = 0;
  for (int i = 0; i < break_points()->length(); i++) {
    if (!break_points()->get(i)->IsUndefined()) {
      BreakPointInfo* break_point_info =
          BreakPointInfo::cast(break_points()->get(i));
      count += break_point_info->GetBreakPointCount();
    }
  }
  return count;
}


Object* DebugInfo::FindBreakPointInfo(Handle<DebugInfo> debug_info,
                                      Handle<Object> break_point_object) {
  if (debug_info->break_points()->IsUndefined()) return Heap::undefined_value();
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (!debug_info->break_points()->get(i)->IsUndefined()) {
      Handle<BreakPointInfo> break_point_info =
          Handle<BreakPointInfo>(BreakPointInfo::cast(
              debug_info->break_points()->get(i)));
      if (BreakPointInfo::HasBreakPointObject(break_point_info,
                                              break_point_object)) {
        return *break_point_info;
      }
    }
  }
  return Heap::undefined_value();
}


// Find the index of the break point info object for the specified code
// position.
int DebugInfo::GetBreakPointInfoIndex(int code_position) {
  if (break_points()->IsUndefined()) return kNoBreakPointInfo;
  for (int i = 0; i < break_points()->length(); i++) {
    if (!break_points()->get(i)->IsUndefined()) {
      BreakPointInfo* break_point_info =
          BreakPointInfo::cast(break_points()->get(i));
      if (break_point_info->code_position()->value() == code_position) {
        return i;
      }
    }
  }
  return kNoBreakPointInfo;
}


// Remove the specified break point object.
void BreakPointInfo::ClearBreakPoint(Handle<BreakPointInfo> break_point_info,
                                     Handle<Object> break_point_object) {
  // If there are no break points just ignore.
  if (break_point_info->break_point_objects()->IsUndefined()) return;
  // If there is a single break point clear it if it is the same.
  if (!break_point_info->break_point_objects()->IsFixedArray()) {
    if (break_point_info->break_point_objects() == *break_point_object) {
      break_point_info->set_break_point_objects(Heap::undefined_value());
    }
    return;
  }
  // If there are multiple break points shrink the array
  ASSERT(break_point_info->break_point_objects()->IsFixedArray());
  Handle<FixedArray> old_array =
      Handle<FixedArray>(
          FixedArray::cast(break_point_info->break_point_objects()));
  Handle<FixedArray> new_array =
      Factory::NewFixedArray(old_array->length() - 1);
  int found_count = 0;
  for (int i = 0; i < old_array->length(); i++) {
    if (old_array->get(i) == *break_point_object) {
      ASSERT(found_count == 0);
      found_count++;
    } else {
      new_array->set(i - found_count, old_array->get(i));
    }
  }
  // If the break point was found in the list change it.
  if (found_count > 0) break_point_info->set_break_point_objects(*new_array);
}


// Add the specified break point object.
void BreakPointInfo::SetBreakPoint(Handle<BreakPointInfo> break_point_info,
                                   Handle<Object> break_point_object) {
  // If there was no break point objects before just set it.
  if (break_point_info->break_point_objects()->IsUndefined()) {
    break_point_info->set_break_point_objects(*break_point_object);
    return;
  }
  // If the break point object is the same as before just ignore.
  if (break_point_info->break_point_objects() == *break_point_object) return;
  // If there was one break point object before replace with array.
  if (!break_point_info->break_point_objects()->IsFixedArray()) {
    Handle<FixedArray> array = Factory::NewFixedArray(2);
    array->set(0, break_point_info->break_point_objects());
    array->set(1, *break_point_object);
    break_point_info->set_break_point_objects(*array);
    return;
  }
  // If there was more than one break point before extend array.
  Handle<FixedArray> old_array =
      Handle<FixedArray>(
          FixedArray::cast(break_point_info->break_point_objects()));
  Handle<FixedArray> new_array =
      Factory::NewFixedArray(old_array->length() + 1);
  for (int i = 0; i < old_array->length(); i++) {
    // If the break point was there before just ignore.
    if (old_array->get(i) == *break_point_object) return;
    new_array->set(i, old_array->get(i));
  }
  // Add the new break point.
  new_array->set(old_array->length(), *break_point_object);
  break_point_info->set_break_point_objects(*new_array);
}


bool BreakPointInfo::HasBreakPointObject(
    Handle<BreakPointInfo> break_point_info,
    Handle<Object> break_point_object) {
  // No break point.
  if (break_point_info->break_point_objects()->IsUndefined()) return false;
  // Single beak point.
  if (!break_point_info->break_point_objects()->IsFixedArray()) {
    return break_point_info->break_point_objects() == *break_point_object;
  }
  // Multiple break points.
  FixedArray* array = FixedArray::cast(break_point_info->break_point_objects());
  for (int i = 0; i < array->length(); i++) {
    if (array->get(i) == *break_point_object) {
      return true;
    }
  }
  return false;
}


// Get the number of break points.
int BreakPointInfo::GetBreakPointCount() {
  // No break point.
  if (break_point_objects()->IsUndefined()) return 0;
  // Single beak point.
  if (!break_point_objects()->IsFixedArray()) return 1;
  // Multiple break points.
  return FixedArray::cast(break_point_objects())->length();
}


} }  // namespace v8::internal
