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

#ifndef V8_OBJECTS_H_
#define V8_OBJECTS_H_

#include "builtins.h"
#include "code-stubs.h"
#include "smart-pointer.h"
#include "unicode-inl.h"

//
// All object types in the V8 JavaScript are described in this file.
//
// Inheritance hierarchy:
//   - Object
//     - Smi          (immediate small integer)
//     - Failure      (immediate for marking failed operation)
//     - HeapObject   (superclass for everything allocated in the heap)
//       - JSObject
//         - JSArray
//         - JSFunction
//         - GlobalObject
//           - JSGlobalObject
//           - JSBuiltinsObject
//         - JSValue
//         - Script
//       - Array
//         - ByteArray
//         - FixedArray
//           - HashTable
//             - Dictionary
//             - SymbolTable
//           - Context
//           - GlobalContext
//       - String
//         - SeqString
//           - AsciiString
//           - TwoByteString
//         - ConsString
//         - SlicedString
//         - ExternalString
//           - ExternalAsciiString
//           - ExternalTwoByteString
//       - HeapNumber
//       - Code
//       - Map
//       - Oddball
//       - Proxy
//       - SharedFunctionInfo
//       - Struct
//         - AccessorInfo
//         - AccessCheckInfo
//         - InterceptorInfo
//         - CallHandlerInfo
//         - FunctionTemplateInfo
//         - ObjectTemplateInfo
//         - SignatureInfo
//         - TypeSwitchInfo
//         - DebugInfo
//         - BreakPointInfo
//
// Formats of Object*:
//  Smi:        [31 bit signed int] 0
//  HeapObject: [32 bit direct pointer] (4 byte aligned) | 01
//  Failure:    [30 bit signed int] 11


// Ecma-262 3rd 8.6.1
enum PropertyAttributes {
  NONE              = v8::None,
  READ_ONLY         = v8::ReadOnly,
  DONT_ENUM         = v8::DontEnum,
  DONT_DELETE       = v8::DontDelete,
  INTERCEPTED       = 1 << 3,
  ABSENT            = 16  // Used in runtime to indicate a property is absent.
};

namespace v8 { namespace internal {


// PropertyDetails captures type and attributes for a property.
// They are used both in property dictionaries and instance descriptors.
class PropertyDetails BASE_EMBEDDED {
 public:

  PropertyDetails(PropertyAttributes attributes,
                  PropertyType type,
                  int index = 0) {
    ASSERT(TypeField::is_valid(type));
    ASSERT(AttributesField::is_valid(attributes));
    ASSERT(IndexField::is_valid(index));

    value_ = TypeField::encode(type)
        | AttributesField::encode(attributes)
        | IndexField::encode(index);

    ASSERT(type == this->type());
    ASSERT(attributes == this->attributes());
    ASSERT(index == this->index());
  }

  // Conversion for storing details as Object*.
  inline PropertyDetails(Smi* smi);
  inline Smi* AsSmi();

  PropertyType type() { return TypeField::decode(value_); }

  bool IsTransition() {
    PropertyType t = type();
    ASSERT(t != INTERCEPTOR);
    if (t == MAP_TRANSITION || t == CONSTANT_TRANSITION) return true;
    return false;
  }

  PropertyAttributes attributes() { return AttributesField::decode(value_); }

  int index() { return IndexField::decode(value_); }

  static bool IsValidIndex(int index) { return IndexField::is_valid(index); }

  bool IsReadOnly() { return (attributes() & READ_ONLY) != 0; }
  bool IsDontDelete() { return (attributes() & DONT_DELETE) != 0; }
  bool IsDontEnum() { return (attributes() & DONT_ENUM) != 0; }

  // Bit fields in value_ (type, shift, size). Must be public so the
  // constants can be embedded in generated code.
  class TypeField:       public BitField<PropertyType,       0, 3> {};
  class AttributesField: public BitField<PropertyAttributes, 3, 3> {};
  class IndexField:      public BitField<uint32_t,           6, 32-6> {};

  static const int kInitialIndex = 1;

 private:
  uint32_t value_;
};

// All Maps have a field instance_type containing a InstanceType.
// It describes the type of the instances.
//
// As an example, a JavaScript object is a heap object and its map
// instance_type is JS_OBJECT_TYPE.
//
// The names of the string instance types are intended to systematically
// mirror their encoding in the instance_type field of the map.  The length
// (SHORT, MEDIUM, or LONG) is always mentioned.  The default encoding is
// considered TWO_BYTE.  It is not mentioned in the name.  ASCII encoding is
// mentioned explicitly in the name.  Likewise, the default representation is
// considered sequential.  It is not mentioned in the name.  The other
// representations (eg, CONS, SLICED, EXTERNAL) are explicitly mentioned.
// Finally, the string is either a SYMBOL_TYPE (if it is a symbol) or a
// STRING_TYPE (if it is not a symbol).
//
// NOTE: The following things are some that depend on the string types having
// instance_types that are less than those of all other types:
// HeapObject::Size, HeapObject::IterateBody, the typeof operator, and
// Object::IsString.
//
// NOTE: Everything following JS_OBJECT_TYPE is considered a
// JSObject for GC purposes. The first four entries here have typeof
// 'object', whereas JS_FUNCTION_TYPE has typeof 'function'.
#define INSTANCE_TYPE_LIST(V)                   \
  V(SHORT_SYMBOL_TYPE)                          \
  V(MEDIUM_SYMBOL_TYPE)                         \
  V(LONG_SYMBOL_TYPE)                           \
  V(SHORT_ASCII_SYMBOL_TYPE)                    \
  V(MEDIUM_ASCII_SYMBOL_TYPE)                   \
  V(LONG_ASCII_SYMBOL_TYPE)                     \
  V(SHORT_CONS_SYMBOL_TYPE)                     \
  V(MEDIUM_CONS_SYMBOL_TYPE)                    \
  V(LONG_CONS_SYMBOL_TYPE)                      \
  V(SHORT_CONS_ASCII_SYMBOL_TYPE)               \
  V(MEDIUM_CONS_ASCII_SYMBOL_TYPE)              \
  V(LONG_CONS_ASCII_SYMBOL_TYPE)                \
  V(SHORT_SLICED_SYMBOL_TYPE)                   \
  V(MEDIUM_SLICED_SYMBOL_TYPE)                  \
  V(LONG_SLICED_SYMBOL_TYPE)                    \
  V(SHORT_SLICED_ASCII_SYMBOL_TYPE)             \
  V(MEDIUM_SLICED_ASCII_SYMBOL_TYPE)            \
  V(LONG_SLICED_ASCII_SYMBOL_TYPE)              \
  V(SHORT_EXTERNAL_SYMBOL_TYPE)                 \
  V(MEDIUM_EXTERNAL_SYMBOL_TYPE)                \
  V(LONG_EXTERNAL_SYMBOL_TYPE)                  \
  V(SHORT_EXTERNAL_ASCII_SYMBOL_TYPE)           \
  V(MEDIUM_EXTERNAL_ASCII_SYMBOL_TYPE)          \
  V(LONG_EXTERNAL_ASCII_SYMBOL_TYPE)            \
  V(SHORT_STRING_TYPE)                          \
  V(MEDIUM_STRING_TYPE)                         \
  V(LONG_STRING_TYPE)                           \
  V(SHORT_ASCII_STRING_TYPE)                    \
  V(MEDIUM_ASCII_STRING_TYPE)                   \
  V(LONG_ASCII_STRING_TYPE)                     \
  V(SHORT_CONS_STRING_TYPE)                     \
  V(MEDIUM_CONS_STRING_TYPE)                    \
  V(LONG_CONS_STRING_TYPE)                      \
  V(SHORT_CONS_ASCII_STRING_TYPE)               \
  V(MEDIUM_CONS_ASCII_STRING_TYPE)              \
  V(LONG_CONS_ASCII_STRING_TYPE)                \
  V(SHORT_SLICED_STRING_TYPE)                   \
  V(MEDIUM_SLICED_STRING_TYPE)                  \
  V(LONG_SLICED_STRING_TYPE)                    \
  V(SHORT_SLICED_ASCII_STRING_TYPE)             \
  V(MEDIUM_SLICED_ASCII_STRING_TYPE)            \
  V(LONG_SLICED_ASCII_STRING_TYPE)              \
  V(SHORT_EXTERNAL_STRING_TYPE)                 \
  V(MEDIUM_EXTERNAL_STRING_TYPE)                \
  V(LONG_EXTERNAL_STRING_TYPE)                  \
  V(SHORT_EXTERNAL_ASCII_STRING_TYPE)           \
  V(MEDIUM_EXTERNAL_ASCII_STRING_TYPE)          \
  V(LONG_EXTERNAL_ASCII_STRING_TYPE)            \
  V(LONG_PRIVATE_EXTERNAL_ASCII_STRING_TYPE)    \
                                                \
  V(MAP_TYPE)                                   \
  V(HEAP_NUMBER_TYPE)                           \
  V(FIXED_ARRAY_TYPE)                           \
  V(CODE_TYPE)                                  \
  V(ODDBALL_TYPE)                               \
  V(PROXY_TYPE)                                 \
  V(BYTE_ARRAY_TYPE)                            \
  V(FILLER_TYPE)                                \
                                                \
  V(ACCESSOR_INFO_TYPE)                         \
  V(ACCESS_CHECK_INFO_TYPE)                     \
  V(INTERCEPTOR_INFO_TYPE)                      \
  V(SHARED_FUNCTION_INFO_TYPE)                  \
  V(CALL_HANDLER_INFO_TYPE)                     \
  V(FUNCTION_TEMPLATE_INFO_TYPE)                \
  V(OBJECT_TEMPLATE_INFO_TYPE)                  \
  V(SIGNATURE_INFO_TYPE)                        \
  V(TYPE_SWITCH_INFO_TYPE)                      \
  V(DEBUG_INFO_TYPE)                            \
  V(BREAK_POINT_INFO_TYPE)                      \
  V(SCRIPT_TYPE)                                \
                                                \
  V(JS_OBJECT_TYPE)                             \
  V(JS_GLOBAL_OBJECT_TYPE)                      \
  V(JS_BUILTINS_OBJECT_TYPE)                    \
  V(JS_VALUE_TYPE)                              \
  V(JS_ARRAY_TYPE)                              \
                                                \
  V(JS_FUNCTION_TYPE)                           \


// Since string types are not consecutive, this macro is used to
// iterate over them.
#define STRING_TYPE_LIST(V)                                                    \
  V(SHORT_SYMBOL_TYPE, TwoByteString::kHeaderSize, short_symbol)               \
  V(MEDIUM_SYMBOL_TYPE, TwoByteString::kHeaderSize, medium_symbol)             \
  V(LONG_SYMBOL_TYPE, TwoByteString::kHeaderSize, long_symbol)                 \
  V(SHORT_ASCII_SYMBOL_TYPE, AsciiString::kHeaderSize, short_ascii_symbol)     \
  V(MEDIUM_ASCII_SYMBOL_TYPE, AsciiString::kHeaderSize, medium_ascii_symbol)   \
  V(LONG_ASCII_SYMBOL_TYPE, AsciiString::kHeaderSize, long_ascii_symbol)       \
  V(SHORT_CONS_SYMBOL_TYPE, ConsString::kSize, short_cons_symbol)              \
  V(MEDIUM_CONS_SYMBOL_TYPE, ConsString::kSize, medium_cons_symbol)            \
  V(LONG_CONS_SYMBOL_TYPE, ConsString::kSize, long_cons_symbol)                \
  V(SHORT_CONS_ASCII_SYMBOL_TYPE, ConsString::kSize, short_cons_ascii_symbol)  \
  V(MEDIUM_CONS_ASCII_SYMBOL_TYPE, ConsString::kSize, medium_cons_ascii_symbol)\
  V(LONG_CONS_ASCII_SYMBOL_TYPE, ConsString::kSize, long_cons_ascii_symbol)    \
  V(SHORT_SLICED_SYMBOL_TYPE, SlicedString::kSize, short_sliced_symbol)        \
  V(MEDIUM_SLICED_SYMBOL_TYPE, SlicedString::kSize, medium_sliced_symbol)      \
  V(LONG_SLICED_SYMBOL_TYPE, SlicedString::kSize, long_sliced_symbol)          \
  V(SHORT_SLICED_ASCII_SYMBOL_TYPE,                                            \
    SlicedString::kSize,                                                       \
    short_sliced_ascii_symbol)                                                 \
  V(MEDIUM_SLICED_ASCII_SYMBOL_TYPE,                                           \
    SlicedString::kSize,                                                       \
    medium_sliced_ascii_symbol)                                                \
  V(LONG_SLICED_ASCII_SYMBOL_TYPE,                                             \
    SlicedString::kSize,                                                       \
    long_sliced_ascii_symbol)                                                  \
  V(SHORT_EXTERNAL_SYMBOL_TYPE,                                                \
    ExternalTwoByteString::kSize,                                              \
    short_external_symbol)                                                     \
  V(MEDIUM_EXTERNAL_SYMBOL_TYPE,                                               \
    ExternalTwoByteString::kSize,                                              \
    medium_external_symbol)                                                    \
  V(LONG_EXTERNAL_SYMBOL_TYPE,                                                 \
    ExternalTwoByteString::kSize,                                              \
    long_external_symbol)                                                      \
  V(SHORT_EXTERNAL_ASCII_SYMBOL_TYPE,                                          \
    ExternalAsciiString::kSize,                                                \
    short_external_ascii_symbol)                                               \
  V(MEDIUM_EXTERNAL_ASCII_SYMBOL_TYPE,                                         \
    ExternalAsciiString::kSize,                                                \
    medium_external_ascii_symbol)                                              \
  V(LONG_EXTERNAL_ASCII_SYMBOL_TYPE,                                           \
    ExternalAsciiString::kSize,                                                \
    long_external_ascii_symbol)                                                \
  V(SHORT_STRING_TYPE, TwoByteString::kHeaderSize, short_string)               \
  V(MEDIUM_STRING_TYPE, TwoByteString::kHeaderSize, medium_string)             \
  V(LONG_STRING_TYPE, TwoByteString::kHeaderSize, long_string)                 \
  V(SHORT_ASCII_STRING_TYPE, AsciiString::kHeaderSize, short_ascii_string)     \
  V(MEDIUM_ASCII_STRING_TYPE, AsciiString::kHeaderSize, medium_ascii_string)   \
  V(LONG_ASCII_STRING_TYPE, AsciiString::kHeaderSize, long_ascii_string)       \
  V(SHORT_CONS_STRING_TYPE, ConsString::kSize, short_cons_string)              \
  V(MEDIUM_CONS_STRING_TYPE, ConsString::kSize, medium_cons_string)            \
  V(LONG_CONS_STRING_TYPE, ConsString::kSize, long_cons_string)                \
  V(SHORT_CONS_ASCII_STRING_TYPE, ConsString::kSize, short_cons_ascii_string)  \
  V(MEDIUM_CONS_ASCII_STRING_TYPE, ConsString::kSize, medium_cons_ascii_string)\
  V(LONG_CONS_ASCII_STRING_TYPE, ConsString::kSize, long_cons_ascii_string)    \
  V(SHORT_SLICED_STRING_TYPE, SlicedString::kSize, short_sliced_string)        \
  V(MEDIUM_SLICED_STRING_TYPE, SlicedString::kSize, medium_sliced_string)      \
  V(LONG_SLICED_STRING_TYPE, SlicedString::kSize, long_sliced_string)          \
  V(SHORT_SLICED_ASCII_STRING_TYPE,                                            \
    SlicedString::kSize,                                                       \
    short_sliced_ascii_string)                                                 \
  V(MEDIUM_SLICED_ASCII_STRING_TYPE,                                           \
    SlicedString::kSize,                                                       \
    medium_sliced_ascii_string)                                                \
  V(LONG_SLICED_ASCII_STRING_TYPE,                                             \
    SlicedString::kSize,                                                       \
    long_sliced_ascii_string)                                                  \
  V(SHORT_EXTERNAL_STRING_TYPE,                                                \
    ExternalTwoByteString::kSize,                                              \
    short_external_string)                                                     \
  V(MEDIUM_EXTERNAL_STRING_TYPE,                                               \
    ExternalTwoByteString::kSize,                                              \
    medium_external_string)                                                    \
  V(LONG_EXTERNAL_STRING_TYPE,                                                 \
    ExternalTwoByteString::kSize,                                              \
    long_external_string)                                                      \
  V(SHORT_EXTERNAL_ASCII_STRING_TYPE,                                          \
    ExternalAsciiString::kSize,                                                \
    short_external_ascii_string)                                               \
  V(MEDIUM_EXTERNAL_ASCII_STRING_TYPE,                                         \
    ExternalAsciiString::kSize,                                                \
    medium_external_ascii_string)                                              \
  V(LONG_EXTERNAL_ASCII_STRING_TYPE,                                           \
    ExternalAsciiString::kSize,                                                \
    long_external_ascii_string)

// A struct is a simple object a set of object-valued fields.  Including an
// object type in this causes the compiler to generate most of the boilerplate
// code for the class including allocation and garbage collection routines,
// casts and predicates.  All you need to define is the class, methods and
// object verification routines.  Easy, no?
//
// Note that for subtle reasons related to the ordering or numerical values of
// type tags, elements in this list have to be added to the INSTANCE_TYPE_LIST
// manually.
#define STRUCT_LIST(V)                                                    \
  V(ACCESSOR_INFO, AccessorInfo, accessor_info)                           \
  V(ACCESS_CHECK_INFO, AccessCheckInfo, access_check_info)                \
  V(INTERCEPTOR_INFO, InterceptorInfo, interceptor_info)                  \
  V(CALL_HANDLER_INFO, CallHandlerInfo, call_handler_info)                \
  V(FUNCTION_TEMPLATE_INFO, FunctionTemplateInfo, function_template_info) \
  V(OBJECT_TEMPLATE_INFO, ObjectTemplateInfo, object_template_info)       \
  V(SIGNATURE_INFO, SignatureInfo, signature_info)                        \
  V(TYPE_SWITCH_INFO, TypeSwitchInfo, type_switch_info)                   \
  V(DEBUG_INFO, DebugInfo, debug_info)                                    \
  V(BREAK_POINT_INFO, BreakPointInfo, break_point_info)                   \
  V(SCRIPT, Script, script)


// We use the full 8 bits of the instance_type field to encode heap object
// instance types.  The high-order bit (bit 7) is set if the object is not a
// string, and cleared if it is a string.
const uint32_t kIsNotStringMask = 0x80;
const uint32_t kStringTag = 0x0;
const uint32_t kNotStringTag = 0x80;

// If bit 7 is clear, bits 5 and 6 are the string's size (short, medium, or
// long).
const uint32_t kStringSizeMask = 0x60;
const uint32_t kShortStringTag = 0x0;
const uint32_t kMediumStringTag = 0x20;
const uint32_t kLongStringTag = 0x40;

// If bit 7 is clear, bit 4 indicates that the string is a symbol (if set) or
// not (if cleared).
const uint32_t kIsSymbolMask = 0x10;
const uint32_t kNotSymbolTag = 0x0;
const uint32_t kSymbolTag = 0x10;

// If bit 7 is clear, and the string representation is a sequential string,
// then bit 3 indicates whether the string consists of two-byte characters or
// one-byte characters.
const uint32_t kStringEncodingMask = 0x8;
const uint32_t kTwoByteStringTag = 0x0;
const uint32_t kAsciiStringTag = 0x8;

// If bit 7 is clear, the low-order 3 bits indicate the representation
// of the string.
const uint32_t kStringRepresentationMask = 0x07;
enum StringRepresentationTag {
  kSeqStringTag = 0x0,
  kConsStringTag = 0x1,
  kSlicedStringTag = 0x2,
  kExternalStringTag = 0x3
};

enum InstanceType {
  SHORT_SYMBOL_TYPE = kShortStringTag | kSymbolTag | kSeqStringTag,
  MEDIUM_SYMBOL_TYPE = kMediumStringTag | kSymbolTag | kSeqStringTag,
  LONG_SYMBOL_TYPE = kLongStringTag | kSymbolTag | kSeqStringTag,
  SHORT_ASCII_SYMBOL_TYPE =
      kShortStringTag | kAsciiStringTag | kSymbolTag | kSeqStringTag,
  MEDIUM_ASCII_SYMBOL_TYPE =
      kMediumStringTag | kAsciiStringTag | kSymbolTag | kSeqStringTag,
  LONG_ASCII_SYMBOL_TYPE =
      kLongStringTag | kAsciiStringTag | kSymbolTag | kSeqStringTag,
  SHORT_CONS_SYMBOL_TYPE = kShortStringTag | kSymbolTag | kConsStringTag,
  MEDIUM_CONS_SYMBOL_TYPE = kMediumStringTag | kSymbolTag | kConsStringTag,
  LONG_CONS_SYMBOL_TYPE = kLongStringTag | kSymbolTag | kConsStringTag,
  SHORT_CONS_ASCII_SYMBOL_TYPE =
      kShortStringTag | kAsciiStringTag | kSymbolTag | kConsStringTag,
  MEDIUM_CONS_ASCII_SYMBOL_TYPE =
      kMediumStringTag | kAsciiStringTag | kSymbolTag | kConsStringTag,
  LONG_CONS_ASCII_SYMBOL_TYPE =
      kLongStringTag | kAsciiStringTag | kSymbolTag | kConsStringTag,
  SHORT_SLICED_SYMBOL_TYPE = kShortStringTag | kSymbolTag | kSlicedStringTag,
  MEDIUM_SLICED_SYMBOL_TYPE = kMediumStringTag | kSymbolTag | kSlicedStringTag,
  LONG_SLICED_SYMBOL_TYPE = kLongStringTag | kSymbolTag | kSlicedStringTag,
  SHORT_SLICED_ASCII_SYMBOL_TYPE =
      kShortStringTag | kAsciiStringTag | kSymbolTag | kSlicedStringTag,
  MEDIUM_SLICED_ASCII_SYMBOL_TYPE =
      kMediumStringTag | kAsciiStringTag | kSymbolTag | kSlicedStringTag,
  LONG_SLICED_ASCII_SYMBOL_TYPE =
      kLongStringTag | kAsciiStringTag | kSymbolTag | kSlicedStringTag,
  SHORT_EXTERNAL_SYMBOL_TYPE =
      kShortStringTag | kSymbolTag | kExternalStringTag,
  MEDIUM_EXTERNAL_SYMBOL_TYPE =
      kMediumStringTag | kSymbolTag | kExternalStringTag,
  LONG_EXTERNAL_SYMBOL_TYPE = kLongStringTag | kSymbolTag | kExternalStringTag,
  SHORT_EXTERNAL_ASCII_SYMBOL_TYPE =
      kShortStringTag | kAsciiStringTag | kSymbolTag | kExternalStringTag,
  MEDIUM_EXTERNAL_ASCII_SYMBOL_TYPE =
      kMediumStringTag | kAsciiStringTag | kSymbolTag | kExternalStringTag,
  LONG_EXTERNAL_ASCII_SYMBOL_TYPE =
      kLongStringTag | kAsciiStringTag | kSymbolTag | kExternalStringTag,
  SHORT_STRING_TYPE = kShortStringTag | kSeqStringTag,
  MEDIUM_STRING_TYPE = kMediumStringTag | kSeqStringTag,
  LONG_STRING_TYPE = kLongStringTag | kSeqStringTag,
  SHORT_ASCII_STRING_TYPE = kShortStringTag | kAsciiStringTag | kSeqStringTag,
  MEDIUM_ASCII_STRING_TYPE = kMediumStringTag | kAsciiStringTag | kSeqStringTag,
  LONG_ASCII_STRING_TYPE = kLongStringTag | kAsciiStringTag | kSeqStringTag,
  SHORT_CONS_STRING_TYPE = kShortStringTag | kConsStringTag,
  MEDIUM_CONS_STRING_TYPE = kMediumStringTag | kConsStringTag,
  LONG_CONS_STRING_TYPE = kLongStringTag | kConsStringTag,
  SHORT_CONS_ASCII_STRING_TYPE =
      kShortStringTag | kAsciiStringTag | kConsStringTag,
  MEDIUM_CONS_ASCII_STRING_TYPE =
      kMediumStringTag | kAsciiStringTag | kConsStringTag,
  LONG_CONS_ASCII_STRING_TYPE =
      kLongStringTag | kAsciiStringTag | kConsStringTag,
  SHORT_SLICED_STRING_TYPE = kShortStringTag | kSlicedStringTag,
  MEDIUM_SLICED_STRING_TYPE = kMediumStringTag | kSlicedStringTag,
  LONG_SLICED_STRING_TYPE = kLongStringTag | kSlicedStringTag,
  SHORT_SLICED_ASCII_STRING_TYPE =
      kShortStringTag | kAsciiStringTag | kSlicedStringTag,
  MEDIUM_SLICED_ASCII_STRING_TYPE =
      kMediumStringTag | kAsciiStringTag | kSlicedStringTag,
  LONG_SLICED_ASCII_STRING_TYPE =
      kLongStringTag | kAsciiStringTag | kSlicedStringTag,
  SHORT_EXTERNAL_STRING_TYPE = kShortStringTag | kExternalStringTag,
  MEDIUM_EXTERNAL_STRING_TYPE = kMediumStringTag | kExternalStringTag,
  LONG_EXTERNAL_STRING_TYPE = kLongStringTag | kExternalStringTag,
  SHORT_EXTERNAL_ASCII_STRING_TYPE =
      kShortStringTag | kAsciiStringTag | kExternalStringTag,
  MEDIUM_EXTERNAL_ASCII_STRING_TYPE =
      kMediumStringTag | kAsciiStringTag | kExternalStringTag,
  LONG_EXTERNAL_ASCII_STRING_TYPE =
      kLongStringTag | kAsciiStringTag | kExternalStringTag,
  LONG_PRIVATE_EXTERNAL_ASCII_STRING_TYPE = LONG_EXTERNAL_ASCII_STRING_TYPE,

  MAP_TYPE = kNotStringTag,
  HEAP_NUMBER_TYPE,
  FIXED_ARRAY_TYPE,
  CODE_TYPE,
  ODDBALL_TYPE,
  PROXY_TYPE,
  BYTE_ARRAY_TYPE,
  FILLER_TYPE,
  SMI_TYPE,

  ACCESSOR_INFO_TYPE,
  ACCESS_CHECK_INFO_TYPE,
  INTERCEPTOR_INFO_TYPE,
  SHARED_FUNCTION_INFO_TYPE,
  CALL_HANDLER_INFO_TYPE,
  FUNCTION_TEMPLATE_INFO_TYPE,
  OBJECT_TEMPLATE_INFO_TYPE,
  SIGNATURE_INFO_TYPE,
  TYPE_SWITCH_INFO_TYPE,
  DEBUG_INFO_TYPE,
  BREAK_POINT_INFO_TYPE,
  SCRIPT_TYPE,

  JS_OBJECT_TYPE,
  JS_GLOBAL_OBJECT_TYPE,
  JS_BUILTINS_OBJECT_TYPE,
  JS_VALUE_TYPE,
  JS_ARRAY_TYPE,

  JS_FUNCTION_TYPE,

  // Pseudo-types
  FIRST_NONSTRING_TYPE = MAP_TYPE,
  FIRST_TYPE = 0x0,
  LAST_TYPE = JS_FUNCTION_TYPE,
  // Boundaries for testing the type is a JavaScript "object".  Note that
  // function objects are not counted as objects, even though they are
  // implemented as such; only values whose typeof is "object" are included.
  FIRST_JS_OBJECT_TYPE = JS_OBJECT_TYPE,
  LAST_JS_OBJECT_TYPE = JS_ARRAY_TYPE
};


enum CompareResult {
  LESS      = -1,
  EQUAL     =  0,
  GREATER   =  1,

  NOT_EQUAL = GREATER
};


#define DECL_BOOLEAN_ACCESSORS(name)   \
  inline bool name();                  \
  inline void set_##name(bool value);  \


#define DECL_ACCESSORS(name, type)  \
  inline type* name();                 \
  inline void set_##name(type* value);


class StringStream;
class ObjectVisitor;

struct ValueInfo : public Malloced {
  ValueInfo() : type(FIRST_TYPE), ptr(NULL), str(NULL), number(0) { }
  InstanceType type;
  Object* ptr;
  const char* str;
  double number;
};


// A template-ized version of the IsXXX functions.
template <class C> static inline bool Is(Object* obj);


// Object is the abstract superclass for all classes in the
// object hierarchy.
// Object does not use any virtual functions to avoid the
// allocation of the C++ vtable.
// Since Smi and Failure are subclasses of Object no
// data members can be present in Object.
class Object BASE_EMBEDDED {
 public:
  // Type testing.
  inline bool IsSmi();
  inline bool IsHeapObject();
  inline bool IsHeapNumber();
  inline bool IsString();
  inline bool IsSeqString();
  inline bool IsAsciiString();
  inline bool IsTwoByteString();
  inline bool IsConsString();
  inline bool IsSlicedString();
  inline bool IsExternalString();
  inline bool IsExternalAsciiString();
  inline bool IsExternalTwoByteString();
  inline bool IsShortString();
  inline bool IsMediumString();
  inline bool IsLongString();
  inline bool IsSymbol();
  inline bool IsNumber();
  inline bool IsByteArray();
  inline bool IsFailure();
  inline bool IsRetryAfterGC();
  inline bool IsException();
  inline bool IsJSObject();
  inline bool IsMap();
  inline bool IsFixedArray();
  inline bool IsDescriptorArray();
  inline bool IsContext();
  inline bool IsGlobalContext();
  inline bool IsJSFunction();
  inline bool IsCode();
  inline bool IsOddball();
  inline bool IsSharedFunctionInfo();
  inline bool IsJSValue();
  inline bool IsProxy();
  inline bool IsBoolean();
  inline bool IsJSArray();
  inline bool IsHashTable();
  inline bool IsDictionary();
  inline bool IsSymbolTable();
  inline bool IsPrimitive();
  inline bool IsGlobalObject();
  inline bool IsJSGlobalObject();
  inline bool IsJSBuiltinsObject();
  inline bool IsUndetectableObject();
  inline bool IsAccessCheckNeeded();

  // Returns true if this object is an instance of the specified
  // function template.
  bool IsInstanceOf(FunctionTemplateInfo* type);

  inline bool IsStruct();
#define DECLARE_STRUCT_PREDICATE(NAME, Name, name) inline bool Is##Name();
  STRUCT_LIST(DECLARE_STRUCT_PREDICATE)
#undef DECLARE_STRUCT_PREDICATE

  // Oddball testing.
  INLINE(bool IsUndefined());
  INLINE(bool IsTheHole());
  INLINE(bool IsNull());
  INLINE(bool IsTrue());
  INLINE(bool IsFalse());

  // Extract the number.
  inline double Number();

  Object* ToObject();             // ECMA-262 9.9.
  Object* ToBoolean();            // ECMA-262 9.2.

  // Convert to a JSObject if needed.
  // global_context is used when creating wrapper object.
  Object* ToObject(Context* global_context);

  // Converts this to a Smi if possible.
  // Failure is returned otherwise.
  inline Object* ToSmi();

  void Lookup(String* name, LookupResult* result);

  // Property access.
  inline Object* GetProperty(String* key);
  inline Object* GetProperty(String* key, PropertyAttributes* attributes);
  Object* GetPropertyWithReceiver(Object* receiver,
                                  String* key,
                                  PropertyAttributes* attributes);
  Object* GetProperty(Object* receiver,
                      LookupResult* result,
                      String* key,
                      PropertyAttributes* attributes);
  Object* GetPropertyWithCallback(Object* receiver,
                                  Object* structure,
                                  String* name,
                                  Object* holder);

  inline Object* GetElement(uint32_t index);
  Object* GetElementWithReceiver(Object* receiver, uint32_t index);

  // Return the object's prototype (might be Heap::null_value()).
  Object* GetPrototype();

  // Returns true if this is a JSValue containing a string and the index is
  // < the length of the string.  Used to implement [] on strings.
  inline bool IsStringObjectWithCharacterAt(uint32_t index);

#ifdef DEBUG
  // Prints this object with details.
  void Print();
  void PrintLn();
  // Verifies the object.
  void Verify();

  // Verify a pointer is a valid object pointer.
  static void VerifyPointer(Object* p);
#endif

  // Prints this object without details.
  void ShortPrint();

  // Prints this object without details to a message accumulator.
  void ShortPrint(StringStream* accumulator);

  // Casting: This cast is only needed to satisfy macros in objects-inl.h.
  static Object* cast(Object* value) { return value; }

  // Layout description.
  static const int kSize = 0;  // Object does not take up any space.

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Object);
};


// Smi represents integer Numbers that can be stored in 31 bits.
// Smis are immediate which means they are NOT allocated in the heap.
// The this pointer has the following format: [31 bit signed int] 0
// Smi stands for small integer.
class Smi: public Object {
 public:
  // Returns the integer value.
  inline int value();

  // Convert a value to a Smi object.
  static inline Smi* FromInt(int value);

  // Returns whether value can be represented in a Smi.
  static inline bool IsValid(int value);

  // Casting.
  static inline Smi* cast(Object* object);

  // Dispatched behavior.
  void SmiPrint();
  void SmiPrint(StringStream* accumulator);
#ifdef DEBUG
  void SmiVerify();
#endif

  // Min and max limits for Smi values.
  static const int kMinValue = -(1 << (kBitsPerPointer - (kSmiTagSize + 1)));
  static const int kMaxValue = (1 << (kBitsPerPointer - (kSmiTagSize + 1))) - 1;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Smi);
};


// Failure is used for reporing out of memory situations and
// propagating exceptions through the runtime system.  Failure objects
// are transient and cannot occur as part of the objects graph.
//
// Failures are a single word, encoded as follows:
// +-------------------------+---+--+--+
// |rrrrrrrrrrrrrrrrrrrrrrrrr|sss|tt|11|
// +-------------------------+---+--+--+
//
// The low two bits, 0-1, are the failure tag, 11.  The next two bits,
// 2-3, are a failure type tag 'tt' with possible values:
//   00 RETRY_AFTER_GC
//   01 EXCEPTION
//   10 INTERNAL_ERROR
//   11 OUT_OF_MEMORY_EXCEPTION
//
// The next three bits, 4-6, are an allocation space tag 'sss'.  The
// allocation space tag is 000 for all failure types except
// RETRY_AFTER_GC.  For RETRY_AFTER_GC, the possible values are
// (the encoding is found in globals.h):
//   000 NEW_SPACE
//   001 OLD_SPACE
//   010 CODE_SPACE
//   011 MAP_SPACE
//   100 LO_SPACE
//
// The remaining bits is the number of words requested by the
// allocation request that failed, and is zeroed except for
// RETRY_AFTER_GC failures.  The 25 bits (on a 32 bit platform) gives
// a representable range of 2^27 bytes (128MB).

// Failure type tag info.
const int kFailureTypeTagSize = 2;
const int kFailureTypeTagMask = (1 << kFailureTypeTagSize) - 1;

class Failure: public Object {
 public:
  // RuntimeStubs assumes EXCEPTION = 1 in the compiler-generated code.
  enum Type {
    RETRY_AFTER_GC = 0,
    EXCEPTION = 1,       // Returning this marker tells the real exception
                         // is in Top::pending_exception.
    INTERNAL_ERROR = 2,
    OUT_OF_MEMORY_EXCEPTION = 3
  };

  inline Type type() const;

  // Returns the space that needs to be collected for RetryAfterGC failures.
  inline AllocationSpace allocation_space() const;

  // Returns the number of bytes requested (up to the representable maximum)
  // for RetryAfterGC failures.
  inline int requested() const;

  inline bool IsInternalError() const;
  inline bool IsOutOfMemoryException() const;

  static Failure* RetryAfterGC(int requested_bytes, AllocationSpace space);
  static inline Failure* Exception();
  static inline Failure* InternalError();
  static inline Failure* OutOfMemoryException();
  // Casting.
  static inline Failure* cast(Object* object);

  // Dispatched behavior.
  void FailurePrint();
  void FailurePrint(StringStream* accumulator);
#ifdef DEBUG
  void FailureVerify();
#endif

 private:
  inline int value() const;
  static inline Failure* Construct(Type type, int value = 0);

  DISALLOW_IMPLICIT_CONSTRUCTORS(Failure);
};


// HeapObject is the superclass for all classes describing heap allocated
// objects.
class HeapObject: public Object {
 public:
  // [map]: contains a Map which contains the objects reflective information.
  inline Map* map();
  inline void set_map(Map* value);

  // Converts an address to a HeapObject pointer.
  static inline HeapObject* FromAddress(Address address);

  // Returns the address of this HeapObject.
  inline Address address();

  // Iterates over pointers contained in the object (including the Map)
  void Iterate(ObjectVisitor* v);

  // Iterates over all pointers contained in the object except the
  // first map pointer.  The object type is given in the first
  // parameter. This function does not access the map pointer in the
  // object, and so is safe to call while the map pointer is modified.
  void IterateBody(InstanceType type, int object_size, ObjectVisitor* v);

  // This method only applies to struct objects.  Iterates over all the fields
  // of this struct.
  void IterateStructBody(int object_size, ObjectVisitor* v);

  // Copy the body from the 'from' object to this.
  // Please note the two object must have the same map prior to the call.
  inline void CopyBody(JSObject* from);

  // Returns the heap object's size in bytes
  inline int Size();

  // Given a heap object's map pointer, returns the heap size in bytes
  // Useful when the map pointer field is used for other purposes.
  // GC internal.
  inline int SizeFromMap(Map* map);

  static inline Object* GetHeapObjectField(HeapObject* obj, int index);

  // Casting.
  static inline HeapObject* cast(Object* obj);

  // Dispatched behavior.
  void HeapObjectShortPrint(StringStream* accumulator);
#ifdef DEBUG
  void HeapObjectPrint();
  void HeapObjectVerify();
  inline void VerifyObjectField(int offset);

  void PrintHeader(const char* id);

  // Verify a pointer is a valid HeapObject pointer that points to object
  // areas in the heap.
  static void VerifyHeapPointer(Object* p);
#endif

  // Layout description.
  // First field in a heap object is map.
  static const int kMapOffset = Object::kSize;
  static const int kSize = kMapOffset + kPointerSize;

 protected:
  // helpers for calling an ObjectVisitor to iterate over pointers in the
  // half-open range [start, end) specified as integer offsets
  inline void IteratePointers(ObjectVisitor* v, int start, int end);
  // as above, for the single element at "offset"
  inline void IteratePointer(ObjectVisitor* v, int offset);

  // Computes the object size from the map.
  // Should only be used from SizeFromMap.
  int SlowSizeFromMap(Map* map);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HeapObject);
};


// The HeapNumber class describes heap allocated numbers that cannot be
// represented in a Smi (small integer)
class HeapNumber: public HeapObject {
 public:
  // [value]: number value.
  inline double value();
  inline void set_value(double value);

  // Casting.
  static inline HeapNumber* cast(Object* obj);

  // Dispatched behavior.
  Object* HeapNumberToBoolean();
  void HeapNumberPrint();
  void HeapNumberPrint(StringStream* accumulator);
#ifdef DEBUG
  void HeapNumberVerify();
#endif

  // Layout description.
  static const int kValueOffset = HeapObject::kSize;
  static const int kSize = kValueOffset + kDoubleSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HeapNumber);
};


// The JSObject describes real heap allocated JavaScript objects with
// properties.
// Note that the map of JSObject changes during execution to enable inline
// caching.
class JSObject: public HeapObject {
 public:
  // [properties]: Backing storage for properties.
  DECL_ACCESSORS(properties, FixedArray)
  inline void initialize_properties();

  // [elements]: The elements in the fast case.
  DECL_ACCESSORS(elements, HeapObject)
  inline void initialize_elements();

  // Accessors for properties.
  inline bool HasFastProperties();

  // Do we want to keep the elements in fast case when increasing the
  // capacity?
  bool KeepInFastCase(int new_capacity);

  // Accessors for slow properties
  inline Dictionary* property_dictionary();  // asserts !HasFastProperties
  inline Dictionary* element_dictionary();  // asserts !HasFastElements

  Object* SetProperty(String* key,
                      Object* value,
                      PropertyAttributes attributes);
  Object* SetProperty(LookupResult* result,
                      String* key,
                      Object* value,
                      PropertyAttributes attributes);
  Object* SetPropertyWithFailedAccessCheck(LookupResult* result,
                                           String* name,
                                           Object* value);
  Object* SetPropertyWithCallback(Object* structure,
                                  String* name,
                                  Object* value,
                                  JSObject* holder);
  Object* SetPropertyWithInterceptor(String* name,
                                     Object* value,
                                     PropertyAttributes attributes);
  Object* SetPropertyPostInterceptor(String* name,
                                     Object* value,
                                     PropertyAttributes attributes);
  Object* IgnoreAttributesAndSetLocalProperty(String* key,
                                              Object* value);
  Object* SetLazyProperty(LookupResult* result,
                          String* name,
                          Object* value,
                          PropertyAttributes attributes);

  // Returns the class name ([[Class]] property in the specification).
  String* class_name();

  // Retrieve interceptors.
  InterceptorInfo* GetNamedInterceptor();
  InterceptorInfo* GetIndexedInterceptor();

  inline PropertyAttributes GetPropertyAttribute(String* name);
  PropertyAttributes GetPropertyAttributeWithReceiver(JSObject* receiver,
                                                      String* name);
  PropertyAttributes GetLocalPropertyAttribute(String* name);

  Object* DefineAccessor(String* name, bool is_getter, JSFunction* fun,
                         PropertyAttributes attributes);
  Object* LookupAccessor(String* name, bool is_getter);

  // Used from Object::GetProperty().
  Object* GetPropertyWithFailedAccessCheck(Object* receiver,
                                           LookupResult* result,
                                           String* name);
  Object* GetPropertyWithInterceptor(JSObject* receiver,
                                     String* name,
                                     PropertyAttributes* attributes);
  Object* GetPropertyPostInterceptor(JSObject* receiver,
                                     String* name,
                                     PropertyAttributes* attributes);
  Object* GetLazyProperty(Object* receiver,
                          LookupResult* result,
                          String* name,
                          PropertyAttributes* attributes);

  bool HasProperty(String* name) {
    return GetPropertyAttribute(name) != ABSENT;
  }

  bool HasLocalProperty(String* name) {
    return GetLocalPropertyAttribute(name) != ABSENT;
  }

  Object* DeleteProperty(String* name);
  Object* DeleteElement(uint32_t index);
  Object* DeleteLazyProperty(LookupResult* result, String* name);

  // Tests for the fast common case for property enumeration.
  bool IsSimpleEnum();

  // Tells whether the backing storage for elements is fast (FixedArray).
  inline bool HasFastElements();

  // Returns true if the backing storage for the slow-case elements of
  // this object takes up nearly as much space as a fast-case backing
  // storage would.  In that case the JSObject should have fast
  // elements.
  bool ShouldHaveFastElements();

  // Return the object's prototype (might be Heap::null_value()).
  inline Object* GetPrototype();

  // Tells whether the index'th element is present.
  inline bool HasElement(uint32_t index);
  bool HasElementWithReceiver(JSObject* receiver, uint32_t index);
  bool HasLocalElement(uint32_t index);

  bool HasElementWithInterceptor(JSObject* receiver, uint32_t index);
  bool HasElementPostInterceptor(JSObject* receiver, uint32_t index);

  Object* SetFastElement(uint32_t index, Object* value);

  // Set the index'th array element.
  // A Failure object is returned if GC is needed.
  Object* SetElement(uint32_t index, Object* value);

  // Returns the index'th element.
  // The undefined object if index is out of bounds.
  Object* GetElementWithReceiver(JSObject* receiver, uint32_t index);

  void SetFastElements(FixedArray* elements);
  Object* SetSlowElements(Object* length);

  // Lookup interceptors are used for handling properties controlled by host
  // objects.
  inline bool HasNamedInterceptor();
  inline bool HasIndexedInterceptor();

  // Support functions for v8 api (needed for correct interceptor behavior).
  bool HasRealNamedProperty(String* key);
  bool HasRealElementProperty(uint32_t index);
  bool HasRealNamedCallbackProperty(String* key);

  // Initializes the array to a certain length
  Object* SetElementsLength(Object* length);

  // Get the header size for a JSObject.  Used to compute the index of
  // internal fields as well as the number of internal fields.
  inline int GetHeaderSize();

  inline int GetInternalFieldCount();
  inline Object* GetInternalField(int index);
  inline void SetInternalField(int index, Object* value);

  // Returns a deep copy of the JavaScript object.
  // Properties and elements are copied too.
  // Returns failure if allocation failed.
  Object* Copy(PretenureFlag pretenure = NOT_TENURED);

  // Lookup a property.  If found, the result is valid and has
  // detailed information.
  void LocalLookup(String* name, LookupResult* result);
  void Lookup(String* name, LookupResult* result);

  // The following lookup functions skip interceptors.
  void LocalLookupRealNamedProperty(String* name, LookupResult* result);
  void LookupRealNamedProperty(String* name, LookupResult* result);
  void LookupRealNamedPropertyInPrototypes(String* name, LookupResult* result);
  void LookupCallbackSetterInPrototypes(String* name, LookupResult* result);

  // Returns the number of properties on this object filtering out properties
  // with the specified attributes (ignoring interceptors).
  int NumberOfLocalProperties(PropertyAttributes filter);
  // Returns the number of enumerable properties (ignoring interceptors).
  int NumberOfEnumProperties();
  // Fill in details for properties into storage.
  void GetLocalPropertyNames(FixedArray* storage);

  // Returns the number of properties on this object filtering out properties
  // with the specified attributes (ignoring interceptors).
  int NumberOfLocalElements(PropertyAttributes filter);
  // Returns the number of enumerable elements (ignoring interceptors).
  int NumberOfEnumElements();
  // Returns the number of elements on this object filtering out elements
  // with the specified attributes (ignoring interceptors).
  int GetLocalElementKeys(FixedArray* storage, PropertyAttributes filter);
  // Count and fill in the enumerable elements into storage.
  // (storage->length() == NumberOfEnumElements()).
  // If storage is NULL, will count the elements without adding
  // them to any storage.
  // Returns the number of enumerable elements.
  int GetEnumElementKeys(FixedArray* storage);

  // Add a property to a fast-case object using a map transition to
  // new_map.
  Object* AddFastPropertyUsingMap(Map* new_map,
                                  String* name,
                                  Object* value);

  // Add a constant function property to a fast-case object.
  Object* AddConstantFunctionProperty(String* name,
                                      JSFunction* function,
                                      PropertyAttributes attributes);

  // Replace a constant function property on a fast-case object.
  Object* ReplaceConstantFunctionProperty(String* name,
                                          Object* value);

  // Add a property to a fast-case object.
  Object* AddFastProperty(String* name,
                          Object* value,
                          PropertyAttributes attributes);

  // Add a property to a slow-case object.
  Object* AddSlowProperty(String* name,
                          Object* value,
                          PropertyAttributes attributes);

  // Add a property to an object.
  Object* AddProperty(String* name,
                      Object* value,
                      PropertyAttributes attributes);

  // Convert the object to use the canonical dictionary
  // representation.
  Object* NormalizeProperties();
  Object* NormalizeElements();

  // Transform slow named properties to fast variants.
  // Returns failure if allocation failed.
  Object* TransformToFastProperties(int unused_property_fields);

  // initializes the body after properties slot, properties slot is
  // initialized by set_properties
  // Note: this call does not update write barrier, it is caller's
  // reponsibility to ensure that *v* can be collected without WB here.
  inline void InitializeBody(int object_size);

  // Check whether this object references another object
  bool ReferencesObject(Object* obj);

  // Casting.
  static inline JSObject* cast(Object* obj);

  // Dispatched behavior.
  void JSObjectIterateBody(int object_size, ObjectVisitor* v);
  void JSObjectShortPrint(StringStream* accumulator);
#ifdef DEBUG
  void JSObjectPrint();
  void JSObjectVerify();
  void PrintProperties();
  void PrintElements();

  // Structure for collecting spill information about JSObjects.
  class SpillInformation {
   public:
    void Clear();
    void Print();
    int number_of_objects_;
    int number_of_objects_with_fast_properties_;
    int number_of_objects_with_fast_elements_;
    int number_of_fast_used_fields_;
    int number_of_fast_unused_fields_;
    int number_of_slow_used_properties_;
    int number_of_slow_unused_properties_;
    int number_of_fast_used_elements_;
    int number_of_fast_unused_elements_;
    int number_of_slow_used_elements_;
    int number_of_slow_unused_elements_;
  };

  void IncrementSpillStatistics(SpillInformation* info);
#endif
  Object* SlowReverseLookup(Object* value);

  static const uint32_t kMaxGap = 1024;
  static const int kMaxFastElementsLength = 5000;

  // Layout description.
  static const int kPropertiesOffset = HeapObject::kSize;
  static const int kElementsOffset = kPropertiesOffset + kPointerSize;
  static const int kHeaderSize = kElementsOffset + kPointerSize;

  Object* GetElementWithInterceptor(JSObject* receiver, uint32_t index);

 private:
  Object* SetElementWithInterceptor(uint32_t index, Object* value);
  Object* SetElementPostInterceptor(uint32_t index, Object* value);

  Object* GetElementPostInterceptor(JSObject* receiver, uint32_t index);

  Object* DeletePropertyPostInterceptor(String* name);
  Object* DeletePropertyWithInterceptor(String* name);

  Object* DeleteElementPostInterceptor(uint32_t index);
  Object* DeleteElementWithInterceptor(uint32_t index);

  PropertyAttributes GetPropertyAttributePostInterceptor(JSObject* receiver,
                                                         String* name,
                                                         bool continue_search);
  PropertyAttributes GetPropertyAttributeWithInterceptor(JSObject* receiver,
                                                         String* name,
                                                         bool continue_search);
  PropertyAttributes GetPropertyAttribute(JSObject* receiver,
                                          LookupResult* result,
                                          String* name,
                                          bool continue_search);

  // Returns true if most of the elements backing storage is used.
  bool HasDenseElements();

  Object* DefineGetterSetter(String* name, PropertyAttributes attributes);

  void LookupInDescriptor(String* name, LookupResult* result);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSObject);
};


// Abstract super class arrays. It provides length behavior.
class Array: public HeapObject {
 public:
  // [length]: length of the array.
  inline int length();
  inline void set_length(int value);

  // Convert an object to an array index.
  // Returns true if the conversion succeeded.
  static inline bool IndexFromObject(Object* object, uint32_t* index);

  // Layout descriptor.
  static const int kLengthOffset = HeapObject::kSize;
  static const int kHeaderSize = kLengthOffset + kIntSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Array);
};


// FixedArray describes fixed sized arrays where element
// type is Object*.

class FixedArray: public Array {
 public:

  // Setter and getter for elements.
  inline Object* get(int index);
  inline void set(int index, Object* value);

  // Setters for frequently used oddballs located in old space.
  inline void set_undefined(int index);
  inline void set_the_hole(int index);

  // Setter that skips the write barrier if mode is SKIP_WRITE_BARRIER.
  enum WriteBarrierMode { SKIP_WRITE_BARRIER, UPDATE_WRITE_BARRIER };
  inline void set(int index, Object* value, WriteBarrierMode mode);
  // Return the write barrier mode for this.
  inline WriteBarrierMode GetWriteBarrierMode();

  // Copy operations.
  Object* Copy();
  Object* CopySize(int new_length);

  // Add the elements of a JSArray to this FixedArray.
  Object* AddKeysFromJSArray(JSArray* array);

  // Compute the union of this and other.
  Object* UnionOfKeys(FixedArray* other);

  // Copy a sub array from the receiver to dest.
  void CopyTo(int pos, FixedArray* dest, int dest_pos, int len);

  // Garbage collection support.
  static int SizeFor(int length) { return kHeaderSize + length * kPointerSize; }

  // Casting.
  static inline FixedArray* cast(Object* obj);

  // Dispatched behavior.
  int FixedArraySize() { return SizeFor(length()); }
  void FixedArrayIterateBody(ObjectVisitor* v);
#ifdef DEBUG
  void FixedArrayPrint();
  void FixedArrayVerify();
#endif

  // Swap two elements.
  void Swap(int i, int j);

  // Sort this array and the smis as pairs wrt. the smis.
  void SortPairs(FixedArray* smis);

 protected:
  // Set operation on FixedArray without using write barriers.
  static inline void fast_set(FixedArray* array, int index, Object* value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(FixedArray);
};


// DescriptorArrays are fixed arrays used to hold instance descriptors.
// The format of the these objects is:
//   [0]: point to a fixed array with (value, detail) pairs.
//   [1]: next enumeration index (Smi), or pointer to small fixed array:
//          [0]: next enumeration index (Smi)
//          [1]: pointer to fixed array with enum cache
//   [2]: first key
//   [length() - 1]: last key
//
class DescriptorArray: public FixedArray {
 public:
  // Returns the number of descriptors in the array.
  int number_of_descriptors() {
    int len = length();
    return len == 0 ? 0 : len - kFirstIndex;
  }

  int NextEnumerationIndex() {
    if (length() == 0) return PropertyDetails::kInitialIndex;
    Object* obj = get(kEnumerationIndexIndex);
    if (obj->IsSmi()) {
      return Smi::cast(obj)->value();
    } else {
      Object* index = FixedArray::cast(obj)->get(kEnumCacheBridgeEnumIndex);
      return Smi::cast(index)->value();
    }
  }

  // Set next enumeration index and flush any enum cache.
  void SetNextEnumerationIndex(int value) {
    fast_set(this, kEnumerationIndexIndex, Smi::FromInt(value));
  }

  bool HasEnumCache() {
    return length() > 0 && !get(kEnumerationIndexIndex)->IsSmi();
  }

  Object* GetEnumCache() {
    ASSERT(HasEnumCache());
    FixedArray* bridge = FixedArray::cast(get(kEnumerationIndexIndex));
    return bridge->get(kEnumCacheBridgeCacheIndex);
  }

  // Initialize or change the enum cache,
  // using the supplied storage for the small "bridge".
  void SetEnumCache(FixedArray* bridge_storage, FixedArray* new_cache);

  // Accessors for fetching instance descriptor at descriptor number..
  inline String* GetKey(int descriptor_number);
  inline Object* GetValue(int descriptor_number);
  inline Smi* GetDetails(int descriptor_number);

  // Accessor for complete descriptor.
  inline void Get(int descriptor_number, Descriptor* desc);
  inline void Set(int descriptor_number, Descriptor* desc);

  void ReplaceConstantFunction(int descriptor_number, JSFunction* value);

  // Copy the descriptor array, insert a new descriptor and optionally
  // remove map transitions.
  Object* CopyInsert(Descriptor* desc, bool remove_map_transitions = false);

  // Copy the descriptor array, replace the property index and attributes
  // of the named property, but preserve its enumeration index.
  Object* CopyReplace(String* name, int index, PropertyAttributes attributes);

  // Sort the instance descriptors by the hash codes of their keys.
  void Sort();

  // Is the descriptor array sorted and without duplicates?
  bool IsSortedNoDuplicates();

  // Search the instance descriptors for given name.
  inline int Search(String* name);

  // Tells whether the name is present int the array.
  bool Contains(String* name) { return kNotFound != Search(name); }

  // Perform a binary search in the instance descriptors represented
  // by this fixed array.  low and high are descriptor indices.  If there
  // are three instance descriptors in this array it should be called
  // with low=0 and high=2.
  int BinarySearch(String* name, int low, int high);

  static Object* Allocate(int number_of_descriptors);

  // Casting.
  static inline DescriptorArray* cast(Object* obj);

  // Constant for denoting key was not found.
  static const int kNotFound = -1;

  static const int kContentArrayIndex = 0;
  static const int kEnumerationIndexIndex = 1;
  static const int kFirstIndex = 2;

  // The length of the "bridge" to the enum cache.
  static const int kEnumCacheBridgeLength = 2;
  static const int kEnumCacheBridgeEnumIndex = 0;
  static const int kEnumCacheBridgeCacheIndex = 1;

  // Layout description.
  static const int kContentArrayOffset = FixedArray::kHeaderSize;
  static const int kEnumerationIndexOffset = kContentArrayOffset + kPointerSize;
  static const int kFirstOffset = kEnumerationIndexOffset + kPointerSize;

  // Layout description for the bridge array.
  static const int kEnumCacheBridgeEnumOffset = FixedArray::kHeaderSize;
  static const int kEnumCacheBridgeCacheOffset =
    kEnumCacheBridgeEnumOffset + kPointerSize;

#ifdef DEBUG
  // Print all the descriptors.
  void PrintDescriptors();
#endif

  // The maximum number of descriptors we want in a descriptor array (should
  // fit in a page).
  static const int kMaxNumberOfDescriptors = 1024 + 512;

 private:
  // Conversion from descriptor number to array indices.
  static int ToKeyIndex(int descriptor_number) {
    return descriptor_number+kFirstIndex;
  }
  static int ToValueIndex(int descriptor_number) {
    return descriptor_number << 1;
  }
  static int ToDetailsIndex(int descriptor_number) {
    return( descriptor_number << 1) + 1;
  }

  // Swap operation on FixedArray without using write barriers.
  static inline void fast_swap(FixedArray* array, int first, int second);

  // Swap descriptor first and second.
  inline void Swap(int first, int second);

  FixedArray* GetContentArray() {
    return FixedArray::cast(get(kContentArrayIndex));
  }
  DISALLOW_IMPLICIT_CONSTRUCTORS(DescriptorArray);
};


// HashTable is a subclass of FixedArray that implements a hash table
// that uses open addressing and quadratic probing.
//
// In order for the quadratic probing to work, elements that have not
// yet been used and elements that have been deleted are
// distinguished.  Probing continues when deleted elements are
// encountered and stops when unused elements are encountered.
//
// - Elements with key == undefined have not been used yet.
// - Elements with key == null have been deleted.
//
// The hash table class is parameterized with a prefix size and with
// the size, including the key size, of the elements held in the hash
// table.  The prefix size indicates an amount of memory in the
// beginning of the backing storage that can be used for non-element
// information by subclasses.
template<int prefix_size, int element_size>
class HashTable: public FixedArray {
 public:
  // Returns the number of elements in the dictionary.
  int NumberOfElements() {
    return Smi::cast(get(kNumberOfElementsIndex))->value();
  }

  // Returns the capacity of the dictionary.
  int Capacity() {
    return Smi::cast(get(kCapacityIndex))->value();
  }

  // ElementAdded should be called whenever an element is added to a
  // dictionary.
  void ElementAdded() { SetNumberOfElements(NumberOfElements() + 1); }

  // ElementRemoved should be called whenever an element is removed from
  // a dictionary.
  void ElementRemoved() { SetNumberOfElements(NumberOfElements() - 1); }
  void ElementsRemoved(int n) { SetNumberOfElements(NumberOfElements() - n); }

  // Returns a new array for dictionary usage. Might return Failure.
  static Object* Allocate(int at_least_space_for);

  // Returns the key at entry.
  Object* KeyAt(int entry) { return get(EntryToIndex(entry)); }

  // Tells wheter k is a real key.  Null and undefined are not allowed
  // as keys and can be used to indicate missing or deleted elements.
  bool IsKey(Object* k) {
    return !k->IsNull() && !k->IsUndefined();
  }

  // Garbage collection support.
  void IteratePrefix(ObjectVisitor* visitor);
  void IterateElements(ObjectVisitor* visitor);

  // Casting.
  static inline HashTable* cast(Object* obj);

  // Key is an abstract superclass keys.
  class Key {
   public:
    // Returns whether the other object matches this key.
    virtual bool IsMatch(Object* other) = 0;
    typedef uint32_t (*HashFunction)(Object* obj);
    // Returns the hash function used for this key.
    virtual HashFunction GetHashFunction() = 0;
    // Returns the hash value for this key.
    virtual uint32_t Hash() = 0;
    // Returns the key object for storing into the dictionary.
    // If allocations fails a failure object is returned.
    virtual Object* GetObject() = 0;
    virtual bool IsStringKey() = 0;
    // Required.
    virtual ~Key() {}
  };

  // Compute the probe offset (quadratic probing).
  INLINE(static uint32_t GetProbeOffset(uint32_t n)) {
    return (n + n * n) >> 1;
  }

  static const int kNumberOfElementsIndex = 0;
  static const int kCapacityIndex         = 1;
  static const int kPrefixStartIndex      = 2;
  static const int kElementsStartIndex    = kPrefixStartIndex + prefix_size;
  static const int kElementSize           = element_size;
  static const int kElementsStartOffset   =
      kHeaderSize + kElementsStartIndex * kPointerSize;

 protected:
  // Find entry for key otherwise return -1.
  int FindEntry(Key* key);

  // Find the entry at which to insert element with the given key that
  // has the given hash value.
  uint32_t FindInsertionEntry(Object* key, uint32_t hash);

  // Returns the index for an entry (of the key)
  static inline int EntryToIndex(int entry) {
    return (entry * kElementSize) + kElementsStartIndex;
  }

  // Update the number of elements in the dictionary.
  void SetNumberOfElements(int nof) {
    fast_set(this, kNumberOfElementsIndex, Smi::FromInt(nof));
  }

  // Sets the capacity of the hash table.
  void SetCapacity(int capacity) {
    // To scale a computed hash code to fit within the hash table, we
    // use bit-wise AND with a mask, so the capacity must be positive
    // and non-zero.
    ASSERT(capacity > 0);
    fast_set(this, kCapacityIndex, Smi::FromInt(capacity));
  }


  // Returns probe entry.
  static uint32_t GetProbe(uint32_t hash, uint32_t number, uint32_t size) {
    ASSERT(IsPowerOf2(size));
    return (hash + GetProbeOffset(number)) & (size - 1);
  }

  // Ensure enough space for n additional elements.
  Object* EnsureCapacity(int n, Key* key);
};


// SymbolTable.
//
// No special elements in the prefix and the element size is 1
// because only the symbol itself (the key) needs to be stored.
class SymbolTable: public HashTable<0, 1> {
 public:
  // Find symbol in the symbol table.  If it is not there yet, it is
  // added.  The return value is the symbol table which might have
  // been enlarged.  If the return value is not a failure, the symbol
  // pointer *s is set to the symbol found.
  Object* LookupSymbol(Vector<const char> str, Object** s);
  Object* LookupString(String* key, Object** s);

  // Casting.
  static inline SymbolTable* cast(Object* obj);

 private:
  Object* LookupKey(Key* key, Object** s);
  class Utf8Key;   // Key based on utf8 string.
  class StringKey;  // Key based on String*.

  DISALLOW_IMPLICIT_CONSTRUCTORS(SymbolTable);
};


// Dictionary for keeping properties and elements in slow case.
//
// One element in the prefix is used for storing non-element
// information about the dictionary.
//
// The rest of the array embeds triples of (key, value, details).
// if key == undefined the triple is empty.
// if key == null the triple has been deleted.
// otherwise key contains the name of a property.
class DictionaryBase: public HashTable<2, 3> {};

class Dictionary: public DictionaryBase {
 public:
  // Returns the value at entry.
  Object* ValueAt(int entry) { return get(EntryToIndex(entry)+1); }

  // Set the value for entry.
  void ValueAtPut(int entry, Object* value) {
    set(EntryToIndex(entry)+1, value);
  }

  // Returns the property details for the property at entry.
  PropertyDetails DetailsAt(int entry) {
    return PropertyDetails(Smi::cast(get(EntryToIndex(entry) + 2)));
  }

  // Set the details for entry.
  void DetailsAtPut(int entry, PropertyDetails value) {
    set(EntryToIndex(entry) + 2, value.AsSmi());
  }

  // Remove all entries were key is a number and (from <= key && key < to).
  void RemoveNumberEntries(uint32_t from, uint32_t to);

  // Sorting support
  Object* RemoveHoles();
  void CopyValuesTo(FixedArray* elements);

  // Casting.
  static inline Dictionary* cast(Object* obj);

  // Find entry for string key otherwise return -1.
  int FindStringEntry(String* key);

  // Find entry for number key otherwise return -1.
  int FindNumberEntry(uint32_t index);

  // Delete a property from the dictionary.
  Object* DeleteProperty(int entry);

  // Type specific at put (default NONE attributes is used when adding).
  Object* AtStringPut(String* key, Object* value);
  Object* AtNumberPut(uint32_t key, Object* value);

  Object* AddStringEntry(String* key, Object* value, PropertyDetails details);
  Object* AddNumberEntry(uint32_t key, Object* value, PropertyDetails details);

  // Set and existing string entry or add a new one if needed.
  Object* SetOrAddStringEntry(String* key,
                              Object* value,
                              PropertyDetails details);

  // Returns the number of elements in the dictionary filtering out properties
  // with the specified attributes.
  int NumberOfElementsFilterAttributes(PropertyAttributes filter);

  // Returns the number of enumerable elements in the dictionary.
  int NumberOfEnumElements();

  // Copies keys to preallocated fixed array.
  void CopyKeysTo(FixedArray* storage, PropertyAttributes filter);
  // Copies enumerable keys to preallocated fixed array.
  void CopyEnumKeysTo(FixedArray* storage, FixedArray* sort_array);
  // Fill in details for properties into storage.
  void CopyKeysTo(FixedArray* storage);

  // Returns the value at entry.
  static int ValueIndexFor(int entry) { return EntryToIndex(entry)+1; }

  // For transforming properties of a JSObject.
  Object* TransformPropertiesToFastFor(JSObject* obj,
                                       int unused_property_fields);

  // If slow elements are required we will never go back to fast-case
  // for the elements kept in this dictionary.  We require slow
  // elements if an element has been added at an index larger than
  // kRequiresSlowElementsLimit.
  inline bool requires_slow_elements();

  // Get the value of the max number key that has been added to this
  // dictionary.  max_number_key can only be called if
  // requires_slow_elements returns false.
  inline uint32_t max_number_key();

  // Accessors for next enumeration index.
  void SetNextEnumerationIndex(int index) {
    fast_set(this, kNextEnumnerationIndexIndex, Smi::FromInt(index));
  }

  int NextEnumerationIndex() {
    return Smi::cast(get(kNextEnumnerationIndexIndex))->value();
  }

  // Returns a new array for dictionary usage. Might return Failure.
  static Object* Allocate(int at_least_space_for);

  // Ensure enough space for n additional elements.
  Object* EnsureCapacity(int n, Key* key);

#ifdef DEBUG
  void Print();
#endif
  // Returns the key (slow).
  Object* SlowReverseLookup(Object* value);

  // Bit masks.
  static const int kRequiresSlowElementsMask = 1;
  static const int kRequiresSlowElementsTagSize = 1;
  static const uint32_t kRequiresSlowElementsLimit = (1 << 29) - 1;

 private:
  // Generic at put operation.
  Object* AtPut(Key* key, Object* value);

  Object* Add(Key* key, Object* value, PropertyDetails details);

  // Add entry to dictionary.
  void AddEntry(Object* key,
                Object* value,
                PropertyDetails details,
                uint32_t hash);

  // Sets the entry to (key, value) pair.
  inline void SetEntry(int entry,
                       Object* key,
                       Object* value,
                       PropertyDetails details);

  void UpdateMaxNumberKey(uint32_t key);

  // Generate new enumneration indices to avoid enumeration insdex overflow.
  Object* GenerateNewEnumerationIndices();

  static const int kMaxNumberKeyIndex = kPrefixStartIndex;
  static const int kNextEnumnerationIndexIndex = kMaxNumberKeyIndex + 1;

  class NumberKey;  // Key containing uint32_t.
  class StringKey;  // Key containing String*.

  DISALLOW_IMPLICIT_CONSTRUCTORS(Dictionary);
};


// ByteArray represents fixed sized byte arrays.  Used by the outside world,
// such as PCRE, and also by the memory allocator and garbage collector to
// fill in free blocks in the heap.
class ByteArray: public Array {
 public:
  // Setter and getter.
  inline byte get(int index);
  inline void set(int index, byte value);

  // Treat contents as an int array.
  inline int get_int(int index);

  static int SizeFor(int length) {
    return kHeaderSize + OBJECT_SIZE_ALIGN(length);
  }
  // We use byte arrays for free blocks in the heap.  Given a desired size in
  // bytes that is a multiple of the word size and big enough to hold a byte
  // array, this function returns the number of elements a byte array should
  // have.
  static int LengthFor(int size_in_bytes) {
    ASSERT(IsAligned(size_in_bytes, kPointerSize));
    ASSERT(size_in_bytes >= kHeaderSize);
    return size_in_bytes - kHeaderSize;
  }

  // Returns data start address.
  inline Address GetDataStartAddress();

  // Returns a pointer to the ByteArray object for a given data start address.
  static inline ByteArray* FromDataStartAddress(Address address);

  // Casting.
  static inline ByteArray* cast(Object* obj);

  // Dispatched behavior.
  int ByteArraySize() { return SizeFor(length()); }
#ifdef DEBUG
  void ByteArrayPrint();
  void ByteArrayVerify();
#endif

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ByteArray);
};


// Code describes objects with on-the-fly generated machine code.
class Code: public HeapObject {
 public:
  // Opaque data type for encapsulating code flags like kind, inline
  // cache state, and arguments count.
  enum Flags { };

  enum Kind {
    FUNCTION,
    STUB,
    BUILTIN,
    LOAD_IC,
    KEYED_LOAD_IC,
    CALL_IC,
    STORE_IC,
    KEYED_STORE_IC,

    // Pseudo-kinds.
    FIRST_IC_KIND = LOAD_IC,
    LAST_IC_KIND = KEYED_STORE_IC
  };

  enum {
    NUMBER_OF_KINDS = LAST_IC_KIND + 1
  };

  // A state indicates that inline cache in this Code object contains
  // objects or relative instruction addresses.
  enum ICTargetState {
    IC_TARGET_IS_ADDRESS,
    IC_TARGET_IS_OBJECT
  };

#ifdef DEBUG
  static const char* Kind2String(Kind kind);
#endif

  // [instruction_size]: Size of the native instructions
  inline int instruction_size();
  inline void set_instruction_size(int value);

  // [relocation_size]: Size of relocation information.
  inline int relocation_size();
  inline void set_relocation_size(int value);

  // [sinfo_size]: Size of scope information.
  inline int sinfo_size();
  inline void set_sinfo_size(int value);

  // [flags]: Various code flags.
  inline Flags flags();
  inline void set_flags(Flags flags);

  // [flags]: Access to specific code flags.
  inline Kind kind();
  inline InlineCacheState state();  // only valid for IC stubs
  inline PropertyType type();  // only valid for monomorphic IC stubs
  inline int arguments_count();  // only valid for call IC stubs
  inline CodeStub::Major major_key();  // only valid for kind STUB

  // Testers for IC stub kinds.
  inline bool is_inline_cache_stub();
  inline bool is_load_stub() { return kind() == LOAD_IC; }
  inline bool is_keyed_load_stub() { return kind() == KEYED_LOAD_IC; }
  inline bool is_store_stub() { return kind() == STORE_IC; }
  inline bool is_keyed_store_stub() { return kind() == KEYED_STORE_IC; }
  inline bool is_call_stub() { return kind() == CALL_IC; }

  // [ic_flag]: State of inline cache targets. The flag is set to the
  // object variant in ConvertICTargetsFromAddressToObject, and set to
  // the address variant in ConvertICTargetsFromObjectToAddress.
  inline ICTargetState ic_flag();
  inline void set_ic_flag(ICTargetState value);

  // Flags operations.
  static inline Flags ComputeFlags(Kind kind,
                                   InlineCacheState state = UNINITIALIZED,
                                   PropertyType type = NORMAL,
                                   int argc = -1);

  static inline Flags ComputeMonomorphicFlags(Kind kind,
                                              PropertyType type,
                                              int argc = -1);

  static inline Kind ExtractKindFromFlags(Flags flags);
  static inline InlineCacheState ExtractStateFromFlags(Flags flags);
  static inline PropertyType ExtractTypeFromFlags(Flags flags);
  static inline int ExtractArgumentsCountFromFlags(Flags flags);
  static inline Flags RemoveTypeFromFlags(Flags flags);


  // Returns the address of the first instruction.
  inline byte* instruction_start();

  // Returns the size of the instructions, padding, and relocation information.
  inline int body_size();

  // Returns the address of the first relocation info (read backwards!).
  inline byte* relocation_start();

  // Code entry point.
  inline byte* entry();

  // Returns true if pc is inside this object's instructions.
  inline bool contains(byte* pc);

  // Returns the adddress of the scope information.
  inline byte* sinfo_start();

  // Convert inline cache target from address to code object before GC.
  void ConvertICTargetsFromAddressToObject();

  // Convert inline cache target from code object to address after GC
  void ConvertICTargetsFromObjectToAddress();

  // Relocate the code by delta bytes. Called to signal that this code
  // object has been moved by delta bytes.
  void Relocate(int delta);

  // Migrate code described by desc.
  void CopyFrom(const CodeDesc& desc);

  // Returns the object size for a given body and sinfo size (Used for
  // allocation).
  static int SizeFor(int body_size, int sinfo_size) {
    ASSERT_SIZE_TAG_ALIGNED(body_size);
    ASSERT_SIZE_TAG_ALIGNED(sinfo_size);
    return kHeaderSize + body_size + sinfo_size;
  }

  // Locating source position.
  int SourcePosition(Address pc);
  int SourceStatementPosition(Address pc);

  // Casting.
  static inline Code* cast(Object* obj);

  // Dispatched behavior.
  int CodeSize() { return SizeFor(body_size(), sinfo_size()); }
  void CodeIterateBody(ObjectVisitor* v);
#ifdef DEBUG
  void CodePrint();
  void CodeVerify();
#endif

  // Layout description.
  static const int kInstructionSizeOffset = HeapObject::kSize;
  static const int kRelocationSizeOffset = kInstructionSizeOffset + kIntSize;
  static const int kSInfoSizeOffset = kRelocationSizeOffset + kIntSize;
  static const int kFlagsOffset = kSInfoSizeOffset + kIntSize;
  static const int kICFlagOffset = kFlagsOffset + kIntSize;
  static const int kHeaderSize = kICFlagOffset + kIntSize;

  // Flags layout.
  static const int kFlagsStateShift          = 0;
  static const int kFlagsKindShift           = 3;
  static const int kFlagsTypeShift           = 6;
  static const int kFlagsArgumentsCountShift = 9;

  static const int kFlagsStateMask          = 0x00000007;  // 000000111
  static const int kFlagsKindMask           = 0x00000038;  // 000111000
  static const int kFlagsTypeMask           = 0x000001C0;  // 111000000
  static const int kFlagsArgumentsCountMask = 0xFFFFFE00;


 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Code);
};


// All heap objects have a Map that describes their structure.
//  A Map contains information about:
//  - Size information about the object
//  - How to iterate over an object (for garbage collection)
class Map: public HeapObject {
 public:
  // instance size.
  inline int instance_size();
  inline void set_instance_size(int value);

  // instance type.
  inline InstanceType instance_type();
  inline void set_instance_type(InstanceType value);

  // tells how many unused property fields are available in the instance.
  // (only used for JSObject in fast mode).
  inline int unused_property_fields();
  inline void set_unused_property_fields(int value);

  // bit field.
  inline byte bit_field();
  inline void set_bit_field(byte value);

  // Tells whether this object has a special lookup behavior.
  void set_special_lookup() {
    set_bit_field(bit_field() | (1 << kHasSpecialLookup));
  }

  bool has_special_lookup() {
    return ((1 << kHasSpecialLookup) & bit_field()) != 0;
  }

  // Tells whether the object in the prototype property will be used
  // for instances created from this function.  If the prototype
  // property is set to a value that is not a JSObject, the prototype
  // property will not be used to create instances of the function.
  // See ECMA-262, 13.2.2.
  inline void set_non_instance_prototype(bool value);
  inline bool has_non_instance_prototype();

  // Tells whether the instance with this map should be ignored by the
  // __proto__ accessor.
  inline void set_is_hidden_prototype() {
    set_bit_field(bit_field() | (1 << kIsHiddenPrototype));
  }

  inline bool is_hidden_prototype() {
    return ((1 << kIsHiddenPrototype) & bit_field()) != 0;
  }

  // Tells whether the instance has a named interceptor.
  inline void set_has_named_interceptor() {
    set_bit_field(bit_field() | (1 << kHasNamedInterceptor));
  }

  inline bool has_named_interceptor() {
    return ((1 << kHasNamedInterceptor) & bit_field()) != 0;
  }

  // Tells whether the instance has a named interceptor.
  inline void set_has_indexed_interceptor() {
    set_bit_field(bit_field() | (1 << kHasIndexedInterceptor));
  }

  inline bool has_indexed_interceptor() {
    return ((1 << kHasIndexedInterceptor) & bit_field()) != 0;
  }

  // Tells whether the instance is undetectable.
  // An undetectable object is a special class of JSObject: 'typeof' operator
  // returns undefined, ToBoolean returns false. Otherwise it behaves like
  // a normal JS object.  It is useful for implementing undetectable
  // document.all in Firefox & Safari.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=248549.
  inline void set_is_undetectable() {
    set_bit_field(bit_field() | (1 << kIsUndetectable));
  }

  inline bool is_undetectable() {
    return ((1 << kIsUndetectable) & bit_field()) != 0;
  }

  // Tells whether the instance has a call-as-function handler.
  inline void set_has_instance_call_handler() {
    set_bit_field(bit_field() | (1 << kHasInstanceCallHandler));
  }

  inline bool has_instance_call_handler() {
    return ((1 << kHasInstanceCallHandler) & bit_field()) != 0;
  }

  // Tells whether the instance needs security checks when accessing its
  // properties.
  inline void set_needs_access_check() {
    set_bit_field(bit_field() | (1 << kNeedsAccessCheck));
  }

  inline bool needs_access_check() {
    return ((1 << kNeedsAccessCheck) & bit_field()) != 0;
  }

  // [prototype]: implicit prototype object.
  DECL_ACCESSORS(prototype, Object)

  // [constructor]: points back to the function responsible for this map.
  DECL_ACCESSORS(constructor, Object)

  // [instance descriptors]: describes the object.
  DECL_ACCESSORS(instance_descriptors, DescriptorArray)

  // [stub cache]: contains stubs compiled for this map.
  DECL_ACCESSORS(code_cache, FixedArray)

  // Returns a copy of the map.
  Object* Copy();

  // Returns the property index for name (only valid for FAST MODE).
  int PropertyIndexFor(String* name);

  // Returns the next free property index (only valid for FAST MODE).
  int NextFreePropertyIndex();

  // Returns the number of properties described in instance_descriptors.
  int NumberOfDescribedProperties();

  // Casting.
  static inline Map* cast(Object* obj);

  // Locate an accessor in the instance descriptor.
  AccessorDescriptor* FindAccessor(String* name);

  // Make sure the instance descriptor has no map transitions
  Object* EnsureNoMapTransitions();

  // Code cache operations.

  // Clears the code cache.
  inline void ClearCodeCache();

  // Update code cache.
  Object* UpdateCodeCache(String* name, Code* code);

  // Returns the found code or undefined if absent.
  Object* FindInCodeCache(String* name, Code::Flags flags);

  // Tells whether code is in the code cache.
  bool IncludedInCodeCache(Code* code);

  // Dispatched behavior.
  void MapIterateBody(ObjectVisitor* v);
#ifdef DEBUG
  void MapPrint();
  void MapVerify();
#endif

  // Layout description.
  static const int kInstanceAttributesOffset = HeapObject::kSize;
  static const int kPrototypeOffset = kInstanceAttributesOffset + kIntSize;
  static const int kConstructorOffset = kPrototypeOffset + kPointerSize;
  static const int kInstanceDescriptorsOffset =
      kConstructorOffset + kPointerSize;
  static const int kCodeCacheOffset = kInstanceDescriptorsOffset + kPointerSize;
  static const int kSize = kCodeCacheOffset + kIntSize;

  // Byte offsets within kInstanceAttributesOffset attributes.
  static const int kInstanceSizeOffset = kInstanceAttributesOffset + 0;
  static const int kInstanceTypeOffset = kInstanceAttributesOffset + 1;
  static const int kUnusedPropertyFieldsOffset = kInstanceAttributesOffset + 2;
  static const int kBitFieldOffset = kInstanceAttributesOffset + 3;

  // Bit positions for bit field.
  static const int kHasSpecialLookup = 0;
  static const int kHasNonInstancePrototype = 1;
  static const int kIsHiddenPrototype = 2;
  static const int kHasNamedInterceptor = 3;
  static const int kHasIndexedInterceptor = 4;
  static const int kIsUndetectable = 5;
  static const int kHasInstanceCallHandler = 6;
  static const int kNeedsAccessCheck = 7;
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Map);
};


// An abstract superclass, a marker class really, for simple structure classes.
// It doesn't carry much functionality but allows struct classes to me
// identified in the type system.
class Struct: public HeapObject {
 public:
  inline void InitializeBody(int object_size);
  static inline Struct* cast(Object* that);
};


// Script types.
enum ScriptType {
  SCRIPT_TYPE_NATIVE,
  SCRIPT_TYPE_EXTENSION,
  SCRIPT_TYPE_NORMAL
};


// Script describes a script which has beed added to the VM.
class Script: public Struct {
 public:
  // [source]: the script source.
  DECL_ACCESSORS(source, Object)

  // [name]: the script name.
  DECL_ACCESSORS(name, Object)

  // [line_offset]: script line offset in resource from where it was extracted.
  DECL_ACCESSORS(line_offset, Smi)

  // [column_offset]: script column offset in resource from where it was
  // extracted.
  DECL_ACCESSORS(column_offset, Smi)

  // [wrapper]: the wrapper cache.
  DECL_ACCESSORS(wrapper, Proxy)

  // [type]: the script type.
  DECL_ACCESSORS(type, Smi)

  static inline Script* cast(Object* obj);

#ifdef DEBUG
  void ScriptPrint();
  void ScriptVerify();
#endif

  static const int kSourceOffset = HeapObject::kSize;
  static const int kNameOffset = kSourceOffset + kPointerSize;
  static const int kLineOffsetOffset = kNameOffset + kPointerSize;
  static const int kColumnOffsetOffset = kLineOffsetOffset + kPointerSize;
  static const int kWrapperOffset = kColumnOffsetOffset + kPointerSize;
  static const int kTypeOffset = kWrapperOffset + kPointerSize;
  static const int kSize = kTypeOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Script);
};


// SharedFunctionInfo describes the JSFunction information that can be
// shared by multiple instances of the function.
class SharedFunctionInfo: public HeapObject {
 public:
  // [name]: Function name.
  DECL_ACCESSORS(name, Object)

  // [code]: Function code.
  DECL_ACCESSORS(code, Code)

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled();

  // [length]: The function length - usually the number of declared parameters.
  // Use up to 2^30 parameters.
  inline int length();
  inline void set_length(int value);

  // [formal parameter count]: The declared number of parameters.
  inline int formal_parameter_count();
  inline void set_formal_parameter_count(int value);

  // [expected_nof_properties]: Expected number of properties for the function.
  inline int expected_nof_properties();
  inline void set_expected_nof_properties(int value);

  // [instance class name]: class name for instances.
  DECL_ACCESSORS(instance_class_name, Object)

  // [function data]: This field has been added for make benefit the API.
  // In the long run we don't want all functions to have this field but
  // we can fix that when we have a better model for storing hidden data
  // on objects.
  DECL_ACCESSORS(function_data, Object)

  // [lazy load data]: If the function has lazy loading, this field
  // contains contexts and other data needed to load it.
  DECL_ACCESSORS(lazy_load_data, Object)

  // [script info]: Script from which the function originates.
  DECL_ACCESSORS(script, Object)

  // [start_position_and_type]: Field used to store both the source code
  // position, whether or not the function is a function expression,
  // and whether or not the function is a toplevel function. The two
  // least significants bit indicates whether the function is an
  // expression and the rest contains the source code position.
  inline int start_position_and_type();
  inline void set_start_position_and_type(int value);

  // [debug info]: Debug information.
  DECL_ACCESSORS(debug_info, Object)

  // Position of the 'function' token in the script source.
  inline int function_token_position();
  inline void set_function_token_position(int function_token_position);

  // Position of this function in the script source.
  inline int start_position();
  inline void set_start_position(int start_position);

  // End position of this function in the script source.
  inline int end_position();
  inline void set_end_position(int end_position);

  // Is this function a function expression in the source code.
  inline bool is_expression();
  inline void set_is_expression(bool value);

  // Is this function a top-level function. Used for accessing the
  // caller of functions. Top-level functions (scripts, evals) are
  // returned as null; see JSFunction::GetCallerAccessor(...).
  inline bool is_toplevel();
  inline void set_is_toplevel(bool value);

  // [source code]: Source code for the function.
  bool HasSourceCode();
  Object* GetSourceCode();

  // Dispatched behavior.
  void SharedFunctionInfoIterateBody(ObjectVisitor* v);
  // Set max_length to -1 for unlimited length.
  void SourceCodePrint(StringStream* accumulator, int max_length);
#ifdef DEBUG
  void SharedFunctionInfoPrint();
  void SharedFunctionInfoVerify();
#endif

  // Casting.
  static inline SharedFunctionInfo* cast(Object* obj);

  // Layout description.
  static const int kNameOffset = HeapObject::kSize;
  static const int kCodeOffset = kNameOffset + kPointerSize;
  static const int kLengthOffset = kCodeOffset + kPointerSize;
  static const int kFormalParameterCountOffset = kLengthOffset + kIntSize;
  static const int kExpectedNofPropertiesOffset =
      kFormalParameterCountOffset + kIntSize;
  static const int kInstanceClassNameOffset =
      kExpectedNofPropertiesOffset + kIntSize;
  static const int kExternalReferenceDataOffset =
      kInstanceClassNameOffset + kPointerSize;
  static const int kLazyLoadDataOffset =
      kExternalReferenceDataOffset + kPointerSize;
  static const int kScriptOffset = kLazyLoadDataOffset + kPointerSize;
  static const int kStartPositionAndTypeOffset = kScriptOffset + kPointerSize;
  static const int kEndPositionOffset = kStartPositionAndTypeOffset + kIntSize;
  static const int kFunctionTokenPositionOffset = kEndPositionOffset + kIntSize;
  static const int kDebugInfoOffset = kFunctionTokenPositionOffset + kIntSize;
  static const int kAccessAttributesOffset = kDebugInfoOffset + kPointerSize;
  static const int kSize = kAccessAttributesOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SharedFunctionInfo);

  // Bit positions in length_and_flg.
  // The least significant bit is used as the flag.
  static const int kFlagBit         = 0;
  static const int kLengthShift     = 1;
  static const int kLengthMask      = ~((1 << kLengthShift) - 1);

  // Bit positions in start_position_and_type.
  // The source code start position is in the 30 most significant bits of
  // the start_position_and_type field.
  static const int kIsExpressionBit = 0;
  static const int kIsTopLevelBit   = 1;
  static const int kStartPositionShift = 2;
  static const int kStartPositionMask = ~((1 << kStartPositionShift) - 1);
};


// JSFunction describes JavaScript functions.
class JSFunction: public JSObject {
 public:
  // [prototype_or_initial_map]:
  DECL_ACCESSORS(prototype_or_initial_map, Object)

  // [shared_function_info]: The information about the function that
  // can be shared by instances.
  DECL_ACCESSORS(shared, SharedFunctionInfo)

  // [context]: The context for this function.
  inline Context* context();
  inline Object* unchecked_context();
  inline void set_context(Object* context);

  // [code]: The generated code object for this function.  Executed
  // when the function is invoked, e.g. foo() or new foo(). See
  // [[Call]] and [[Construct]] description in ECMA-262, section
  // 8.6.2, page 27.
  inline Code* code();
  inline void set_code(Code* value);

  // Tells whether this function is a context-independent boilerplate
  // function.
  inline bool IsBoilerplate();

  // Tells whether this function needs to be loaded.
  inline bool IsLoaded();

  // [literals]: Fixed array holding the materialized literals.
  DECL_ACCESSORS(literals, FixedArray)

  // The initial map for an object created by this constructor.
  inline Map* initial_map();
  inline void set_initial_map(Map* value);
  inline bool has_initial_map();

  // Get and set the prototype property on a JSFunction. If the
  // function has an initial map the prototype is set on the initial
  // map. Otherwise, the prototype is put in the initial map field
  // until an initial map is needed.
  inline bool has_prototype();
  inline bool has_instance_prototype();
  inline Object* prototype();
  inline Object* instance_prototype();
  Object* SetInstancePrototype(Object* value);
  Object* SetPrototype(Object* value);

  // Accessor for this function's initial map's [[class]]
  // property. This is primarily used by ECMA native functions.  This
  // method sets the class_name field of this function's initial map
  // to a given value. It creates an initial map if this function does
  // not have one. Note that this method does not copy the initial map
  // if it has one already, but simply replaces it with the new value.
  // Instances created afterwards will have a map whose [[class]] is
  // set to 'value', but there is no guarantees on instances created
  // before.
  Object* SetInstanceClassName(String* name);

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled();

  // Casting.
  static inline JSFunction* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSFunctionPrint();
  void JSFunctionVerify();
#endif

  // Returns the number of allocated literals.
  int NumberOfLiterals();

  // Layout descriptors.
  static const int kPrototypeOrInitialMapOffset = JSObject::kHeaderSize;
  static const int kSharedFunctionInfoOffset =
      kPrototypeOrInitialMapOffset + kPointerSize;
  static const int kContextOffset = kSharedFunctionInfoOffset + kPointerSize;
  static const int kLiteralsOffset = kContextOffset + kPointerSize;
  static const int kSize = kLiteralsOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSFunction);
};


// Forward declaration.
class JSBuiltinsObject;

// Common super class for JavaScript global objects and the special
// builtins global objects.
class GlobalObject: public JSObject {
 public:
  // [builtins]: the object holding the runtime routines written in JS.
  DECL_ACCESSORS(builtins, JSBuiltinsObject)

  // [global context]: the global context corresponding to this global objet.
  DECL_ACCESSORS(global_context, Context)

  // Layout description.
  static const int kBuiltinsOffset = JSObject::kHeaderSize;
  static const int kGlobalContextOffset = kBuiltinsOffset + kPointerSize;
  static const int kHeaderSize = kGlobalContextOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(GlobalObject);
  friend class AGCCVersionRequiresThisClassToHaveAFriendSoHereItIs;
};


// JavaScript global object.
class JSGlobalObject: public GlobalObject {
 public:
  // [security token]: the object being used for security check when accessing
  // global properties.
  DECL_ACCESSORS(security_token, Object)

  // Casting.
  static inline JSGlobalObject* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSGlobalObjectPrint();
  void JSGlobalObjectVerify();
#endif

  // Layout description.
  static const int kSecurityTokenOffset = GlobalObject::kHeaderSize;
  static const int kSize = kSecurityTokenOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSGlobalObject);
};


// Builtins global object which holds the runtime routines written in
// JavaScript.
class JSBuiltinsObject: public GlobalObject {
 public:
  // Accessors for the runtime routines written in JavaScript.
  inline Object* javascript_builtin(Builtins::JavaScript id);
  inline void set_javascript_builtin(Builtins::JavaScript id, Object* value);

  // Casting.
  static inline JSBuiltinsObject* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSBuiltinsObjectPrint();
  void JSBuiltinsObjectVerify();
#endif

  // Layout description.  The size of the builtins object includes
  // room for one pointer per runtime routine written in javascript.
  static const int kJSBuiltinsCount = Builtins::id_count;
  static const int kJSBuiltinsOffset = GlobalObject::kHeaderSize;
  static const int kSize =
      kJSBuiltinsOffset + (kJSBuiltinsCount * kPointerSize);
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSBuiltinsObject);
};


// Representation for JS Wrapper objects, String, Number, Boolean, Date, etc.
class JSValue: public JSObject {
 public:
  // [value]: the object being wrapped.
  DECL_ACCESSORS(value, Object)

  // Casting.
  static inline JSValue* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSValuePrint();
  void JSValueVerify();
#endif

  // Layout description.
  static const int kValueOffset = JSObject::kHeaderSize;
  static const int kSize = kValueOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSValue);
};


enum AllowNullsFlag {ALLOW_NULLS, DISALLOW_NULLS};
enum RobustnessFlag {ROBUST_STRING_TRAVERSAL, FAST_STRING_TRAVERSAL};


// The String abstract class captures JavaScript string values:
//
// Ecma-262:
//  4.3.16 String Value
//    A string value is a member of the type String and is a finite
//    ordered sequence of zero or more 16-bit unsigned integer values.
//
// All string values have a length field.
class String: public HeapObject {
 public:
  // Get and set the length of the string.
  inline int length();
  inline void set_length(int value);

  // Get and set the uninterpreted length field of the string.  Notice
  // that the length field is also used to cache the hash value of
  // strings.  In order to get or set the actual length of the string
  // use the length() and set_length methods.
  inline int length_field();
  inline void set_length_field(int value);

  // Get and set individual two byte chars in the string.
  inline void Set(int index, uint16_t value);
  // Get individual two byte char in the string.  Repeated calls
  // to this method are not efficient unless the string is flat.
  inline uint16_t Get(int index);

  // Flatten the top level ConsString that is hiding behind this
  // string.  This is a no-op unless the string is a ConsString or a
  // SlicedString.  Flatten mutates the ConsString and might return a
  // failure.
  Object* Flatten();
  // Try to flatten the string.  Do not allow handling of allocation
  // failures.  After calling TryFlatten, the string could still be a
  // ConsString.
  inline void TryFlatten();

  // Is this string an ascii string.
  inline bool IsAscii();

  // Fast testing routines that assume the receiver is a string and
  // just check whether it is a certain kind of string.
  inline bool StringIsSlicedString();
  inline bool StringIsConsString();

  // Mark the string as an undetectable object. It only applies to
  // ascii and two byte string types.
  bool MarkAsUndetectable();

  // Slice the string and return a substring.
  Object* Slice(int from, int to);

  // String equality operations.
  inline bool Equals(String* other);
  bool IsEqualTo(Vector<const char> str);

  // Return a UTF8 representation of the string.  The string is null
  // terminated but may optionally contain nulls.  Length is returned
  // in length_output if length_output is not a null pointer  The string
  // should be nearly flat, otherwise the performance of this method may
  // be very slow (quadratic in the length).  Setting robustness_flag to
  // ROBUST_STRING_TRAVERSAL invokes behaviour that is robust  This means it
  // handles unexpected data without causing assert failures and it does not
  // do any heap allocations.  This is useful when printing stack traces.
  SmartPointer<char> ToCString(AllowNullsFlag allow_nulls,
                               RobustnessFlag robustness_flag,
                               int offset,
                               int length,
                               int* length_output = 0);
  SmartPointer<char> ToCString(
      AllowNullsFlag allow_nulls = DISALLOW_NULLS,
      RobustnessFlag robustness_flag = FAST_STRING_TRAVERSAL,
      int* length_output = 0);

  // Return a 16 bit Unicode representation of the string.
  // The string should be nearly flat, otherwise the performance of
  // of this method may be very bad.  Setting robustness_flag to
  // ROBUST_STRING_TRAVERSAL invokes behaviour that is robust  This means it
  // handles unexpected data without causing assert failures and it does not
  // do any heap allocations.  This is useful when printing stack traces.
  uc16* ToWideCString(RobustnessFlag robustness_flag = FAST_STRING_TRAVERSAL);

  // Tells whether the hash code has been computed.
  inline bool HasHashCode();

  // Returns a hash value used for the property table
  inline uint32_t Hash();

  static uint32_t ComputeHashCode(unibrow::CharacterStream* buffer, int length);
  static bool ComputeArrayIndex(unibrow::CharacterStream* buffer,
                                uint32_t* index,
                                int length);

  // Conversion.
  inline bool AsArrayIndex(uint32_t* index);

  // Casting.
  static inline String* cast(Object* obj);

  void PrintOn(FILE* out);

  // Get the size tag.
  inline uint32_t size_tag();
  static inline uint32_t map_size_tag(Map* map);

  // True if the string is a symbol.
  inline bool is_symbol();
  static inline bool is_symbol_map(Map* map);

  // True if the string is ASCII.
  inline bool is_ascii();
  static inline bool is_ascii_map(Map* map);

  // Get the representation tag.
  inline StringRepresentationTag representation_tag();
  static inline StringRepresentationTag map_representation_tag(Map* map);

  // For use during stack traces.  Performs rudimentary sanity check.
  bool LooksValid();

  // Dispatched behavior.
  void StringShortPrint(StringStream* accumulator);
#ifdef DEBUG
  void StringPrint();
  void StringVerify();
#endif
  inline bool IsFlat();

  // Layout description.
  static const int kLengthOffset = HeapObject::kSize;
  static const int kSize = kLengthOffset + kIntSize;

  // Limits on sizes of different types of strings.
  static const int kMaxShortStringSize = 255;
  static const int kMaxMediumStringSize = 65535;

  // Max ascii char code.
  static const int kMaxAsciiCharCode = 127;

  // Shift constants for retriving length from length/hash field.
  static const int kShortLengthShift = 3 * kBitsPerByte;
  static const int kMediumLengthShift = 2 * kBitsPerByte;
  static const int kLongLengthShift = 2;

  // Mask constant for checking if a string has a computed hash code
  // and if it is an array index.  The least significant bit indicates
  // whether a hash code has been computed.  If the hash code has been
  // computed the 2nd bit tells whether the string can be used as an
  // array index.
  static const int kHashComputedMask = 1;
  static const int kIsArrayIndexMask = 1 << 1;

  // Support for regular expressions.
  const uc16* GetTwoByteData();
  const uc16* GetTwoByteData(unsigned start);

  // Support for StringInputBuffer
  static const unibrow::byte* ReadBlock(String* input,
                                        unibrow::byte* util_buffer,
                                        unsigned capacity,
                                        unsigned* remaining,
                                        unsigned* offset);
  static const unibrow::byte* ReadBlock(String** input,
                                        unibrow::byte* util_buffer,
                                        unsigned capacity,
                                        unsigned* remaining,
                                        unsigned* offset);

  // Helper function for flattening strings.
  static void Flatten(String* source,
                      String* sink,
                      int from,
                      int to,
                      int sink_offset);

 protected:
  class ReadBlockBuffer {
   public:
    ReadBlockBuffer(unibrow::byte* util_buffer_,
                    unsigned cursor_,
                    unsigned capacity_,
                    unsigned remaining_) :
      util_buffer(util_buffer_),
      cursor(cursor_),
      capacity(capacity_),
      remaining(remaining_) {
    }
    unibrow::byte* util_buffer;
    unsigned       cursor;
    unsigned       capacity;
    unsigned       remaining;
  };

  // NOTE: If you call StringInputBuffer routines on strings that are
  // too deeply nested trees of cons and slice strings, then this
  // routine will overflow the stack. Strings that are merely deeply
  // nested trees of cons strings do not have a problem apart from
  // performance.

  static inline const unibrow::byte* ReadBlock(String* input,
                                               ReadBlockBuffer* buffer,
                                               unsigned* offset,
                                               unsigned max_chars);
  static void ReadBlockIntoBuffer(String* input,
                                  ReadBlockBuffer* buffer,
                                  unsigned* offset_ptr,
                                  unsigned max_chars);

 private:
  // Slow case of String::Equals.  This implementation works on any strings
  // but it is most efficient on strings that are almost flat.
  bool SlowEquals(String* other);

  // Slow case of AsArrayIndex.
  bool SlowAsArrayIndex(uint32_t* index);

  // Compute and set the hash code.
  uint32_t ComputeAndSetHash();

  DISALLOW_IMPLICIT_CONSTRUCTORS(String);
};


// The SeqString abstract class captures sequential string values.
class SeqString: public String {
 public:

  // Casting.
  static inline SeqString* cast(Object* obj);

  // Dispatched behaviour.
  // For regexp code.
  uint16_t* SeqStringGetTwoByteAddress();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SeqString);
};


// The AsciiString class captures sequential ascii string objects.
// Each character in the AsciiString is an ascii character.
class AsciiString: public SeqString {
 public:
  // Dispatched behavior.
  inline uint16_t AsciiStringGet(int index);
  inline void AsciiStringSet(int index, uint16_t value);

  // Get the address of the characters in this string.
  inline Address GetCharsAddress();

  // Casting
  static inline AsciiString* cast(Object* obj);

  // Garbage collection support.  This method is called by the
  // garbage collector to compute the actual size of an AsciiString
  // instance.
  inline int AsciiStringSize(Map* map);

  // Computes the size for an AsciiString instance of a given length.
  static int SizeFor(int length) {
    return kHeaderSize + OBJECT_SIZE_ALIGN(length * kCharSize);
  }

  // Layout description.
  static const int kHeaderSize = String::kSize;

  // Support for StringInputBuffer.
  inline void AsciiStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                             unsigned* offset,
                                             unsigned chars);
  inline const unibrow::byte* AsciiStringReadBlock(unsigned* remaining,
                                                   unsigned* offset,
                                                   unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AsciiString);
};


// The TwoByteString class captures sequential unicode string objects.
// Each character in the TwoByteString is a two-byte uint16_t.
class TwoByteString: public SeqString {
 public:
  // Dispatched behavior.
  inline uint16_t TwoByteStringGet(int index);
  inline void TwoByteStringSet(int index, uint16_t value);

  // For regexp code.
  const uint16_t* TwoByteStringGetData(unsigned start);

  // Casting
  static inline TwoByteString* cast(Object* obj);

  // Garbage collection support.  This method is called by the
  // garbage collector to compute the actual size of a TwoByteString
  // instance.
  inline int TwoByteStringSize(Map* map);

  // Computes the size for a TwoByteString instance of a given length.
  static int SizeFor(int length) {
    return kHeaderSize + OBJECT_SIZE_ALIGN(length * kShortSize);
  }

  // Layout description.
  static const int kHeaderSize = String::kSize;

  // Support for StringInputBuffer.
  inline void TwoByteStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                               unsigned* offset_ptr,
                                               unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(TwoByteString);
};


// The ConsString class describes string values built by using the
// addition operator on strings.  A ConsString is a pair where the
// first and second components are pointers to other string values.
// One or both components of a ConsString can be pointers to other
// ConsStrings, creating a binary tree of ConsStrings where the leaves
// are non-ConsString string values.  The string value represented by
// a ConsString can be obtained by concatenating the leaf string
// values in a left-to-right depth-first traversal of the tree.
class ConsString: public String {
 public:
  // First object of the cons cell.
  inline Object* first();
  inline void set_first(Object* first);

  // Second object of the cons cell.
  inline Object* second();
  inline void set_second(Object* second);

  // Dispatched behavior.
  uint16_t ConsStringGet(int index);

  // Casting.
  static inline ConsString* cast(Object* obj);

  // Garbage collection support.  This method is called during garbage
  // collection to iterate through the heap pointers in the body of
  // the ConsString.
  void ConsStringIterateBody(ObjectVisitor* v);

  // Layout description.
  static const int kFirstOffset = String::kSize;
  static const int kSecondOffset = kFirstOffset + kPointerSize;
  static const int kSize = kSecondOffset + kPointerSize;

  // Support for StringInputBuffer.
  inline const unibrow::byte* ConsStringReadBlock(ReadBlockBuffer* buffer,
                                                  unsigned* offset_ptr,
                                                  unsigned chars);
  inline void ConsStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                            unsigned* offset_ptr,
                                            unsigned chars);


  // Minimum lenth for a cons string.
  static const int kMinLength = 13;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ConsString);
};


// The SlicedString class describes string values that are slices of
// some other string.  SlicedStrings consist of a reference to an
// underlying heap-allocated string value, a start index, and the
// length field common to all strings.
class SlicedString: public String {
 public:
  // The underlying string buffer.
  inline Object* buffer();
  inline void set_buffer(Object* buffer);

  // The start index of the slice.
  inline int start();
  inline void set_start(int start);

  // Dispatched behavior.
  uint16_t SlicedStringGet(int index);

  // Flatten any ConsString hiding behind this SlicedString.
  Object* SlicedStringFlatten();

  // Casting.
  static inline SlicedString* cast(Object* obj);

  // Garbage collection support.
  void SlicedStringIterateBody(ObjectVisitor* v);

  // Layout description
  static const int kBufferOffset = String::kSize;
  static const int kStartOffset = kBufferOffset + kPointerSize;
  static const int kSize = kStartOffset + kIntSize;

  // Support for StringInputBuffer.
  inline const unibrow::byte* SlicedStringReadBlock(ReadBlockBuffer* buffer,
                                                    unsigned* offset_ptr,
                                                    unsigned chars);
  inline void SlicedStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                              unsigned* offset_ptr,
                                              unsigned chars);

  // Minimum lenth for a sliced string.
  static const int kMinLength = 13;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SlicedString);
};


// The ExternalString class describes string values that are backed by
// a string resource that lies outside the V8 heap.  ExternalStrings
// consist of the length field common to all strings, a pointer to the
// external resource.  It is important to ensure (externally) that the
// resource is not deallocated while the ExternalString is live in the
// V8 heap.
//
// The API expects that all ExternalStrings are created through the
// API.  Therefore, ExternalStrings should not be used internally.
class ExternalString: public String {
 public:
  // Casting
  static inline ExternalString* cast(Object* obj);

  // Layout description.
  static const int kResourceOffset = String::kSize;
  static const int kSize = kResourceOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalString);
};


// The ExternalAsciiString class is an external string backed by an
// ASCII string.
class ExternalAsciiString: public ExternalString {
 public:
  typedef v8::String::ExternalAsciiStringResource Resource;

  // The underlying resource.
  inline Resource* resource();
  inline void set_resource(Resource* buffer);

  // Dispatched behavior.
  uint16_t ExternalAsciiStringGet(int index);

  // Casting.
  static inline ExternalAsciiString* cast(Object* obj);

  // Support for StringInputBuffer.
  const unibrow::byte* ExternalAsciiStringReadBlock(unsigned* remaining,
                                                    unsigned* offset,
                                                    unsigned chars);
  inline void ExternalAsciiStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                     unsigned* offset,
                                                     unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalAsciiString);
};


// The ExternalTwoByteString class is an external string backed by a UTF-16
// encoded string.
class ExternalTwoByteString: public ExternalString {
 public:
  typedef v8::String::ExternalStringResource Resource;

  // The underlying string resource.
  inline Resource* resource();
  inline void set_resource(Resource* buffer);

  // Dispatched behavior.
  uint16_t ExternalTwoByteStringGet(int index);

  // For regexp code.
  const uint16_t* ExternalTwoByteStringGetData(unsigned start);

  // Casting.
  static inline ExternalTwoByteString* cast(Object* obj);

  // Support for StringInputBuffer.
  void ExternalTwoByteStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                unsigned* offset_ptr,
                                                unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalTwoByteString);
};


// Note that StringInputBuffers are not valid across a GC!  To fix this
// it would have to store a String Handle instead of a String* and
// AsciiStringReadBlock would have to be modified to use memcpy.
//
// StringInputBuffer is able to traverse any string regardless of how
// deeply nested a sequence of ConsStrings it is made of.  However,
// performance will be better if deep strings are flattened before they
// are traversed.  Since flattening requires memory allocation this is
// not always desirable, however (esp. in debugging situations).
class StringInputBuffer: public unibrow::InputBuffer<String, String*, 1024> {
 public:
  virtual void Seek(unsigned pos);
  inline StringInputBuffer(): unibrow::InputBuffer<String, String*, 1024>() {}
  inline StringInputBuffer(String* backing):
      unibrow::InputBuffer<String, String*, 1024>(backing) {}
};


class SafeStringInputBuffer
  : public unibrow::InputBuffer<String, String**, 256> {
 public:
  virtual void Seek(unsigned pos);
  inline SafeStringInputBuffer()
      : unibrow::InputBuffer<String, String**, 256>() {}
  inline SafeStringInputBuffer(String** backing)
      : unibrow::InputBuffer<String, String**, 256>(backing) {}
};


// The Oddball describes objects null, undefined, true, and false.
class Oddball: public HeapObject {
 public:
  // [to_string]: Cached to_string computed at startup.
  DECL_ACCESSORS(to_string, String)

  // [to_number]: Cached to_number computed at startup.
  DECL_ACCESSORS(to_number, Object)

  // Casting.
  static inline Oddball* cast(Object* obj);

  // Dispatched behavior.
  void OddballIterateBody(ObjectVisitor* v);
#ifdef DEBUG
  void OddballVerify();
#endif

  // Initialize the fields.
  Object* Initialize(const char* to_string, Object* to_number);

  // Layout description.
  static const int kToStringOffset = HeapObject::kSize;
  static const int kToNumberOffset = kToStringOffset + kPointerSize;
  static const int kSize = kToNumberOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Oddball);
};


// Proxy describes objects pointing from JavaScript to C structures.
class Proxy: public HeapObject {
 public:
  // [proxy]: field containing the address.
  inline Address proxy();
  inline void set_proxy(Address value);

  // Casting.
  static inline Proxy* cast(Object* obj);

  // Dispatched behavior.
  inline void ProxyIterateBody(ObjectVisitor* v);
#ifdef DEBUG
  void ProxyPrint();
  void ProxyVerify();
#endif

  // Layout description.

  static const int kProxyOffset = HeapObject::kSize;
  static const int kSize = kProxyOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Proxy);
};


// The JSArray describes JavaScript Arrays
//  Such an array can be in one of two modes:
//    - fast, backing storage is a FixedArray and length <= elements.length();
//       Please note: push and pop can be used to grow and shrink the array.
//    - slow, backing storage is a HashTable with numbers as keys.
class JSArray: public JSObject {
 public:
  // [length]: The length property.
  DECL_ACCESSORS(length, Object)

  Object* JSArrayUpdateLengthFromIndex(uint32_t index, Object* value);

  // Initialize the array with the given capacity. The function may
  // fail due to out-of-memory situations, but only if the requested
  // capacity is non-zero.
  Object* Initialize(int capacity);

  // Set the content of the array to the content of storage.
  void SetContent(FixedArray* storage);

  // Support for sorting
  Object* RemoveHoles();

  // Casting.
  static inline JSArray* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSArrayPrint();
  void JSArrayVerify();
#endif

  // Layout description.
  static const int kLengthOffset = JSObject::kHeaderSize;
  static const int kSize = kLengthOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSArray);
};


// An accesor must have a getter, but can have no setter.
//
// When setting a property, V8 searches accessors in prototypes.
// If an accessor was found and it does not have a setter,
// the request is ignored.
//
// To allow shadow an accessor property, the accessor can
// have READ_ONLY property attribute so that a new value
// is added to the local object to shadow the accessor
// in prototypes.
class AccessorInfo: public Struct {
 public:
  DECL_ACCESSORS(getter, Object)
  DECL_ACCESSORS(setter, Object)
  DECL_ACCESSORS(data, Object)
  DECL_ACCESSORS(name, Object)
  DECL_ACCESSORS(flag, Smi)

  inline bool all_can_read();
  inline void set_all_can_read(bool value);

  inline bool all_can_write();
  inline void set_all_can_write(bool value);

  inline PropertyAttributes property_attributes();
  inline void set_property_attributes(PropertyAttributes attributes);

  static inline AccessorInfo* cast(Object* obj);

#ifdef DEBUG
  void AccessorInfoPrint();
  void AccessorInfoVerify();
#endif

  static const int kGetterOffset = HeapObject::kSize;
  static const int kSetterOffset = kGetterOffset + kPointerSize;
  static const int kDataOffset = kSetterOffset + kPointerSize;
  static const int kNameOffset = kDataOffset + kPointerSize;
  static const int kFlagOffset = kNameOffset + kPointerSize;
  static const int kSize = kFlagOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AccessorInfo);

  // Bit positions in flag.
  static const int kAllCanReadBit  = 0;
  static const int kAllCanWriteBit = 1;
  class AttributesField: public BitField<PropertyAttributes, 2, 3> {};
};


class AccessCheckInfo: public Struct {
 public:
  DECL_ACCESSORS(named_callback, Object)
  DECL_ACCESSORS(indexed_callback, Object)
  DECL_ACCESSORS(data, Object)

  static inline AccessCheckInfo* cast(Object* obj);

#ifdef DEBUG
  void AccessCheckInfoPrint();
  void AccessCheckInfoVerify();
#endif

  static const int kNamedCallbackOffset   = HeapObject::kSize;
  static const int kIndexedCallbackOffset = kNamedCallbackOffset + kPointerSize;
  static const int kDataOffset = kIndexedCallbackOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AccessCheckInfo);
};


class InterceptorInfo: public Struct {
 public:
  DECL_ACCESSORS(getter, Object)
  DECL_ACCESSORS(setter, Object)
  DECL_ACCESSORS(query, Object)
  DECL_ACCESSORS(deleter, Object)
  DECL_ACCESSORS(enumerator, Object)
  DECL_ACCESSORS(data, Object)

  static inline InterceptorInfo* cast(Object* obj);

#ifdef DEBUG
  void InterceptorInfoPrint();
  void InterceptorInfoVerify();
#endif

  static const int kGetterOffset = HeapObject::kSize;
  static const int kSetterOffset = kGetterOffset + kPointerSize;
  static const int kQueryOffset = kSetterOffset + kPointerSize;
  static const int kDeleterOffset = kQueryOffset + kPointerSize;
  static const int kEnumeratorOffset = kDeleterOffset + kPointerSize;
  static const int kDataOffset = kEnumeratorOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(InterceptorInfo);
};


class CallHandlerInfo: public Struct {
 public:
  DECL_ACCESSORS(callback, Object)
  DECL_ACCESSORS(data, Object)

  static inline CallHandlerInfo* cast(Object* obj);

#ifdef DEBUG
  void CallHandlerInfoPrint();
  void CallHandlerInfoVerify();
#endif

  static const int kCallbackOffset = HeapObject::kSize;
  static const int kDataOffset = kCallbackOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CallHandlerInfo);
};


class TemplateInfo: public Struct {
 public:
  DECL_ACCESSORS(tag, Object)
  DECL_ACCESSORS(property_list, Object)

#ifdef DEBUG
  void TemplateInfoVerify();
#endif

  static const int kTagOffset          = HeapObject::kSize;
  static const int kPropertyListOffset = kTagOffset + kPointerSize;
  static const int kHeaderSize         = kPropertyListOffset + kPointerSize;
 protected:
  friend class AGCCVersionRequiresThisClassToHaveAFriendSoHereItIs;
  DISALLOW_IMPLICIT_CONSTRUCTORS(TemplateInfo);
};


class FunctionTemplateInfo: public TemplateInfo {
 public:
  DECL_ACCESSORS(serial_number, Object)
  DECL_ACCESSORS(call_code, Object)
  DECL_ACCESSORS(property_accessors, Object)
  DECL_ACCESSORS(prototype_template, Object)
  DECL_ACCESSORS(parent_template, Object)
  DECL_ACCESSORS(named_property_handler, Object)
  DECL_ACCESSORS(indexed_property_handler, Object)
  DECL_ACCESSORS(instance_template, Object)
  DECL_ACCESSORS(class_name, Object)
  DECL_ACCESSORS(signature, Object)
  DECL_ACCESSORS(lookup_callback, Object)
  DECL_ACCESSORS(instance_call_handler, Object)
  DECL_ACCESSORS(access_check_info, Object)
  DECL_ACCESSORS(flag, Smi)

  // Following properties use flag bits.
  DECL_BOOLEAN_ACCESSORS(hidden_prototype)
  DECL_BOOLEAN_ACCESSORS(undetectable)
  // If the bit is set, object instances created by this function
  // requires access check.
  DECL_BOOLEAN_ACCESSORS(needs_access_check)

  static inline FunctionTemplateInfo* cast(Object* obj);

#ifdef DEBUG
  void FunctionTemplateInfoPrint();
  void FunctionTemplateInfoVerify();
#endif

  static const int kSerialNumberOffset = TemplateInfo::kHeaderSize;
  static const int kCallCodeOffset = kSerialNumberOffset + kPointerSize;
  static const int kPropertyAccessorsOffset = kCallCodeOffset + kPointerSize;
  static const int kPrototypeTemplateOffset =
      kPropertyAccessorsOffset + kPointerSize;
  static const int kParentTemplateOffset =
      kPrototypeTemplateOffset + kPointerSize;
  static const int kNamedPropertyHandlerOffset =
      kParentTemplateOffset + kPointerSize;
  static const int kIndexedPropertyHandlerOffset =
      kNamedPropertyHandlerOffset + kPointerSize;
  static const int kInstanceTemplateOffset =
      kIndexedPropertyHandlerOffset + kPointerSize;
  static const int kClassNameOffset = kInstanceTemplateOffset + kPointerSize;
  static const int kSignatureOffset = kClassNameOffset + kPointerSize;
  static const int kLookupCallbackOffset = kSignatureOffset + kPointerSize;
  static const int kInstanceCallHandlerOffset =
      kLookupCallbackOffset + kPointerSize;
  static const int kAccessCheckInfoOffset =
      kInstanceCallHandlerOffset + kPointerSize;
  static const int kFlagOffset = kAccessCheckInfoOffset + kPointerSize;
  static const int kSize = kFlagOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(FunctionTemplateInfo);

  // Bit position in the flag, from least significant bit position.
  static const int kHiddenPrototypeBit   = 0;
  static const int kUndetectableBit      = 1;
  static const int kNeedsAccessCheckBit  = 2;
};


class ObjectTemplateInfo: public TemplateInfo {
 public:
  DECL_ACCESSORS(constructor, Object)
  DECL_ACCESSORS(internal_field_count, Object)

  static inline ObjectTemplateInfo* cast(Object* obj);

#ifdef DEBUG
  void ObjectTemplateInfoPrint();
  void ObjectTemplateInfoVerify();
#endif

  static const int kConstructorOffset = TemplateInfo::kHeaderSize;
  static const int kInternalFieldCountOffset =
      kConstructorOffset + kPointerSize;
  static const int kSize = kInternalFieldCountOffset + kHeaderSize;
};


class SignatureInfo: public Struct {
 public:
  DECL_ACCESSORS(receiver, Object)
  DECL_ACCESSORS(args, Object)

  static inline SignatureInfo* cast(Object* obj);

#ifdef DEBUG
  void SignatureInfoPrint();
  void SignatureInfoVerify();
#endif

  static const int kReceiverOffset = Struct::kSize;
  static const int kArgsOffset     = kReceiverOffset + kPointerSize;
  static const int kSize           = kArgsOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SignatureInfo);
};


class TypeSwitchInfo: public Struct {
 public:
  DECL_ACCESSORS(types, Object)

  static inline TypeSwitchInfo* cast(Object* obj);

#ifdef DEBUG
  void TypeSwitchInfoPrint();
  void TypeSwitchInfoVerify();
#endif

  static const int kTypesOffset = Struct::kSize;
  static const int kSize        = kTypesOffset + kPointerSize;
};


// The DebugInfo class holds additional information for a function beeing
// debugged.
class DebugInfo: public Struct {
 public:
  // The shared function info for the source beeing debugged.
  DECL_ACCESSORS(shared, SharedFunctionInfo)
  // Code object for the original code.
  DECL_ACCESSORS(original_code, Code)
  // Code object for the patched code. This code object is the code object
  // currently active for the function.
  DECL_ACCESSORS(code, Code)
  // Fixed array holding status information for each active break point.
  DECL_ACCESSORS(break_points, FixedArray)

  // Check if there is a break point at a code position.
  bool HasBreakPoint(int code_position);
  // Get the break point info object for a code position.
  Object* GetBreakPointInfo(int code_position);
  // Clear a break point.
  static void ClearBreakPoint(Handle<DebugInfo> debug_info,
                              int code_position,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<DebugInfo> debug_info, int code_position,
                            int source_position, int statement_position,
                            Handle<Object> break_point_object);
  // Get the break point objects for a code position.
  Object* GetBreakPointObjects(int code_position);
  // Find the break point info holding this break point object.
  static Object* FindBreakPointInfo(Handle<DebugInfo> debug_info,
                                    Handle<Object> break_point_object);
  // Get the number of break points for this function.
  int GetBreakPointCount();

  static inline DebugInfo* cast(Object* obj);

#ifdef DEBUG
  void DebugInfoPrint();
  void DebugInfoVerify();
#endif

  static const int kSharedFunctionInfoIndex = Struct::kSize;
  static const int kOriginalCodeIndex = kSharedFunctionInfoIndex + kPointerSize;
  static const int kPatchedCodeIndex = kOriginalCodeIndex + kPointerSize;
  static const int kActiveBreakPointsCountIndex =
      kPatchedCodeIndex + kPointerSize;
  static const int kBreakPointsStateIndex =
      kActiveBreakPointsCountIndex + kPointerSize;
  static const int kSize = kBreakPointsStateIndex + kPointerSize;

 private:
  static const int kNoBreakPointInfo = -1;

  // Lookup the index in the break_points array for a code position.
  int GetBreakPointInfoIndex(int code_position);

  DISALLOW_IMPLICIT_CONSTRUCTORS(DebugInfo);
};


// The BreakPointInfo class holds information for break points set in a
// function. The DebugInfo object holds a BreakPointInfo object for each code
// position with one or more break points.
class BreakPointInfo: public Struct {
 public:
  // The position in the code for the break point.
  DECL_ACCESSORS(code_position, Smi)
  // The position in the source for the break position.
  DECL_ACCESSORS(source_position, Smi)
  // The position in the source for the last statement before this break
  // position.
  DECL_ACCESSORS(statement_position, Smi)
  // List of related JavaScript break points.
  DECL_ACCESSORS(break_point_objects, Object)

  // Removes a break point.
  static void ClearBreakPoint(Handle<BreakPointInfo> info,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<BreakPointInfo> info,
                            Handle<Object> break_point_object);
  // Check if break point info has this break point object.
  static bool HasBreakPointObject(Handle<BreakPointInfo> info,
                                  Handle<Object> break_point_object);
  // Get the number of break points for this code position.
  int GetBreakPointCount();

  static inline BreakPointInfo* cast(Object* obj);

#ifdef DEBUG
  void BreakPointInfoPrint();
  void BreakPointInfoVerify();
#endif

  static const int kCodePositionIndex = Struct::kSize;
  static const int kSourcePositionIndex = kCodePositionIndex + kPointerSize;
  static const int kStatementPositionIndex =
      kSourcePositionIndex + kPointerSize;
  static const int kBreakPointObjectsIndex =
      kStatementPositionIndex + kPointerSize;
  static const int kSize = kBreakPointObjectsIndex + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(BreakPointInfo);
};


#undef DECL_BOOLEAN_ACCESSORS
#undef DECL_ACCESSORS


// Abstract base class for visiting, and optionally modifying, the
// pointers contained in Objects. Used in GC and serialization/deserialization.
class ObjectVisitor BASE_EMBEDDED {
 public:
  virtual ~ObjectVisitor() {}

  // Visits a contiguous arrays of pointers in the half-open range
  // [start, end). Any or all of the values may be modified on return.
  virtual void VisitPointers(Object** start, Object** end) = 0;

  // To allow lazy clearing of inline caches the visitor has
  // a rich interface for iterating over Code objects..

  // Called prior to visiting the body of a Code object.
  virtual void BeginCodeIteration(Code* code);

  // Visits a code target in the instruction stream.
  virtual void VisitCodeTarget(RelocInfo* rinfo);

  // Visits a runtime entry in the instruction stream.
  virtual void VisitRuntimeEntry(RelocInfo* rinfo) {}

  // Visits a debug call target in the instruction stream.
  virtual void VisitDebugTarget(RelocInfo* rinfo);

  // Called after completing  visiting the body of a Code object.
  virtual void EndCodeIteration(Code* code) {}

  // Handy shorthand for visiting a single pointer.
  virtual void VisitPointer(Object** p) { VisitPointers(p, p + 1); }

  // Visits a contiguous arrays of external references (references to the C++
  // heap) in the half-open range [start, end). Any or all of the values
  // may be modified on return.
  virtual void VisitExternalReferences(Address* start, Address* end) {}

  inline void VisitExternalReference(Address* p) {
    VisitExternalReferences(p, p + 1);
  }

#ifdef DEBUG
  // Intended for serialization/deserialization checking: insert, or
  // check for the presence of, a tag at this position in the stream.
  virtual void Synchronize(const char* tag) {}
#endif
};


// BooleanBit is a helper class for setting and getting a bit in an
// integer or Smi.
class BooleanBit : public AllStatic {
 public:
  static inline bool get(Smi* smi, int bit_position) {
    return get(smi->value(), bit_position);
  }

  static inline bool get(int value, int bit_position) {
    return (value & (1 << bit_position)) != 0;
  }

  static inline Smi* set(Smi* smi, int bit_position, bool v) {
    return Smi::FromInt(set(smi->value(), bit_position, v));
  }

  static inline int set(int value, int bit_position, bool v) {
    if (v) {
      value |= (1 << bit_position);
    } else {
      value &= ~(1 << bit_position);
    }
    return value;
  }
};

} }  // namespace v8::internal

#endif  // V8_OBJECTS_H_
