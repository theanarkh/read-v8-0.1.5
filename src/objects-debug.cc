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

#include "disassembler.h"
#include "disasm.h"
#include "macro-assembler.h"

namespace v8 { namespace internal {

#ifdef DEBUG

static const char* TypeToString(InstanceType type);


void Object::Print() {
  if (IsSmi()) {
    Smi::cast(this)->SmiPrint();
  } else if (IsFailure()) {
    Failure::cast(this)->FailurePrint();
  } else {
    HeapObject::cast(this)->HeapObjectPrint();
  }
  Flush();
}


void Object::PrintLn() {
  Print();
  PrintF("\n");
}


void Object::Verify() {
  if (IsSmi()) {
    Smi::cast(this)->SmiVerify();
  } else if (IsFailure()) {
    Failure::cast(this)->FailureVerify();
  } else {
    HeapObject::cast(this)->HeapObjectVerify();
  }
}


void Object::VerifyPointer(Object* p) {
  if (p->IsHeapObject()) {
    HeapObject::VerifyHeapPointer(p);
  } else {
    ASSERT(p->IsSmi());
  }
}


void Smi::SmiVerify() {
  ASSERT(IsSmi());
}


void Failure::FailureVerify() {
  ASSERT(IsFailure());
}


void HeapObject::PrintHeader(const char* id) {
  PrintF("%p: [%s]\n", this, id);
}


void HeapObject::HeapObjectPrint() {
  InstanceType instance_type = map()->instance_type();

  HandleScope scope;
  if (instance_type < FIRST_NONSTRING_TYPE) {
    String::cast(this)->StringPrint();
    return;
  }

  switch (instance_type) {
    case MAP_TYPE:
      Map::cast(this)->MapPrint();
      break;
    case HEAP_NUMBER_TYPE:
      HeapNumber::cast(this)->HeapNumberPrint();
      break;
    case FIXED_ARRAY_TYPE:
      FixedArray::cast(this)->FixedArrayPrint();
      break;
    case BYTE_ARRAY_TYPE:
      ByteArray::cast(this)->ByteArrayPrint();
      break;
    case FILLER_TYPE:
      PrintF("filler");
      break;
    case JS_OBJECT_TYPE:  // fall through
    case JS_ARRAY_TYPE:
      JSObject::cast(this)->JSObjectPrint();
      break;
    case ODDBALL_TYPE:
      Oddball::cast(this)->to_string()->Print();
      break;
    case JS_FUNCTION_TYPE:
      JSFunction::cast(this)->JSFunctionPrint();
      break;
    case JS_GLOBAL_OBJECT_TYPE:
      JSGlobalObject::cast(this)->JSGlobalObjectPrint();
      break;
    case JS_BUILTINS_OBJECT_TYPE:
      JSBuiltinsObject::cast(this)->JSBuiltinsObjectPrint();
      break;
    case JS_VALUE_TYPE:
      JSValue::cast(this)->value()->Print();
      break;
    case CODE_TYPE:
      Code::cast(this)->CodePrint();
      break;
    case PROXY_TYPE:
      Proxy::cast(this)->ProxyPrint();
      break;
    case SHARED_FUNCTION_INFO_TYPE:
      SharedFunctionInfo::cast(this)->SharedFunctionInfoPrint();
      break;

#define MAKE_STRUCT_CASE(NAME, Name, name) \
  case NAME##_TYPE:                        \
    Name::cast(this)->Name##Print();       \
    break;
  STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE

    default:
      PrintF("UNKNOWN TYPE %d", map()->instance_type());
      UNREACHABLE();
      break;
  }
}


void HeapObject::HeapObjectVerify() {
  InstanceType instance_type = map()->instance_type();

  if (instance_type < FIRST_NONSTRING_TYPE) {
    String::cast(this)->StringVerify();
    return;
  }

  switch (instance_type) {
    case MAP_TYPE:
      Map::cast(this)->MapVerify();
      break;
    case HEAP_NUMBER_TYPE:
      HeapNumber::cast(this)->HeapNumberVerify();
      break;
    case FIXED_ARRAY_TYPE:
      FixedArray::cast(this)->FixedArrayVerify();
      break;
    case BYTE_ARRAY_TYPE:
      ByteArray::cast(this)->ByteArrayVerify();
      break;
    case CODE_TYPE:
      Code::cast(this)->CodeVerify();
      break;
    case ODDBALL_TYPE:
      Oddball::cast(this)->OddballVerify();
      break;
    case JS_OBJECT_TYPE:
      JSObject::cast(this)->JSObjectVerify();
      break;
    case JS_VALUE_TYPE:
      JSValue::cast(this)->JSValueVerify();
      break;
    case JS_FUNCTION_TYPE:
      JSFunction::cast(this)->JSFunctionVerify();
      break;
    case JS_GLOBAL_OBJECT_TYPE:
      JSGlobalObject::cast(this)->JSGlobalObjectVerify();
      break;
    case JS_BUILTINS_OBJECT_TYPE:
      JSBuiltinsObject::cast(this)->JSBuiltinsObjectVerify();
      break;
    case JS_ARRAY_TYPE:
      JSArray::cast(this)->JSArrayVerify();
      break;
    case FILLER_TYPE:
      break;
    case PROXY_TYPE:
      Proxy::cast(this)->ProxyVerify();
      break;
    case SHARED_FUNCTION_INFO_TYPE:
      SharedFunctionInfo::cast(this)->SharedFunctionInfoVerify();
      break;

#define MAKE_STRUCT_CASE(NAME, Name, name) \
  case NAME##_TYPE:                        \
    Name::cast(this)->Name##Verify();      \
    break;
    STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE

    default:
      UNREACHABLE();
      break;
  }
}


void HeapObject::VerifyHeapPointer(Object* p) {
  ASSERT(p->IsHeapObject());
  ASSERT(Heap::Contains(HeapObject::cast(p)));
}


void HeapNumber::HeapNumberVerify() {
  ASSERT(IsHeapNumber());
}


void ByteArray::ByteArrayPrint() {
  PrintF("byte array, data starts at %p", GetDataStartAddress());
}


void ByteArray::ByteArrayVerify() {
  ASSERT(IsByteArray());
}


void JSObject::PrintProperties() {
  if (HasFastProperties()) {
    for (DescriptorReader r(map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      PrintF("   ");
      r.GetKey()->StringPrint();
      PrintF(": ");
      if (r.type() == FIELD) {
        properties()->get(r.GetFieldIndex())->ShortPrint();
        PrintF(" (field at offset %d)\n", r.GetFieldIndex());
      } else if (r.type() ==  CONSTANT_FUNCTION) {
        r.GetConstantFunction()->ShortPrint();
        PrintF(" (constant function)\n");
      } else if (r.type() == CALLBACKS) {
        r.GetCallbacksObject()->ShortPrint();
        PrintF(" (callback)\n");
      } else if (r.type() == MAP_TRANSITION) {
        PrintF(" (map transition)\n");
      } else {
        UNREACHABLE();
      }
    }
  } else {
    property_dictionary()->Print();
  }
}


void JSObject::PrintElements() {
  if (HasFastElements()) {
    FixedArray* p = FixedArray::cast(elements());
    for (int i = 0; i < p->length(); i++) {
      PrintF("   %d: ", i);
      p->get(i)->ShortPrint();
      PrintF("\n");
    }
  } else {
    elements()->Print();
  }
}


void JSObject::JSObjectPrint() {
  PrintF("%p: [JSObject]\n", this);
  PrintF(" - map = %p\n", map());
  PrintF(" - prototype = %p\n", GetPrototype());
  PrintF(" {\n");
  PrintProperties();
  PrintElements();
  PrintF(" }\n");
}


void JSObject::JSObjectVerify() {
  VerifyHeapPointer(properties());
  VerifyHeapPointer(elements());
  if (HasFastProperties()) {
    CHECK(map()->unused_property_fields() ==
          (properties()->length() - map()->NextFreePropertyIndex()));
  }
}


static const char* TypeToString(InstanceType type) {
  switch (type) {
    case MAP_TYPE: return "MAP";
    case HEAP_NUMBER_TYPE: return "HEAP_NUMBER";
    case SHORT_SYMBOL_TYPE:
    case MEDIUM_SYMBOL_TYPE:
    case LONG_SYMBOL_TYPE: return "SYMBOL";
    case SHORT_ASCII_SYMBOL_TYPE:
    case MEDIUM_ASCII_SYMBOL_TYPE:
    case LONG_ASCII_SYMBOL_TYPE: return "ASCII_SYMBOL";
    case SHORT_SLICED_SYMBOL_TYPE:
    case MEDIUM_SLICED_SYMBOL_TYPE:
    case LONG_SLICED_SYMBOL_TYPE: return "SLICED_SYMBOL";
    case SHORT_SLICED_ASCII_SYMBOL_TYPE:
    case MEDIUM_SLICED_ASCII_SYMBOL_TYPE:
    case LONG_SLICED_ASCII_SYMBOL_TYPE: return "SLICED_ASCII_SYMBOL";
    case SHORT_CONS_SYMBOL_TYPE:
    case MEDIUM_CONS_SYMBOL_TYPE:
    case LONG_CONS_SYMBOL_TYPE: return "CONS_SYMBOL";
    case SHORT_CONS_ASCII_SYMBOL_TYPE:
    case MEDIUM_CONS_ASCII_SYMBOL_TYPE:
    case LONG_CONS_ASCII_SYMBOL_TYPE: return "CONS_ASCII_SYMBOL";
    case SHORT_EXTERNAL_ASCII_SYMBOL_TYPE:
    case MEDIUM_EXTERNAL_ASCII_SYMBOL_TYPE:
    case LONG_EXTERNAL_ASCII_SYMBOL_TYPE:
    case SHORT_EXTERNAL_SYMBOL_TYPE:
    case MEDIUM_EXTERNAL_SYMBOL_TYPE:
    case LONG_EXTERNAL_SYMBOL_TYPE: return "EXTERNAL_SYMBOL";
    case SHORT_ASCII_STRING_TYPE:
    case MEDIUM_ASCII_STRING_TYPE:
    case LONG_ASCII_STRING_TYPE: return "ASCII_STRING";
    case SHORT_STRING_TYPE:
    case MEDIUM_STRING_TYPE:
    case LONG_STRING_TYPE: return "TWO_BYTE_STRING";
    case SHORT_CONS_STRING_TYPE:
    case MEDIUM_CONS_STRING_TYPE:
    case LONG_CONS_STRING_TYPE:
    case SHORT_CONS_ASCII_STRING_TYPE:
    case MEDIUM_CONS_ASCII_STRING_TYPE:
    case LONG_CONS_ASCII_STRING_TYPE: return "CONS_STRING";
    case SHORT_SLICED_STRING_TYPE:
    case MEDIUM_SLICED_STRING_TYPE:
    case LONG_SLICED_STRING_TYPE:
    case SHORT_SLICED_ASCII_STRING_TYPE:
    case MEDIUM_SLICED_ASCII_STRING_TYPE:
    case LONG_SLICED_ASCII_STRING_TYPE: return "SLICED_STRING";
    case SHORT_EXTERNAL_ASCII_STRING_TYPE:
    case MEDIUM_EXTERNAL_ASCII_STRING_TYPE:
    case LONG_EXTERNAL_ASCII_STRING_TYPE:
    case SHORT_EXTERNAL_STRING_TYPE:
    case MEDIUM_EXTERNAL_STRING_TYPE:
    case LONG_EXTERNAL_STRING_TYPE: return "EXTERNAL_STRING";
    case FIXED_ARRAY_TYPE: return "FIXED_ARRAY";
    case BYTE_ARRAY_TYPE: return "BYTE_ARRAY";
    case FILLER_TYPE: return "FILLER";
    case JS_OBJECT_TYPE: return "JS_OBJECT";
    case ODDBALL_TYPE: return "ODDBALL";
    case SHARED_FUNCTION_INFO_TYPE: return "SHARED_FUNCTION_INFO";
    case JS_FUNCTION_TYPE: return "JS_FUNCTION";
    case CODE_TYPE: return "CODE";
    case JS_ARRAY_TYPE: return "JS_ARRAY";
    case JS_VALUE_TYPE: return "JS_VALUE";
    case JS_GLOBAL_OBJECT_TYPE: return "JS_GLOBAL_OBJECT";
    case JS_BUILTINS_OBJECT_TYPE: return "JS_BUILTINS_OBJECT";
    case PROXY_TYPE: return "PROXY";
    case SMI_TYPE: return "SMI";
#define MAKE_STRUCT_CASE(NAME, Name, name) case NAME##_TYPE: return #NAME;
  STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
  }
  return "UNKNOWN";
}


void Map::MapPrint() {
  HeapObject::PrintHeader("Map");
  PrintF(" - type: %s\n", TypeToString(instance_type()));
  PrintF(" - instance size: %d\n", instance_size());
  PrintF(" - unused property fields: %d\n", unused_property_fields());
  PrintF(" - instance descriptors: ");
  instance_descriptors()->ShortPrint();
  PrintF("\n - prototype: ");
  prototype()->ShortPrint();
  PrintF("\n - constructor: ");
  constructor()->ShortPrint();
  PrintF("\n");
}


void Map::MapVerify() {
  ASSERT(!Heap::InNewSpace(this));
  ASSERT(FIRST_TYPE <= instance_type() && instance_type() <= LAST_TYPE);
  ASSERT(kPointerSize <= instance_size()
         && instance_size() < Heap::Capacity());
  VerifyHeapPointer(prototype());
  VerifyHeapPointer(instance_descriptors());
}


void FixedArray::FixedArrayPrint() {
  HeapObject::PrintHeader("FixedArray");
  PrintF(" - length: %d", length());
  for (int i = 0; i < length(); i++) {
    PrintF("\n  [%d]: ", i);
    get(i)->ShortPrint();
  }
  PrintF("\n");
}


void FixedArray::FixedArrayVerify() {
  for (int i = 0; i < length(); i++) {
    Object* e = get(i);
    if (e->IsHeapObject()) {
      VerifyHeapPointer(e);
    } else {
      e->Verify();
    }
  }
}


void JSValue::JSValuePrint() {
  HeapObject::PrintHeader("ValueObject");
  value()->Print();
}


void JSValue::JSValueVerify() {
  Object* v = value();
  if (v->IsHeapObject()) {
    VerifyHeapPointer(v);
  }
}


void String::StringPrint() {
  if (IsSymbol()) {
    PrintF("#");
  } else if (IsConsString()) {
    PrintF("c\"");
  } else {
    PrintF("\"");
  }

  for (int i = 0; i < length(); i++) {
    PrintF("%c", Get(i));
  }

  if (!IsSymbol()) PrintF("\"");
}


void String::StringVerify() {
  CHECK(IsString());
  CHECK(length() >= 0 && length() <= Smi::kMaxValue);
  if (IsSymbol()) {
    CHECK(!Heap::InNewSpace(this));
  }
}


void JSFunction::JSFunctionPrint() {
  HeapObject::PrintHeader("Function");
  PrintF(" - map = 0x%p\n", map());
  PrintF(" - is boilerplate: %s\n", IsBoilerplate() ? "yes" : "no");
  PrintF(" - initial_map = ");
  if (has_initial_map()) {
    initial_map()->ShortPrint();
  }
  PrintF("\n - shared_info = ");
  shared()->ShortPrint();
  PrintF("\n   - name = ");
  shared()->name()->Print();
  PrintF("\n - context = ");
  unchecked_context()->ShortPrint();
  PrintF("\n - code = ");
  code()->ShortPrint();
  PrintF("\n");

  PrintProperties();
  PrintElements();

  PrintF("\n");
}


void JSFunction::JSFunctionVerify() {
  CHECK(IsJSFunction());
  VerifyObjectField(kPrototypeOrInitialMapOffset);
}


void SharedFunctionInfo::SharedFunctionInfoPrint() {
  HeapObject::PrintHeader("SharedFunctionInfo");
  PrintF(" - name: ");
  name()->ShortPrint();
  PrintF("\n - expected_nof_properties: %d", expected_nof_properties());
  PrintF("\n - instance class name =");
  instance_class_name()->Print();
  PrintF("\n - code =");
  code()->ShortPrint();
  PrintF("\n - source code =");
  GetSourceCode()->ShortPrint();
  PrintF("\n - lazy load: %s",
         lazy_load_data() == Heap::undefined_value() ? "no" : "yes");
  // Script files are often large, hard to read.
  // PrintF("\n - script =");
  // script()->Print();
  PrintF("\n - function token position = %d", function_token_position());
  PrintF("\n - start position = %d", start_position());
  PrintF("\n - end position = %d", end_position());
  PrintF("\n - is expression = %d", is_expression());
  PrintF("\n - debug info = ");
  debug_info()->Print();
  PrintF("\n - length = %d", length());
  PrintF("\n");
}

void SharedFunctionInfo::SharedFunctionInfoVerify() {
  CHECK(IsSharedFunctionInfo());
  VerifyObjectField(kNameOffset);
  VerifyObjectField(kCodeOffset);
  VerifyObjectField(kInstanceClassNameOffset);
  VerifyObjectField(kExternalReferenceDataOffset);
  VerifyObjectField(kLazyLoadDataOffset);
  VerifyObjectField(kScriptOffset);
  VerifyObjectField(kDebugInfoOffset);
}


void JSGlobalObject::JSGlobalObjectPrint() {
  PrintF("global ");
  JSObjectPrint();
}


void JSGlobalObject::JSGlobalObjectVerify() {
  CHECK(IsJSGlobalObject());
  JSObjectVerify();
  for (int i = GlobalObject::kBuiltinsOffset;
       i < JSGlobalObject::kSize;
       i += kPointerSize) {
    VerifyObjectField(i);
  }
}


void JSBuiltinsObject::JSBuiltinsObjectPrint() {
  PrintF("builtins ");
  JSObjectPrint();
}


void JSBuiltinsObject::JSBuiltinsObjectVerify() {
  CHECK(IsJSBuiltinsObject());
  JSObjectVerify();
  for (int i = GlobalObject::kBuiltinsOffset;
       i < JSBuiltinsObject::kSize;
       i += kPointerSize) {
    VerifyObjectField(i);
  }
}


void Oddball::OddballVerify() {
  CHECK(IsOddball());
  VerifyHeapPointer(to_string());
  Object* number = to_number();
  if (number->IsHeapObject()) {
    ASSERT(number == Heap::nan_value());
  } else {
    ASSERT(number->IsSmi());
    int value = Smi::cast(number)->value();
    ASSERT(value == 0 || value == 1 || value == -1);
  }
}


const char* Code::Kind2String(Kind kind) {
  switch (kind) {
    case FUNCTION: return "FUNCTION";
    case STUB: return "STUB";
    case BUILTIN: return "BUILTIN";
    case LOAD_IC: return "LOAD_IC";
    case KEYED_LOAD_IC: return "KEYED_LOAD_IC";
    case STORE_IC: return "STORE_IC";
    case KEYED_STORE_IC: return "KEYED_STORE_IC";
    case CALL_IC: return "CALL_IC";
  }
  UNREACHABLE();
  return NULL;
}


void Code::CodePrint() {
  HeapObject::PrintHeader("Code");
  PrintF("kind = %s", Kind2String(kind()));

  PrintF("\nInstructions (size = %d)\n", instruction_size());
  Disassembler::Decode(NULL /*use PrintF*/, this);
  PrintF("\n");

  PrintF("RelocInfo (size = %d)\n", relocation_size());
  for (RelocIterator it(this); !it.done(); it.next())
    it.rinfo()->Print();
  PrintF("\n");
}


void Code::CodeVerify() {
  ASSERT(ic_flag() == IC_TARGET_IS_ADDRESS);
  for (RelocIterator it(this); !it.done(); it.next()) {
    it.rinfo()->Verify();
  }
}


void JSArray::JSArrayVerify() {
  JSObjectVerify();
  ASSERT(length()->IsNumber() || length()->IsUndefined());
  ASSERT(elements()->IsUndefined() || elements()->IsFixedArray());
}


void Proxy::ProxyPrint() {
  PrintF("proxy to %p", proxy());
}


void Proxy::ProxyVerify() {
  ASSERT(IsProxy());
}


void Dictionary::Print() {
  int capacity = Capacity();
  for (int i = 0; i < capacity; i++) {
    Object* k = KeyAt(i);
    if (IsKey(k)) {
      PrintF(" ");
      if (k->IsString()) {
        String::cast(k)->StringPrint();
      } else {
        k->ShortPrint();
      }
      PrintF(": ");
      ValueAt(i)->ShortPrint();
      PrintF("\n");
    }
  }
}


void AccessorInfo::AccessorInfoVerify() {
  CHECK(IsAccessorInfo());
  VerifyPointer(getter());
  VerifyPointer(setter());
  VerifyPointer(name());
  VerifyPointer(data());
  VerifyPointer(flag());
}

void AccessorInfo::AccessorInfoPrint() {
  PrintF("AccessorInfo");
  PrintF("\n - getter: ");
  getter()->ShortPrint();
  PrintF("\n - setter: ");
  setter()->ShortPrint();
  PrintF("\n - name: ");
  name()->ShortPrint();
  PrintF("\n - data: ");
  data()->ShortPrint();
  PrintF("\n - flag: ");
  flag()->ShortPrint();
}

void AccessCheckInfo::AccessCheckInfoVerify() {
  CHECK(IsAccessCheckInfo());
  VerifyPointer(named_callback());
  VerifyPointer(indexed_callback());
  VerifyPointer(data());
}

void AccessCheckInfo::AccessCheckInfoPrint() {
  PrintF("AccessCheckInfo");
  PrintF("\n - named_callback: ");
  named_callback()->ShortPrint();
  PrintF("\n - indexed_callback: ");
  indexed_callback()->ShortPrint();
  PrintF("\n - data: ");
  data()->ShortPrint();
}

void InterceptorInfo::InterceptorInfoVerify() {
  CHECK(IsInterceptorInfo());
  VerifyPointer(getter());
  VerifyPointer(setter());
  VerifyPointer(query());
  VerifyPointer(deleter());
  VerifyPointer(enumerator());
  VerifyPointer(data());
}

void InterceptorInfo::InterceptorInfoPrint() {
  PrintF("InterceptorInfo");
  PrintF("\n - getter: ");
  getter()->ShortPrint();
  PrintF("\n - setter: ");
  setter()->ShortPrint();
  PrintF("\n - query: ");
  query()->ShortPrint();
  PrintF("\n - deleter: ");
  deleter()->ShortPrint();
  PrintF("\n - enumerator: ");
  enumerator()->ShortPrint();
  PrintF("\n - data: ");
  data()->ShortPrint();
}

void CallHandlerInfo::CallHandlerInfoVerify() {
  CHECK(IsCallHandlerInfo());
  VerifyPointer(callback());
  VerifyPointer(data());
}

void CallHandlerInfo::CallHandlerInfoPrint() {
  PrintF("CallHandlerInfo");
  PrintF("\n - callback: ");
  callback()->ShortPrint();
  PrintF("\n - data: ");
  data()->ShortPrint();
}

void TemplateInfo::TemplateInfoVerify() {
  VerifyPointer(tag());
  VerifyPointer(property_list());
}

void FunctionTemplateInfo::FunctionTemplateInfoVerify() {
  CHECK(IsFunctionTemplateInfo());
  TemplateInfoVerify();
  VerifyPointer(serial_number());
  VerifyPointer(call_code());
  VerifyPointer(property_accessors());
  VerifyPointer(prototype_template());
  VerifyPointer(parent_template());
  VerifyPointer(named_property_handler());
  VerifyPointer(indexed_property_handler());
  VerifyPointer(instance_template());
  VerifyPointer(signature());
  VerifyPointer(access_check_info());
}

void FunctionTemplateInfo::FunctionTemplateInfoPrint() {
  PrintF("FunctionTemplateInfo");
  PrintF("\n - tag: ");
  tag()->ShortPrint();
  PrintF("\n - property_list: ");
  property_list()->ShortPrint();
  PrintF("\n - serial_number: ");
  serial_number()->ShortPrint();
  PrintF("\n - call_code: ");
  call_code()->ShortPrint();
  PrintF("\n - property_accessors: ");
  property_accessors()->ShortPrint();
  PrintF("\n - prototype_template: ");
  prototype_template()->ShortPrint();
  PrintF("\n - parent_template: ");
  parent_template()->ShortPrint();
  PrintF("\n - named_property_handler: ");
  named_property_handler()->ShortPrint();
  PrintF("\n - indexed_property_handler: ");
  indexed_property_handler()->ShortPrint();
  PrintF("\n - instance_template: ");
  instance_template()->ShortPrint();
  PrintF("\n - signature: ");
  signature()->ShortPrint();
  PrintF("\n - access_check_info: ");
  access_check_info()->ShortPrint();
  PrintF("\n - hidden_prototype: %s", hidden_prototype() ? "true" : "false");
  PrintF("\n - undetectable: %s", undetectable() ? "true" : "false");
  PrintF("\n - need_access_check: %s", needs_access_check() ? "true" : "false");
}

void ObjectTemplateInfo::ObjectTemplateInfoVerify() {
  CHECK(IsObjectTemplateInfo());
  TemplateInfoVerify();
  VerifyPointer(constructor());
  VerifyPointer(internal_field_count());
}

void ObjectTemplateInfo::ObjectTemplateInfoPrint() {
  PrintF("ObjectTemplateInfo");
  PrintF("\n - constructor: ");
  constructor()->ShortPrint();
  PrintF("\n - internal_field_count: ");
  internal_field_count()->ShortPrint();
}

void SignatureInfo::SignatureInfoVerify() {
  CHECK(IsSignatureInfo());
  VerifyPointer(receiver());
  VerifyPointer(args());
}

void SignatureInfo::SignatureInfoPrint() {
  PrintF("SignatureInfo");
  PrintF("\n - receiver: ");
  receiver()->ShortPrint();
  PrintF("\n - args: ");
  args()->ShortPrint();
}

void TypeSwitchInfo::TypeSwitchInfoVerify() {
  CHECK(IsTypeSwitchInfo());
  VerifyPointer(types());
}

void TypeSwitchInfo::TypeSwitchInfoPrint() {
  PrintF("TypeSwitchInfo");
  PrintF("\n - types: ");
  types()->ShortPrint();
}


void Script::ScriptVerify() {
  CHECK(IsScript());
  VerifyPointer(source());
  VerifyPointer(name());
  line_offset()->SmiVerify();
  column_offset()->SmiVerify();
  type()->SmiVerify();
}


void Script::ScriptPrint() {
  HeapObject::PrintHeader("Script");
  PrintF("\n - source: ");
  source()->ShortPrint();
  PrintF("\n - name: ");
  name()->ShortPrint();
  PrintF("\n - line_offset: ");
  line_offset()->ShortPrint();
  PrintF("\n - column_offset: ");
  column_offset()->ShortPrint();
  PrintF("\n - type: ");
  type()->ShortPrint();
  PrintF("\n");
}


void DebugInfo::DebugInfoVerify() {
  CHECK(IsDebugInfo());
  VerifyPointer(shared());
  VerifyPointer(original_code());
  VerifyPointer(code());
  VerifyPointer(break_points());
}


void DebugInfo::DebugInfoPrint() {
  PrintF("DebugInfo");
  PrintF("\n - shared");
  shared()->ShortPrint();
  PrintF("\n - original_code");
  original_code()->ShortPrint();
  PrintF("\n - code");
  code()->ShortPrint();
  PrintF("\n - break_points");
  break_points()->ShortPrint();
}


void BreakPointInfo::BreakPointInfoVerify() {
  CHECK(IsBreakPointInfo());
  code_position()->SmiVerify();
  source_position()->SmiVerify();
  statement_position()->SmiVerify();
  VerifyPointer(break_point_objects());
}


void BreakPointInfo::BreakPointInfoPrint() {
  PrintF("BreakPointInfo");
  PrintF("\n - code_position %d", code_position());
  PrintF("\n - source_position %d", source_position());
  PrintF("\n - statement_position %d", statement_position());
  PrintF("\n - break_point_objects ");
  break_point_objects()->ShortPrint();
}


void JSObject::IncrementSpillStatistics(SpillInformation* info) {
  info->number_of_objects_++;
  // Named properties
  if (HasFastProperties()) {
    info->number_of_objects_with_fast_properties_++;
    info->number_of_fast_used_fields_   += map()->NextFreePropertyIndex();
    info->number_of_fast_unused_fields_ += map()->unused_property_fields();
  } else {
    Dictionary* dict = property_dictionary();
    info->number_of_slow_used_properties_ += dict->NumberOfElements();
    info->number_of_slow_unused_properties_ +=
        dict->Capacity() - dict->NumberOfElements();
  }
  // Indexed properties
  if (HasFastElements()) {
    info->number_of_objects_with_fast_elements_++;
    int holes = 0;
    FixedArray* e = FixedArray::cast(elements());
    int len = e->length();
    for (int i = 0; i < len; i++) {
      if (e->get(i) == Heap::the_hole_value()) holes++;
    }
    info->number_of_fast_used_elements_   += len - holes;
    info->number_of_fast_unused_elements_ += holes;
  } else {
    Dictionary* dict = element_dictionary();
    info->number_of_slow_used_elements_ += dict->NumberOfElements();
    info->number_of_slow_unused_elements_ +=
        dict->Capacity() - dict->NumberOfElements();
  }
}


void JSObject::SpillInformation::Clear() {
  number_of_objects_ = 0;
  number_of_objects_with_fast_properties_ = 0;
  number_of_objects_with_fast_elements_ = 0;
  number_of_fast_used_fields_ = 0;
  number_of_fast_unused_fields_ = 0;
  number_of_slow_used_properties_ = 0;
  number_of_slow_unused_properties_ = 0;
  number_of_fast_used_elements_ = 0;
  number_of_fast_unused_elements_ = 0;
  number_of_slow_used_elements_ = 0;
  number_of_slow_unused_elements_ = 0;
}

void JSObject::SpillInformation::Print() {
  PrintF("\n  JSObject Spill Statistics (#%d):\n", number_of_objects_);

  PrintF("    - fast properties (#%d): %d (used) %d (unused)\n",
         number_of_objects_with_fast_properties_,
         number_of_fast_used_fields_, number_of_fast_unused_fields_);

  PrintF("    - slow properties (#%d): %d (used) %d (unused)\n",
         number_of_objects_ - number_of_objects_with_fast_properties_,
         number_of_slow_used_properties_, number_of_slow_unused_properties_);

  PrintF("    - fast elements (#%d): %d (used) %d (unused)\n",
         number_of_objects_with_fast_elements_,
         number_of_fast_used_elements_, number_of_fast_unused_elements_);

  PrintF("    - slow elements (#%d): %d (used) %d (unused)\n",
         number_of_objects_ - number_of_objects_with_fast_elements_,
         number_of_slow_used_elements_, number_of_slow_unused_elements_);

  PrintF("\n");
}


void DescriptorArray::PrintDescriptors() {
  PrintF("Descriptor array  %d\n", number_of_descriptors());
  int number = 0;
  for (DescriptorReader r(this); !r.eos(); r.advance()) {
    Descriptor desc;
    r.Get(&desc);
    PrintF(" %d: ", number++);
    desc.Print();
  }
  PrintF("\n");
}


#endif  // DEBUG

} }  // namespace v8::internal
