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
//
// Review notes:
//
// - The use of macros in these inline fuctions may seem superfluous
// but it is absolutely needed to make sure gcc generates optimal
// code. gcc is not happy when attempting to inline too deep.
//

#ifndef V8_OBJECTS_INL_H_
#define V8_OBJECTS_INL_H_

#include "objects.h"
#include "contexts.h"
#include "conversions-inl.h"
#include "property.h"

namespace v8 { namespace internal {

PropertyDetails::PropertyDetails(Smi* smi) {
  value_ = smi->value();
}


Smi* PropertyDetails::AsSmi() {
  return Smi::FromInt(value_);
}


#define CAST_ACCESSOR(type)                     \
  type* type::cast(Object* object) {            \
    ASSERT(object->Is##type());                 \
    return reinterpret_cast<type*>(object);     \
  }


#define INT_ACCESSORS(holder, name, offset)                             \
  int holder::name() { return READ_INT_FIELD(this, offset); }           \
  void holder::set_##name(int value) { WRITE_INT_FIELD(this, offset, value); }


#define ACCESSORS(holder, name, type, offset)                           \
  type* holder::name() { return type::cast(READ_FIELD(this, offset)); } \
  void holder::set_##name(type* value) {                                \
    WRITE_FIELD(this, offset, value);                                   \
    WRITE_BARRIER(this, offset);                                        \
  }


#define SMI_ACCESSORS(holder, name, offset)             \
  int holder::name() {                                  \
    Object* value = READ_FIELD(this, offset);           \
    return Smi::cast(value)->value();                   \
  }                                                     \
  void holder::set_##name(int value) {                  \
    WRITE_FIELD(this, offset, Smi::FromInt(value));     \
  }


#define BOOL_ACCESSORS(holder, field, name, offset) \
  bool holder::name() {                                    \
    return BooleanBit::get(field(), offset);               \
  }                                                        \
  void holder::set_##name(bool value) {                    \
    set_##field(BooleanBit::set(field(), offset, value));  \
  }


bool Object::IsSmi() {
  return HAS_SMI_TAG(this);
}


bool Object::IsHeapObject() {
  return HAS_HEAP_OBJECT_TAG(this);
}


bool Object::IsHeapNumber() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == HEAP_NUMBER_TYPE;
}


bool Object::IsString() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() < FIRST_NONSTRING_TYPE;
}


bool Object::IsSeqString() {
  return IsString()
    && (String::cast(this)->representation_tag() == kSeqStringTag);
}


bool Object::IsAsciiString() {
  return IsString() && (String::cast(this)->is_ascii());
}


bool Object::IsTwoByteString() {
  return IsString() && (!String::cast(this)->is_ascii());
}


bool Object::IsConsString() {
  return IsString()
    && (String::cast(this)->representation_tag() == kConsStringTag);
}


bool Object::IsSlicedString() {
  return IsString()
    && (String::cast(this)->representation_tag() == kSlicedStringTag);
}


bool Object::IsExternalString() {
  return IsString()
    && (String::cast(this)->representation_tag() == kExternalStringTag);
}


bool Object::IsExternalAsciiString() {
  return IsExternalString() && (String::cast(this)->is_ascii());
}


bool Object::IsExternalTwoByteString() {
  return IsExternalString() && (!String::cast(this)->is_ascii());
}


bool Object::IsShortString() {
  return IsString() && (String::cast(this)->size_tag() == kShortStringTag);
}


bool Object::IsMediumString() {
  return IsString() && (String::cast(this)->size_tag() == kMediumStringTag);
}


bool Object::IsLongString() {
  return IsString() && (String::cast(this)->size_tag() == kLongStringTag);
}


bool Object::IsSymbol() {
  return IsString() && (String::cast(this)->is_symbol());
}


bool Object::IsNumber() {
  return IsSmi() || IsHeapNumber();
}


bool Object::IsByteArray() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == BYTE_ARRAY_TYPE;
}


bool Object::IsFailure() {
  return HAS_FAILURE_TAG(this);
}


bool Object::IsRetryAfterGC() {
  return HAS_FAILURE_TAG(this)
    && Failure::cast(this)->type() == Failure::RETRY_AFTER_GC;
}


bool Object::IsException() {
  return this == Failure::Exception();
}


bool Object::IsJSObject() {
  return IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() >= JS_OBJECT_TYPE;
}


bool Object::IsMap() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == MAP_TYPE;
}


bool Object::IsFixedArray() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == FIXED_ARRAY_TYPE;
}


bool Object::IsDescriptorArray() {
  return IsFixedArray();
}


bool Object::IsContext() {
  return Object::IsHeapObject()
    && (HeapObject::cast(this)->map() == Heap::context_map() ||
        HeapObject::cast(this)->map() == Heap::global_context_map());
}


bool Object::IsGlobalContext() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map() == Heap::global_context_map();
}


bool Object::IsJSFunction() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == JS_FUNCTION_TYPE;
}


template <> static inline bool Is<JSFunction>(Object* obj) {
  return obj->IsJSFunction();
}


bool Object::IsCode() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == CODE_TYPE;
}


bool Object::IsOddball() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == ODDBALL_TYPE;
}


bool Object::IsSharedFunctionInfo() {
  return Object::IsHeapObject() &&
      (HeapObject::cast(this)->map()->instance_type() ==
       SHARED_FUNCTION_INFO_TYPE);
}


bool Object::IsJSValue() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == JS_VALUE_TYPE;
}


bool Object::IsProxy() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == PROXY_TYPE;
}


bool Object::IsBoolean() {
  return IsTrue() || IsFalse();
}


bool Object::IsJSArray() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map()->instance_type() == JS_ARRAY_TYPE;
}


template <> static inline bool Is<JSArray>(Object* obj) {
  return obj->IsJSArray();
}


bool Object::IsHashTable() {
  return Object::IsHeapObject()
    && HeapObject::cast(this)->map() == Heap::hash_table_map();
}


bool Object::IsDictionary() {
  return IsHashTable() && this != Heap::symbol_table();
}


bool Object::IsSymbolTable() {
  return IsHashTable() && this == Heap::symbol_table();
}


bool Object::IsPrimitive() {
  return IsOddball() || IsNumber() || IsString();
}


bool Object::IsGlobalObject() {
  return IsHeapObject() &&
      ((HeapObject::cast(this)->map()->instance_type() ==
        JS_GLOBAL_OBJECT_TYPE) ||
       (HeapObject::cast(this)->map()->instance_type() ==
        JS_BUILTINS_OBJECT_TYPE));
}


bool Object::IsJSGlobalObject() {
#ifdef DEBUG
  if (IsHeapObject() &&
      (HeapObject::cast(this)->map()->instance_type() ==
       JS_GLOBAL_OBJECT_TYPE)) {
    ASSERT(IsAccessCheckNeeded());
  }
#endif
  return IsHeapObject() &&
      (HeapObject::cast(this)->map()->instance_type() ==
       JS_GLOBAL_OBJECT_TYPE);
}


bool Object::IsJSBuiltinsObject() {
  return IsHeapObject() &&
      (HeapObject::cast(this)->map()->instance_type() ==
       JS_BUILTINS_OBJECT_TYPE);
}


bool Object::IsUndetectableObject() {
  return IsHeapObject()
    && HeapObject::cast(this)->map()->is_undetectable();
}


bool Object::IsAccessCheckNeeded() {
  return IsHeapObject()
    && HeapObject::cast(this)->map()->needs_access_check();
}


bool Object::IsStruct() {
  if (!IsHeapObject()) return false;
  switch (HeapObject::cast(this)->map()->instance_type()) {
#define MAKE_STRUCT_CASE(NAME, Name, name) case NAME##_TYPE: return true;
  STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
    default: return false;
  }
}


#define MAKE_STRUCT_PREDICATE(NAME, Name, name)                  \
  bool Object::Is##Name() {                                      \
    return Object::IsHeapObject()                                \
      && HeapObject::cast(this)->map()->instance_type() == NAME##_TYPE; \
  }
  STRUCT_LIST(MAKE_STRUCT_PREDICATE)
#undef MAKE_STRUCT_PREDICATE


bool Object::IsUndefined() {
  return this == Heap::undefined_value();
}


bool Object::IsTheHole() {
  return this == Heap::the_hole_value();
}


bool Object::IsNull() {
  return this == Heap::null_value();
}


bool Object::IsTrue() {
  return this == Heap::true_value();
}


bool Object::IsFalse() {
  return this == Heap::false_value();
}


double Object::Number() {
  ASSERT(IsNumber());
  return IsSmi()
    ? static_cast<double>(reinterpret_cast<Smi*>(this)->value())
    : reinterpret_cast<HeapNumber*>(this)->value();
}



Object* Object::ToSmi() {
  if (IsSmi()) return this;
  if (IsHeapNumber()) {
    double value = HeapNumber::cast(this)->value();
    int int_value = FastD2I(value);
    if (value == FastI2D(int_value) && Smi::IsValid(int_value)) {
      return Smi::FromInt(int_value);
    }
  }
  return Failure::Exception();
}


Object* Object::GetElement(uint32_t index) {
  return GetElementWithReceiver(this, index);
}


Object* Object::GetProperty(String* key) {
  PropertyAttributes attributes;
  return GetPropertyWithReceiver(this, key, &attributes);
}


Object* Object::GetProperty(String* key, PropertyAttributes* attributes) {
  return GetPropertyWithReceiver(this, key, attributes);
}


#define FIELD_ADDR(p, offset) \
  (reinterpret_cast<byte*>(p) + offset - kHeapObjectTag)

#define READ_FIELD(p, offset) \
  (*reinterpret_cast<Object**>(FIELD_ADDR(p, offset)))

#define WRITE_FIELD(p, offset, value) \
  (*reinterpret_cast<Object**>(FIELD_ADDR(p, offset)) = value)

#define WRITE_BARRIER(object, offset) \
  Heap::RecordWrite(object->address(), offset);

#define READ_DOUBLE_FIELD(p, offset) \
  (*reinterpret_cast<double*>(FIELD_ADDR(p, offset)))

#define WRITE_DOUBLE_FIELD(p, offset, value) \
  (*reinterpret_cast<double*>(FIELD_ADDR(p, offset)) = value)

#define READ_INT_FIELD(p, offset) \
  (*reinterpret_cast<int*>(FIELD_ADDR(p, offset)))

#define WRITE_INT_FIELD(p, offset, value) \
  (*reinterpret_cast<int*>(FIELD_ADDR(p, offset)) = value)

#define READ_SHORT_FIELD(p, offset) \
  (*reinterpret_cast<uint16_t*>(FIELD_ADDR(p, offset)))

#define WRITE_SHORT_FIELD(p, offset, value) \
  (*reinterpret_cast<uint16_t*>(FIELD_ADDR(p, offset)) = value)

#define READ_BYTE_FIELD(p, offset) \
  (*reinterpret_cast<byte*>(FIELD_ADDR(p, offset)))

#define WRITE_BYTE_FIELD(p, offset, value) \
  (*reinterpret_cast<byte*>(FIELD_ADDR(p, offset)) = value)


Object* HeapObject::GetHeapObjectField(HeapObject* obj, int index) {
  return READ_FIELD(obj, HeapObject::kSize + kPointerSize * index);
}


int Smi::value() {
  return reinterpret_cast<int>(this) >> kSmiTagSize;
}


Smi* Smi::FromInt(int value) {
  ASSERT(Smi::IsValid(value));
  return reinterpret_cast<Smi*>((value << kSmiTagSize) | kSmiTag);
}


Failure::Type Failure::type() const {
  return static_cast<Type>(value() & kFailureTypeTagMask);
}


bool Failure::IsInternalError() const {
  return type() == INTERNAL_ERROR;
}


bool Failure::IsOutOfMemoryException() const {
  return type() == OUT_OF_MEMORY_EXCEPTION;
}


int Failure::requested() const {
  const int kShiftBits =
      kFailureTypeTagSize + kSpaceTagSize - kObjectAlignmentBits;
  STATIC_ASSERT(kShiftBits >= 0);
  ASSERT(type() == RETRY_AFTER_GC);
  return value() >> kShiftBits;
}


AllocationSpace Failure::allocation_space() const {
  ASSERT_EQ(RETRY_AFTER_GC, type());
  return static_cast<AllocationSpace>((value() >> kFailureTypeTagSize)
                                      & kSpaceTagMask);
}


Failure* Failure::InternalError() {
  return Construct(INTERNAL_ERROR);
}


Failure* Failure::Exception() {
  return Construct(EXCEPTION);
}

Failure* Failure::OutOfMemoryException() {
  return Construct(OUT_OF_MEMORY_EXCEPTION);
}


int Failure::value() const {
  return reinterpret_cast<int>(this) >> kFailureTagSize;
}


Failure* Failure::Construct(Type type, int value) {
  int info = (value << kFailureTypeTagSize) | type;
  ASSERT(Smi::IsValid(info));  // Same validation check as in Smi
  return reinterpret_cast<Failure*>((info << kFailureTagSize) | kFailureTag);
}


bool Smi::IsValid(int value) {
#ifdef DEBUG
  bool in_range = (value >= kMinValue) && (value <= kMaxValue);
#endif
  // To be representable as an tagged small integer, the two
  // most-significant bits of 'value' must be either 00 or 11 due to
  // sign-extension. To check this we add 01 to the two
  // most-significant bits, and check if the most-significant bit is 0
  //
  // CAUTION: The original code below:
  // bool result = ((value + 0x40000000) & 0x80000000) == 0;
  // may lead to incorrect results according to the C language spec, and
  // in fact doesn't work correctly with gcc4.1.1 in some cases: The
  // compiler may produce undefined results in case of signed integer
  // overflow. The computation must be done w/ unsigned ints.
  bool result =
      ((static_cast<unsigned int>(value) + 0x40000000U) & 0x80000000U) == 0;
  ASSERT(result == in_range);
  return result;
}


#ifdef DEBUG
void HeapObject::VerifyObjectField(int offset) {
  VerifyPointer(READ_FIELD(this, offset));
}
#endif


Map* HeapObject::map() {
  return reinterpret_cast<Map*> READ_FIELD(this, kMapOffset);
}


void HeapObject::set_map(Map* value) {
  WRITE_FIELD(this, kMapOffset, value);
}




HeapObject* HeapObject::FromAddress(Address address) {
  ASSERT_TAG_ALIGNED(address);
  return reinterpret_cast<HeapObject*>(address + kHeapObjectTag);
}


Address HeapObject::address() {
  return reinterpret_cast<Address>(this) - kHeapObjectTag;
}


int HeapObject::Size() {
  return SizeFromMap(map());
}


void HeapObject::IteratePointers(ObjectVisitor* v, int start, int end) {
  v->VisitPointers(reinterpret_cast<Object**>(FIELD_ADDR(this, start)),
                   reinterpret_cast<Object**>(FIELD_ADDR(this, end)));
}


void HeapObject::IteratePointer(ObjectVisitor* v, int offset) {
  v->VisitPointer(reinterpret_cast<Object**>(FIELD_ADDR(this, offset)));
}


void HeapObject::CopyBody(JSObject* from) {
  ASSERT(map() == from->map());
  ASSERT(Size() == from->Size());
  int object_size = Size();
  for (int offset = kSize; offset < object_size;  offset += kPointerSize) {
    Object* value = READ_FIELD(from, offset);
    // Note: WRITE_FIELD does not update the write barrier.
    WRITE_FIELD(this, offset, value);
    WRITE_BARRIER(this, offset);
  }
}


double HeapNumber::value() {
  return READ_DOUBLE_FIELD(this, kValueOffset);
}


void HeapNumber::set_value(double value) {
  WRITE_DOUBLE_FIELD(this, kValueOffset, value);
}


ACCESSORS(JSObject, properties, FixedArray, kPropertiesOffset)
ACCESSORS(JSObject, elements, HeapObject, kElementsOffset)


void JSObject::initialize_properties() {
  ASSERT(!Heap::InNewSpace(Heap::empty_fixed_array()));
  WRITE_FIELD(this, kPropertiesOffset, Heap::empty_fixed_array());
}


void JSObject::initialize_elements() {
  ASSERT(!Heap::InNewSpace(Heap::empty_fixed_array()));
  WRITE_FIELD(this, kElementsOffset, Heap::empty_fixed_array());
}


ACCESSORS(Oddball, to_string, String, kToStringOffset)
ACCESSORS(Oddball, to_number, Object, kToNumberOffset)


int JSObject::GetHeaderSize() {
  switch (map()->instance_type()) {
    case JS_GLOBAL_OBJECT_TYPE:
      return JSGlobalObject::kSize;
    case JS_BUILTINS_OBJECT_TYPE:
      return JSBuiltinsObject::kSize;
    case JS_FUNCTION_TYPE:
      return JSFunction::kSize;
    case JS_VALUE_TYPE:
      return JSValue::kSize;
    case JS_ARRAY_TYPE:
      return JSValue::kSize;
    case JS_OBJECT_TYPE:
      return JSObject::kHeaderSize;
    default:
      UNREACHABLE();
      return 0;
  }
}


int JSObject::GetInternalFieldCount() {
  ASSERT(1 << kPointerSizeLog2 == kPointerSize);
  return (Size() - GetHeaderSize()) >> kPointerSizeLog2;
}


Object* JSObject::GetInternalField(int index) {
  ASSERT(index < GetInternalFieldCount() && index >= 0);
  return READ_FIELD(this, GetHeaderSize() + (kPointerSize * index));
}


void JSObject::SetInternalField(int index, Object* value) {
  ASSERT(index < GetInternalFieldCount() && index >= 0);
  int offset = GetHeaderSize() + (kPointerSize * index);
  WRITE_FIELD(this, offset, value);
  WRITE_BARRIER(this, offset);
}


void JSObject::InitializeBody(int object_size) {
  for (int offset = kHeaderSize; offset < object_size; offset += kPointerSize) {
    WRITE_FIELD(this, offset, Heap::undefined_value());
  }
}


void Struct::InitializeBody(int object_size) {
  for (int offset = kSize; offset < object_size; offset += kPointerSize) {
    WRITE_FIELD(this, offset, Heap::undefined_value());
  }
}


bool JSObject::HasFastProperties() {
  return !properties()->IsDictionary();
}


bool Array::IndexFromObject(Object* object, uint32_t* index) {
  if (object->IsSmi()) {
    int value = Smi::cast(object)->value();
    if (value < 0) return false;
    *index = value;
    return true;
  }
  if (object->IsHeapNumber()) {
    double value = HeapNumber::cast(object)->value();
    uint32_t uint_value = static_cast<uint32_t>(value);
    if (value == static_cast<double>(uint_value)) {
      *index = uint_value;
      return true;
    }
  }
  return false;
}


bool Object::IsStringObjectWithCharacterAt(uint32_t index) {
  if (!this->IsJSValue()) return false;

  JSValue* js_value = JSValue::cast(this);
  if (!js_value->value()->IsString()) return false;

  String* str = String::cast(js_value->value());
  if (index >= (uint32_t)str->length()) return false;

  return true;
}


Object* FixedArray::get(int index) {
  ASSERT(index >= 0 && index < this->length());
  return READ_FIELD(this, kHeaderSize + index * kPointerSize);
}


void FixedArray::set(int index, Object* value) {
  ASSERT(index >= 0 && index < this->length());
  int offset = kHeaderSize + index * kPointerSize;
  WRITE_FIELD(this, offset, value);
  WRITE_BARRIER(this, offset);
}


FixedArray::WriteBarrierMode FixedArray::GetWriteBarrierMode() {
  if (Heap::InNewSpace(this)) return SKIP_WRITE_BARRIER;
  return UPDATE_WRITE_BARRIER;
}


void FixedArray::set(int index,
                     Object* value,
                     FixedArray::WriteBarrierMode mode) {
  ASSERT(index >= 0 && index < this->length());
  int offset = kHeaderSize + index * kPointerSize;
  WRITE_FIELD(this, offset, value);
  if (mode == UPDATE_WRITE_BARRIER) {
    WRITE_BARRIER(this, offset);
  } else {
    ASSERT(mode == SKIP_WRITE_BARRIER);
    ASSERT(Heap::InNewSpace(this) || !Heap::InNewSpace(value));
  }
}


void FixedArray::fast_set(FixedArray* array, int index, Object* value) {
  ASSERT(index >= 0 && index < array->length());
  WRITE_FIELD(array, kHeaderSize + index * kPointerSize, value);
}


void FixedArray::set_undefined(int index) {
  ASSERT(index >= 0 && index < this->length());
  ASSERT(!Heap::InNewSpace(Heap::undefined_value()));
  WRITE_FIELD(this, kHeaderSize + index * kPointerSize,
              Heap::undefined_value());
}


void FixedArray::set_the_hole(int index) {
  ASSERT(index >= 0 && index < this->length());
  ASSERT(!Heap::InNewSpace(Heap::the_hole_value()));
  WRITE_FIELD(this, kHeaderSize + index * kPointerSize, Heap::the_hole_value());
}


void DescriptorArray::fast_swap(FixedArray* array, int first, int second) {
  Object* tmp = array->get(first);
  fast_set(array, first, array->get(second));
  fast_set(array, second, tmp);
}


int DescriptorArray::Search(String* name) {
  SLOW_ASSERT(IsSortedNoDuplicates());

  // Check for empty descriptor array.
  int nof = number_of_descriptors();
  if (nof == 0) return kNotFound;

  // Fast case: do linear search for small arrays.
  const int kMaxElementsForLinearSearch = 8;
  if (name->IsSymbol() && nof < kMaxElementsForLinearSearch) {
    for (int number = 0; number < nof; number++) {
      if (name == GetKey(number)) return number;
    }
    return kNotFound;
  }

  // Slow case: perform binary search.
  return BinarySearch(name, 0, nof - 1);
}



String* DescriptorArray::GetKey(int descriptor_number) {
  ASSERT(descriptor_number < number_of_descriptors());
  return String::cast(get(ToKeyIndex(descriptor_number)));
}


Object* DescriptorArray::GetValue(int descriptor_number) {
  ASSERT(descriptor_number < number_of_descriptors());
  return GetContentArray()->get(ToValueIndex(descriptor_number));
}


Smi* DescriptorArray::GetDetails(int descriptor_number) {
  ASSERT(descriptor_number < number_of_descriptors());
  return Smi::cast(GetContentArray()->get(ToDetailsIndex(descriptor_number)));
}


void DescriptorArray::Get(int descriptor_number, Descriptor* desc) {
  desc->Init(GetKey(descriptor_number),
             GetValue(descriptor_number),
             GetDetails(descriptor_number));
}


void DescriptorArray::Set(int descriptor_number, Descriptor* desc) {
  // Range check.
  ASSERT(descriptor_number < number_of_descriptors());

  // Make sure non of the elements in desc are in new space.
  ASSERT(!Heap::InNewSpace(desc->GetKey()));
  ASSERT(!Heap::InNewSpace(desc->GetValue()));

  fast_set(this, ToKeyIndex(descriptor_number), desc->GetKey());
  FixedArray* content_array = GetContentArray();
  fast_set(content_array, ToValueIndex(descriptor_number), desc->GetValue());
  fast_set(content_array, ToDetailsIndex(descriptor_number),
           desc->GetDetails().AsSmi());
}


void DescriptorArray::Swap(int first, int second) {
  fast_swap(this, ToKeyIndex(first), ToKeyIndex(second));
  FixedArray* content_array = GetContentArray();
  fast_swap(content_array, ToValueIndex(first), ToValueIndex(second));
  fast_swap(content_array, ToDetailsIndex(first),  ToDetailsIndex(second));
}


bool Dictionary::requires_slow_elements() {
  Object* max_index_object = get(kPrefixStartIndex);
  if (!max_index_object->IsSmi()) return false;
  return 0 !=
      (Smi::cast(max_index_object)->value() & kRequiresSlowElementsMask);
}


uint32_t Dictionary::max_number_key() {
  ASSERT(!requires_slow_elements());
  Object* max_index_object = get(kPrefixStartIndex);
  if (!max_index_object->IsSmi()) return 0;
  uint32_t value = static_cast<uint32_t>(Smi::cast(max_index_object)->value());
  return value >> kRequiresSlowElementsTagSize;
}


// ------------------------------------
// Cast operations


CAST_ACCESSOR(FixedArray)
CAST_ACCESSOR(DescriptorArray)
CAST_ACCESSOR(Dictionary)
CAST_ACCESSOR(SymbolTable)
CAST_ACCESSOR(String)
CAST_ACCESSOR(SeqString)
CAST_ACCESSOR(AsciiString)
CAST_ACCESSOR(TwoByteString)
CAST_ACCESSOR(ConsString)
CAST_ACCESSOR(SlicedString)
CAST_ACCESSOR(ExternalString)
CAST_ACCESSOR(ExternalAsciiString)
CAST_ACCESSOR(ExternalTwoByteString)
CAST_ACCESSOR(JSObject)
CAST_ACCESSOR(Smi)
CAST_ACCESSOR(Failure)
CAST_ACCESSOR(HeapObject)
CAST_ACCESSOR(HeapNumber)
CAST_ACCESSOR(Oddball)
CAST_ACCESSOR(SharedFunctionInfo)
CAST_ACCESSOR(Map)
CAST_ACCESSOR(JSFunction)
CAST_ACCESSOR(JSGlobalObject)
CAST_ACCESSOR(JSBuiltinsObject)
CAST_ACCESSOR(Code)
CAST_ACCESSOR(JSArray)
CAST_ACCESSOR(Proxy)
CAST_ACCESSOR(ByteArray)
CAST_ACCESSOR(Struct)


#define MAKE_STRUCT_CAST(NAME, Name, name) CAST_ACCESSOR(Name)
  STRUCT_LIST(MAKE_STRUCT_CAST)
#undef MAKE_STRUCT_CAST

template <int prefix_size, int elem_size>
HashTable<prefix_size, elem_size>* HashTable<prefix_size, elem_size>::cast(
    Object* obj) {
  ASSERT(obj->IsHashTable());
  return reinterpret_cast<HashTable*>(obj);
}


INT_ACCESSORS(Array, length, kLengthOffset)


bool String::Equals(String* other) {
  if (other == this) return true;
  if (IsSymbol() && other->IsSymbol()) return false;
  return SlowEquals(other);
}


int String::length() {
  uint32_t len = READ_INT_FIELD(this, kLengthOffset);

  switch (size_tag()) {
    case kShortStringTag:
      return  len >> kShortLengthShift;
    case kMediumStringTag:
      return len >> kMediumLengthShift;
    case kLongStringTag:
      return len >> kLongLengthShift;
    default:
      break;
  }
  UNREACHABLE();
  return 0;
}


void String::set_length(int value) {
  switch (size_tag()) {
    case kShortStringTag:
      WRITE_INT_FIELD(this, kLengthOffset, value << kShortLengthShift);
      break;
    case kMediumStringTag:
      WRITE_INT_FIELD(this, kLengthOffset, value << kMediumLengthShift);
      break;
    case kLongStringTag:
      WRITE_INT_FIELD(this, kLengthOffset, value << kLongLengthShift);
      break;
    default:
      UNREACHABLE();
      break;
  }
}


int String::length_field() {
  return READ_INT_FIELD(this, kLengthOffset);
}


void String::set_length_field(int value) {
  WRITE_INT_FIELD(this, kLengthOffset, value);
}


void String::TryFlatten() {
  Flatten();
}


uint16_t String::Get(int index) {
  ASSERT(index >= 0 && index < length());
  switch (representation_tag()) {
    case kSeqStringTag:
      return is_ascii()
        ? AsciiString::cast(this)->AsciiStringGet(index)
        : TwoByteString::cast(this)->TwoByteStringGet(index);
    case kConsStringTag:
      return ConsString::cast(this)->ConsStringGet(index);
    case kSlicedStringTag:
      return SlicedString::cast(this)->SlicedStringGet(index);
    case kExternalStringTag:
      return is_ascii()
        ? ExternalAsciiString::cast(this)->ExternalAsciiStringGet(index)
        : ExternalTwoByteString::cast(this)->ExternalTwoByteStringGet(index);
    default:
      break;
  }

  UNREACHABLE();
  return 0;
}


void String::Set(int index, uint16_t value) {
  ASSERT(index >= 0 && index < length());
  ASSERT(IsSeqString());

  return is_ascii()
      ? AsciiString::cast(this)->AsciiStringSet(index, value)
      : TwoByteString::cast(this)->TwoByteStringSet(index, value);
}


bool String::IsAscii() {
  return is_ascii();
}


bool String::StringIsConsString() {
  return representation_tag() == kConsStringTag;
}


bool String::StringIsSlicedString() {
  return representation_tag() == kSlicedStringTag;
}


uint32_t String::size_tag() {
  return map_size_tag(map());
}


uint32_t String::map_size_tag(Map* map) {
  return map->instance_type() & kStringSizeMask;
}


bool String::is_symbol() {
  return is_symbol_map(map());
}


bool String::is_symbol_map(Map* map) {
  return (map->instance_type() & kIsSymbolMask) != 0;
}


bool String::is_ascii() {
  return is_ascii_map(map());
}


bool String::is_ascii_map(Map* map) {
  return (map->instance_type() & kStringEncodingMask) != 0;
}


StringRepresentationTag String::representation_tag() {
  return map_representation_tag(map());
}


StringRepresentationTag String::map_representation_tag(Map* map) {
  uint32_t tag = map->instance_type() & kStringRepresentationMask;
  return static_cast<StringRepresentationTag>(tag);
}


bool String::IsFlat() {
  String* current = this;
  while (true) {
    switch (current->representation_tag()) {
      case kConsStringTag:
        return String::cast(ConsString::cast(current)->second())->length() == 0;
      case kSlicedStringTag:
        current = String::cast(SlicedString::cast(this)->buffer());
        break;
      default:
        return true;
    }
  }
}


uint16_t AsciiString::AsciiStringGet(int index) {
  ASSERT(index >= 0 && index < length());
  return READ_BYTE_FIELD(this, kHeaderSize + index * kCharSize);
}


void AsciiString::AsciiStringSet(int index, uint16_t value) {
  ASSERT(index >= 0 && index < length() && value <= kMaxAsciiCharCode);
  WRITE_BYTE_FIELD(this, kHeaderSize + index * kCharSize,
                   static_cast<byte>(value));
}


Address AsciiString::GetCharsAddress() {
  return FIELD_ADDR(this, kHeaderSize);
}


uint16_t TwoByteString::TwoByteStringGet(int index) {
  ASSERT(index >= 0 && index < length());
  return READ_SHORT_FIELD(this, kHeaderSize + index * kShortSize);
}


void TwoByteString::TwoByteStringSet(int index, uint16_t value) {
  ASSERT(index >= 0 && index < length());
  WRITE_SHORT_FIELD(this, kHeaderSize + index * kShortSize, value);
}


int TwoByteString::TwoByteStringSize(Map* map) {
  uint32_t length = READ_INT_FIELD(this, kLengthOffset);

  // Use the map (and not 'this') to compute the size tag, since
  // TwoByteStringSize is called during GC when maps are encoded.
  switch (map_size_tag(map)) {
    case kShortStringTag:
      length = length >> kShortLengthShift;
      break;
    case kMediumStringTag:
      length = length >> kMediumLengthShift;
      break;
    case kLongStringTag:
      length = length >> kLongLengthShift;
      break;
    default:
      break;
  }
  return SizeFor(length);
}


int AsciiString::AsciiStringSize(Map* map) {
  uint32_t length = READ_INT_FIELD(this, kLengthOffset);

  // Use the map (and not 'this') to compute the size tag, since
  // AsciiStringSize is called during GC when maps are encoded.
  switch (map_size_tag(map)) {
    case kShortStringTag:
      length = length >> kShortLengthShift;
      break;
    case kMediumStringTag:
      length = length >> kMediumLengthShift;
      break;
    case kLongStringTag:
      length = length >> kLongLengthShift;
      break;
    default:
      break;
  }

  return SizeFor(length);
}


Object* ConsString::first() {
  return READ_FIELD(this, kFirstOffset);
}


void ConsString::set_first(Object* value) {
  WRITE_FIELD(this, kFirstOffset, value);
  WRITE_BARRIER(this, kFirstOffset);
}


Object* ConsString::second() {
  return READ_FIELD(this, kSecondOffset);
}


void ConsString::set_second(Object* value) {
  WRITE_FIELD(this, kSecondOffset, value);
  WRITE_BARRIER(this, kSecondOffset);
}


Object* SlicedString::buffer() {
  return READ_FIELD(this, kBufferOffset);
}


void SlicedString::set_buffer(Object* buffer) {
  WRITE_FIELD(this, kBufferOffset, buffer);
  WRITE_BARRIER(this, kBufferOffset);
}


int SlicedString::start() {
  return READ_INT_FIELD(this, kStartOffset);
}


void SlicedString::set_start(int start) {
  WRITE_INT_FIELD(this, kStartOffset, start);
}


ExternalAsciiString::Resource* ExternalAsciiString::resource() {
  return *reinterpret_cast<Resource**>(FIELD_ADDR(this, kResourceOffset));
}


void ExternalAsciiString::set_resource(
    ExternalAsciiString::Resource* resource) {
  *reinterpret_cast<Resource**>(FIELD_ADDR(this, kResourceOffset)) = resource;
}


ExternalTwoByteString::Resource* ExternalTwoByteString::resource() {
  return *reinterpret_cast<Resource**>(FIELD_ADDR(this, kResourceOffset));
}


void ExternalTwoByteString::set_resource(
    ExternalTwoByteString::Resource* resource) {
  *reinterpret_cast<Resource**>(FIELD_ADDR(this, kResourceOffset)) = resource;
}


byte ByteArray::get(int index) {
  ASSERT(index >= 0 && index < this->length());
  return READ_BYTE_FIELD(this, kHeaderSize + index * kCharSize);
}


void ByteArray::set(int index, byte value) {
  ASSERT(index >= 0 && index < this->length());
  WRITE_BYTE_FIELD(this, kHeaderSize + index * kCharSize, value);
}


int ByteArray::get_int(int index) {
  ASSERT(index >= 0 && (index * kIntSize) < this->length());
  return READ_INT_FIELD(this, kHeaderSize + index * kIntSize);
}


ByteArray* ByteArray::FromDataStartAddress(Address address) {
  ASSERT_TAG_ALIGNED(address);
  return reinterpret_cast<ByteArray*>(address - kHeaderSize + kHeapObjectTag);
}


Address ByteArray::GetDataStartAddress() {
  return reinterpret_cast<Address>(this) - kHeapObjectTag + kHeaderSize;
}


int Map::instance_size() {
  return READ_BYTE_FIELD(this, kInstanceSizeOffset);
}


int HeapObject::SizeFromMap(Map* map) {
  InstanceType instance_type = map->instance_type();
  // Only inline the two most frequent cases.
  if (instance_type == JS_OBJECT_TYPE) return  map->instance_size();
  if (instance_type == FIXED_ARRAY_TYPE) {
    return reinterpret_cast<FixedArray*>(this)->FixedArraySize();
  }
  // Otherwise do the general size computation.
  return SlowSizeFromMap(map);
}


void Map::set_instance_size(int value) {
  ASSERT(0 <= value && value < 256);
  WRITE_BYTE_FIELD(this, kInstanceSizeOffset, static_cast<byte>(value));
}


InstanceType Map::instance_type() {
  return static_cast<InstanceType>(READ_BYTE_FIELD(this, kInstanceTypeOffset));
}


void Map::set_instance_type(InstanceType value) {
  ASSERT(0 <= value && value < 256);
  WRITE_BYTE_FIELD(this, kInstanceTypeOffset, value);
}


int Map::unused_property_fields() {
  return READ_BYTE_FIELD(this, kUnusedPropertyFieldsOffset);
}


void Map::set_unused_property_fields(int value) {
  WRITE_BYTE_FIELD(this, kUnusedPropertyFieldsOffset, Min(value, 255));
}


byte Map::bit_field() {
  return READ_BYTE_FIELD(this, kBitFieldOffset);
}


void Map::set_bit_field(byte value) {
  WRITE_BYTE_FIELD(this, kBitFieldOffset, value);
}


void Map::set_non_instance_prototype(bool value) {
  if (value) {
    set_bit_field(bit_field() | (1 << kHasNonInstancePrototype));
  } else {
    set_bit_field(bit_field() & ~(1 << kHasNonInstancePrototype));
  }
}


bool Map::has_non_instance_prototype() {
  return ((1 << kHasNonInstancePrototype) & bit_field()) != 0;
}


Code::Flags Code::flags() {
  return static_cast<Flags>(READ_INT_FIELD(this, kFlagsOffset));
}


void Code::set_flags(Code::Flags flags) {
  // Make sure that all call stubs have an arguments count.
  ASSERT(ExtractKindFromFlags(flags) != CALL_IC ||
         ExtractArgumentsCountFromFlags(flags) >= 0);
  WRITE_INT_FIELD(this, kFlagsOffset, flags);
}


Code::Kind Code::kind() {
  return ExtractKindFromFlags(flags());
}


InlineCacheState Code::state() {
  InlineCacheState result = ExtractStateFromFlags(flags());
  // Only allow uninitialized or debugger states for non-IC code
  // objects. This is used in the debugger to determine whether or not
  // a call to code object has been replaced with a debug break call.
  ASSERT(is_inline_cache_stub() ||
         result == UNINITIALIZED ||
         result == DEBUG_BREAK ||
         result == DEBUG_PREPARE_STEP_IN);
  return result;
}


PropertyType Code::type() {
  ASSERT(state() == MONOMORPHIC);
  return ExtractTypeFromFlags(flags());
}


int Code::arguments_count() {
  ASSERT(is_call_stub() || kind() == STUB);
  return ExtractArgumentsCountFromFlags(flags());
}


CodeStub::Major Code::major_key() {
  // TODO(1238541): Simplify this somewhat complicated encoding.
  ASSERT(kind() == STUB);
  int low = ExtractStateFromFlags(flags());
  int high = ExtractTypeFromFlags(flags());
  return static_cast<CodeStub::Major>(high << 3 | low);
}


bool Code::is_inline_cache_stub() {
  Kind kind = this->kind();
  return kind >= FIRST_IC_KIND && kind <= LAST_IC_KIND;
}


Code::Flags Code::ComputeFlags(Kind kind,
                               InlineCacheState state,
                               PropertyType type,
                               int argc) {
  // Compute the bit mask.
  int bits = kind << kFlagsKindShift;
  bits |= state << kFlagsStateShift;
  bits |= type << kFlagsTypeShift;
  bits |= argc << kFlagsArgumentsCountShift;
  // Cast to flags and validate result before returning it.
  Flags result = static_cast<Flags>(bits);
  ASSERT(ExtractKindFromFlags(result) == kind);
  ASSERT(ExtractStateFromFlags(result) == state);
  ASSERT(ExtractTypeFromFlags(result) == type);
  ASSERT(ExtractArgumentsCountFromFlags(result) == argc);
  return result;
}


Code::Flags Code::ComputeMonomorphicFlags(Kind kind,
                                          PropertyType type,
                                          int argc) {
  return ComputeFlags(kind, MONOMORPHIC, type, argc);
}


Code::Kind Code::ExtractKindFromFlags(Flags flags) {
  int bits = (flags & kFlagsKindMask) >> kFlagsKindShift;
  return static_cast<Kind>(bits);
}


InlineCacheState Code::ExtractStateFromFlags(Flags flags) {
  int bits = (flags & kFlagsStateMask) >> kFlagsStateShift;
  return static_cast<InlineCacheState>(bits);
}


PropertyType Code::ExtractTypeFromFlags(Flags flags) {
  int bits = (flags & kFlagsTypeMask) >> kFlagsTypeShift;
  return static_cast<PropertyType>(bits);
}


int Code::ExtractArgumentsCountFromFlags(Flags flags) {
  return (flags & kFlagsArgumentsCountMask) >> kFlagsArgumentsCountShift;
}


Code::Flags Code::RemoveTypeFromFlags(Flags flags) {
  int bits = flags & ~kFlagsTypeMask;
  return static_cast<Flags>(bits);
}


Object* Map::prototype() {
  return READ_FIELD(this, kPrototypeOffset);
}


void Map::set_prototype(Object* value) {
  ASSERT(value->IsNull() || value->IsJSObject());
  WRITE_FIELD(this, kPrototypeOffset, value);
  WRITE_BARRIER(this, kPrototypeOffset);
}


ACCESSORS(Map, instance_descriptors, DescriptorArray,
          kInstanceDescriptorsOffset)
ACCESSORS(Map, code_cache, FixedArray, kCodeCacheOffset)
ACCESSORS(Map, constructor, Object, kConstructorOffset)

ACCESSORS(JSFunction, shared, SharedFunctionInfo, kSharedFunctionInfoOffset)
ACCESSORS(JSFunction, literals, FixedArray, kLiteralsOffset)

ACCESSORS(GlobalObject, builtins, JSBuiltinsObject, kBuiltinsOffset)
ACCESSORS(GlobalObject, global_context, Context, kGlobalContextOffset)

ACCESSORS(JSGlobalObject, security_token, Object, kSecurityTokenOffset)

ACCESSORS(AccessorInfo, getter, Object, kGetterOffset)
ACCESSORS(AccessorInfo, setter, Object, kSetterOffset)
ACCESSORS(AccessorInfo, data, Object, kDataOffset)
ACCESSORS(AccessorInfo, name, Object, kNameOffset)
ACCESSORS(AccessorInfo, flag, Smi, kFlagOffset)

ACCESSORS(AccessCheckInfo, named_callback, Object, kNamedCallbackOffset)
ACCESSORS(AccessCheckInfo, indexed_callback, Object, kIndexedCallbackOffset)
ACCESSORS(AccessCheckInfo, data, Object, kDataOffset)

ACCESSORS(InterceptorInfo, getter, Object, kGetterOffset)
ACCESSORS(InterceptorInfo, setter, Object, kSetterOffset)
ACCESSORS(InterceptorInfo, query, Object, kQueryOffset)
ACCESSORS(InterceptorInfo, deleter, Object, kDeleterOffset)
ACCESSORS(InterceptorInfo, enumerator, Object, kEnumeratorOffset)
ACCESSORS(InterceptorInfo, data, Object, kDataOffset)

ACCESSORS(CallHandlerInfo, callback, Object, kCallbackOffset)
ACCESSORS(CallHandlerInfo, data, Object, kDataOffset)

ACCESSORS(TemplateInfo, tag, Object, kTagOffset)
ACCESSORS(TemplateInfo, property_list, Object, kPropertyListOffset)

ACCESSORS(FunctionTemplateInfo, serial_number, Object, kSerialNumberOffset)
ACCESSORS(FunctionTemplateInfo, call_code, Object, kCallCodeOffset)
ACCESSORS(FunctionTemplateInfo, property_accessors, Object,
          kPropertyAccessorsOffset)
ACCESSORS(FunctionTemplateInfo, prototype_template, Object,
          kPrototypeTemplateOffset)
ACCESSORS(FunctionTemplateInfo, parent_template, Object, kParentTemplateOffset)
ACCESSORS(FunctionTemplateInfo, named_property_handler, Object,
          kNamedPropertyHandlerOffset)
ACCESSORS(FunctionTemplateInfo, indexed_property_handler, Object,
          kIndexedPropertyHandlerOffset)
ACCESSORS(FunctionTemplateInfo, instance_template, Object,
          kInstanceTemplateOffset)
ACCESSORS(FunctionTemplateInfo, class_name, Object, kClassNameOffset)
ACCESSORS(FunctionTemplateInfo, signature, Object, kSignatureOffset)
ACCESSORS(FunctionTemplateInfo, lookup_callback, Object, kLookupCallbackOffset)
ACCESSORS(FunctionTemplateInfo, instance_call_handler, Object,
          kInstanceCallHandlerOffset)
ACCESSORS(FunctionTemplateInfo, access_check_info, Object,
          kAccessCheckInfoOffset)
ACCESSORS(FunctionTemplateInfo, flag, Smi, kFlagOffset)

ACCESSORS(ObjectTemplateInfo, constructor, Object, kConstructorOffset)
ACCESSORS(ObjectTemplateInfo, internal_field_count, Object,
          kInternalFieldCountOffset)

ACCESSORS(SignatureInfo, receiver, Object, kReceiverOffset)
ACCESSORS(SignatureInfo, args, Object, kArgsOffset)

ACCESSORS(TypeSwitchInfo, types, Object, kTypesOffset)

ACCESSORS(Script, source, Object, kSourceOffset)
ACCESSORS(Script, name, Object, kNameOffset)
ACCESSORS(Script, line_offset, Smi, kLineOffsetOffset)
ACCESSORS(Script, column_offset, Smi, kColumnOffsetOffset)
ACCESSORS(Script, wrapper, Proxy, kWrapperOffset)
ACCESSORS(Script, type, Smi, kTypeOffset)

ACCESSORS(DebugInfo, shared, SharedFunctionInfo, kSharedFunctionInfoIndex)
ACCESSORS(DebugInfo, original_code, Code, kOriginalCodeIndex)
ACCESSORS(DebugInfo, code, Code, kPatchedCodeIndex)
ACCESSORS(DebugInfo, break_points, FixedArray, kBreakPointsStateIndex)

ACCESSORS(BreakPointInfo, code_position, Smi, kCodePositionIndex)
ACCESSORS(BreakPointInfo, source_position, Smi, kSourcePositionIndex)
ACCESSORS(BreakPointInfo, statement_position, Smi, kStatementPositionIndex)
ACCESSORS(BreakPointInfo, break_point_objects, Object, kBreakPointObjectsIndex)

ACCESSORS(SharedFunctionInfo, name, Object, kNameOffset)
ACCESSORS(SharedFunctionInfo, instance_class_name, Object,
          kInstanceClassNameOffset)
ACCESSORS(SharedFunctionInfo, function_data, Object,
          kExternalReferenceDataOffset)
ACCESSORS(SharedFunctionInfo, lazy_load_data, Object, kLazyLoadDataOffset)
ACCESSORS(SharedFunctionInfo, script, Object, kScriptOffset)
ACCESSORS(SharedFunctionInfo, debug_info, Object, kDebugInfoOffset)

BOOL_ACCESSORS(FunctionTemplateInfo, flag, hidden_prototype,
               kHiddenPrototypeBit)
BOOL_ACCESSORS(FunctionTemplateInfo, flag, undetectable, kUndetectableBit)
BOOL_ACCESSORS(FunctionTemplateInfo, flag, needs_access_check,
               kNeedsAccessCheckBit)
BOOL_ACCESSORS(SharedFunctionInfo, start_position_and_type, is_expression,
               kIsExpressionBit)
BOOL_ACCESSORS(SharedFunctionInfo, start_position_and_type, is_toplevel,
               kIsTopLevelBit)

INT_ACCESSORS(SharedFunctionInfo, length, kLengthOffset)
INT_ACCESSORS(SharedFunctionInfo, formal_parameter_count,
              kFormalParameterCountOffset)
INT_ACCESSORS(SharedFunctionInfo, expected_nof_properties,
              kExpectedNofPropertiesOffset)
INT_ACCESSORS(SharedFunctionInfo, start_position_and_type,
              kStartPositionAndTypeOffset)
INT_ACCESSORS(SharedFunctionInfo, end_position, kEndPositionOffset)
INT_ACCESSORS(SharedFunctionInfo, function_token_position,
              kFunctionTokenPositionOffset)


int SharedFunctionInfo::start_position() {
  return start_position_and_type() >> kStartPositionShift;
}


void SharedFunctionInfo::set_start_position(int start_position) {
  set_start_position_and_type((start_position << kStartPositionShift)
    | (start_position_and_type() & ~kStartPositionMask));
}


Code* SharedFunctionInfo::code() {
  return Code::cast(READ_FIELD(this, kCodeOffset));
}


void SharedFunctionInfo::set_code(Code* value) {
  WRITE_FIELD(this, kCodeOffset, value);
  WRITE_BARRIER(this, kCodeOffset);
}


bool SharedFunctionInfo::is_compiled() {
  // TODO(1242782): Create a code kind for uncompiled code.
  return code()->kind() != Code::STUB;
}


bool JSFunction::IsBoilerplate() {
  return map() == Heap::boilerplate_function_map();
}


bool JSFunction::IsLoaded() {
  return shared()->lazy_load_data() == Heap::undefined_value();
}


Code* JSFunction::code() {
  return shared()->code();
}


void JSFunction::set_code(Code* value) {
  shared()->set_code(value);
}


Context* JSFunction::context() {
  return Context::cast(READ_FIELD(this, kContextOffset));
}


Object* JSFunction::unchecked_context() {
  return READ_FIELD(this, kContextOffset);
}


void JSFunction::set_context(Object* value) {
  ASSERT(value == Heap::undefined_value() || value->IsContext());
  WRITE_FIELD(this, kContextOffset, value);
  WRITE_BARRIER(this, kContextOffset);
}

ACCESSORS(JSFunction, prototype_or_initial_map, Object,
          kPrototypeOrInitialMapOffset)


Map* JSFunction::initial_map() {
  return Map::cast(prototype_or_initial_map());
}


void JSFunction::set_initial_map(Map* value) {
  set_prototype_or_initial_map(value);
}


bool JSFunction::has_initial_map() {
  return prototype_or_initial_map()->IsMap();
}


bool JSFunction::has_instance_prototype() {
  return has_initial_map() || !prototype_or_initial_map()->IsTheHole();
}


bool JSFunction::has_prototype() {
  return map()->has_non_instance_prototype() || has_instance_prototype();
}


Object* JSFunction::instance_prototype() {
  ASSERT(has_instance_prototype());
  if (has_initial_map()) return initial_map()->prototype();
  // When there is no initial map and the prototype is a JSObject, the
  // initial map field is used for the prototype field.
  return prototype_or_initial_map();
}


Object* JSFunction::prototype() {
  ASSERT(has_prototype());
  // If the function's prototype property has been set to a non-JSObject
  // value, that value is stored in the constructor field of the map.
  if (map()->has_non_instance_prototype()) return map()->constructor();
  return instance_prototype();
}


bool JSFunction::is_compiled() {
  return shared()->is_compiled();
}


Object* JSBuiltinsObject::javascript_builtin(Builtins::JavaScript id) {
  ASSERT(0 <= id && id < kJSBuiltinsCount);
  return READ_FIELD(this, kJSBuiltinsOffset + (id * kPointerSize));
}


void JSBuiltinsObject::set_javascript_builtin(Builtins::JavaScript id,
                                              Object* value) {
  ASSERT(0 <= id && id < kJSBuiltinsCount);
  WRITE_FIELD(this, kJSBuiltinsOffset + (id * kPointerSize), value);
  WRITE_BARRIER(this, kJSBuiltinsOffset + (id * kPointerSize));
}


Address Proxy::proxy() {
  return AddressFrom<Address>(READ_INT_FIELD(this, kProxyOffset));
}


void Proxy::set_proxy(Address value) {
  WRITE_INT_FIELD(this, kProxyOffset, OffsetFrom(value));
}


void Proxy::ProxyIterateBody(ObjectVisitor* visitor) {
  visitor->VisitExternalReference(
      reinterpret_cast<Address *>(FIELD_ADDR(this, kProxyOffset)));
}


ACCESSORS(JSValue, value, Object, kValueOffset)


JSValue* JSValue::cast(Object* obj) {
  ASSERT(obj->IsJSValue());
  ASSERT(HeapObject::cast(obj)->Size() == JSValue::kSize);
  return reinterpret_cast<JSValue*>(obj);
}


INT_ACCESSORS(Code, instruction_size, kInstructionSizeOffset)
INT_ACCESSORS(Code, relocation_size, kRelocationSizeOffset)
INT_ACCESSORS(Code, sinfo_size, kSInfoSizeOffset)


Code::ICTargetState Code::ic_flag() {
  return static_cast<ICTargetState>(READ_INT_FIELD(this, kICFlagOffset));
}


void Code::set_ic_flag(ICTargetState value) {
  WRITE_INT_FIELD(this, kICFlagOffset, value);
}


byte* Code::instruction_start()  {
  return FIELD_ADDR(this, kHeaderSize);
}


int Code::body_size() {
  return RoundUp(instruction_size() + relocation_size(), kObjectAlignment);
}


byte* Code::relocation_start() {
  return FIELD_ADDR(this, CodeSize() - sinfo_size() - relocation_size());
}


byte* Code::entry() {
  return instruction_start();
}


bool Code::contains(byte* pc) {
  return (instruction_start() <= pc) &&
      (pc < instruction_start() + instruction_size());
}


byte* Code::sinfo_start() {
  return FIELD_ADDR(this, CodeSize() - sinfo_size());
}


ACCESSORS(JSArray, length, Object, kLengthOffset)


bool JSObject::HasFastElements() {
  return !elements()->IsDictionary();
}


bool JSObject::HasNamedInterceptor() {
  return map()->has_named_interceptor();
}


bool JSObject::HasIndexedInterceptor() {
  return map()->has_indexed_interceptor();
}


Dictionary* JSObject::property_dictionary() {
  ASSERT(!HasFastProperties());
  return Dictionary::cast(properties());
}


Dictionary* JSObject::element_dictionary() {
  ASSERT(!HasFastElements());
  return Dictionary::cast(elements());
}


bool String::HasHashCode() {
  return (length_field() & kHashComputedMask) != 0;
}


uint32_t String::Hash() {
  // Fast case: has hash code already been computed?
  int hash = length_field();
  if (hash & kHashComputedMask) return hash;
  // Slow case: compute hash code and set it..
  return ComputeAndSetHash();
}


bool String::AsArrayIndex(uint32_t* index) {
  int hash = length_field();
  if ((hash & kHashComputedMask) && !(hash & kIsArrayIndexMask)) return false;
  return SlowAsArrayIndex(index);
}


Object* JSObject::GetPrototype() {
  return JSObject::cast(this)->map()->prototype();
}


PropertyAttributes JSObject::GetPropertyAttribute(String* key) {
  return GetPropertyAttributeWithReceiver(this, key);
}


bool JSObject::HasElement(uint32_t index) {
  return HasElementWithReceiver(this, index);
}


bool AccessorInfo::all_can_read() {
  return BooleanBit::get(flag(), kAllCanReadBit);
}


void AccessorInfo::set_all_can_read(bool value) {
  set_flag(BooleanBit::set(flag(), kAllCanReadBit, value));
}


bool AccessorInfo::all_can_write() {
  return BooleanBit::get(flag(), kAllCanWriteBit);
}


void AccessorInfo::set_all_can_write(bool value) {
  set_flag(BooleanBit::set(flag(), kAllCanWriteBit, value));
}


PropertyAttributes AccessorInfo::property_attributes() {
  return AttributesField::decode(static_cast<uint32_t>(flag()->value()));
}


void AccessorInfo::set_property_attributes(PropertyAttributes attributes) {
  ASSERT(AttributesField::is_valid(attributes));
  int rest_value = flag()->value() & ~AttributesField::mask();
  set_flag(Smi::FromInt(rest_value | AttributesField::encode(attributes)));
}

void Dictionary::SetEntry(int entry,
                          Object* key,
                          Object* value,
                          PropertyDetails details) {
  ASSERT(!key->IsString() || details.index() > 0);
  int index = EntryToIndex(entry);
  WriteBarrierMode mode = GetWriteBarrierMode();
  set(index, key, mode);
  set(index+1, value, mode);
  fast_set(this, index+2, details.AsSmi());
}


void Map::ClearCodeCache() {
  // No write barrier is needed since empty_fixed_array is not in new space.
  // Please note this function is used during marking:
  //  - MarkCompactCollector::MarkUnmarkedObject
  ASSERT(!Heap::InNewSpace(Heap::empty_fixed_array()));
  WRITE_FIELD(this, kCodeCacheOffset, Heap::empty_fixed_array());
}


#undef CAST_ACCESSOR
#undef INT_ACCESSORS
#undef SMI_ACCESSORS
#undef ACCESSORS
#undef FIELD_ADDR
#undef READ_FIELD
#undef WRITE_FIELD
#undef WRITE_BARRIER
#undef READ_MEMADDR_FIELD
#undef WRITE_MEMADDR_FIELD
#undef READ_DOUBLE_FIELD
#undef WRITE_DOUBLE_FIELD
#undef READ_INT_FIELD
#undef WRITE_INT_FIELD
#undef READ_SHORT_FIELD
#undef WRITE_SHORT_FIELD
#undef READ_BYTE_FIELD
#undef WRITE_BYTE_FIELD


} }  // namespace v8::internal

#endif  // V8_OBJECTS_INL_H_
