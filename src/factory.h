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

#ifndef V8_FACTORY_H_
#define V8_FACTORY_H_

#include "heap.h"

namespace v8 { namespace internal {


// Interface for handle based allocation.

class Factory : public AllStatic {
 public:
  // Allocate a new fixed array.
  static Handle<FixedArray> NewFixedArray(
      int size,
      PretenureFlag pretenure = NOT_TENURED);

  static Handle<DescriptorArray> NewDescriptorArray(int number_of_descriptors);

  static Handle<String> LookupSymbol(Vector<const char> str);
  static Handle<String> LookupAsciiSymbol(const char* str) {
    return LookupSymbol(CStrVector(str));
  }


  // String creation functions.  Most of the string creation functions take
  // a Heap::PretenureFlag argument to optionally request that they be
  // allocated in the old generation.  The pretenure flag defaults to
  // DONT_TENURE.
  //
  // Creates a new String object.  There are two String encodings: ASCII and
  // two byte.  One should choose between the three string factory functions
  // based on the encoding of the string buffer that the string is
  // initialized from.
  //   - ...FromAscii initializes the string from a buffer that is ASCII
  //     encoded (it does not check that the buffer is ASCII encoded) and
  //     the result will be ASCII encoded.
  //   - ...FromUtf8 initializes the string from a buffer that is UTF-8
  //     encoded.  If the characters are all single-byte characters, the
  //     result will be ASCII encoded, otherwise it will converted to two
  //     byte.
  //   - ...FromTwoByte initializes the string from a buffer that is two
  //     byte encoded.  If the characters are all single-byte characters,
  //     the result will be converted to ASCII, otherwise it will be left as
  //     two byte.
  //
  // ASCII strings are pretenured when used as keys in the SourceCodeCache.
  static Handle<String> NewStringFromAscii(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);

  // UTF8 strings are pretenured when used for regexp literal patterns and
  // flags in the parser.
  static Handle<String> NewStringFromUtf8(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);

  static Handle<String> NewStringFromTwoByte(Vector<const uc16> str);

  // Allocates and partially initializes a TwoByte String. The characters of
  // the string are uninitialized. Currently used in regexp code only, where
  // they are pretenured.
  static Handle<String> NewRawTwoByteString(
      int length,
      PretenureFlag pretenure = NOT_TENURED);

  // Create a new cons string object which consists of a pair of strings.
  static Handle<String> NewConsString(Handle<String> first,
                                      Handle<String> second);

  // Create a new sliced string object which represents a substring of a
  // backing string.
  static Handle<String> NewStringSlice(Handle<String> str, int begin, int end);

  // Creates a new external String object.  There are two String encodings
  // in the system: ASCII and two byte.  Unlike other String types, it does
  // not make sense to have a UTF-8 factory function for external strings,
  // because we cannot change the underlying buffer.
  static Handle<String> NewExternalStringFromAscii(
      ExternalAsciiString::Resource* resource);
  static Handle<String> NewExternalStringFromTwoByte(
      ExternalTwoByteString::Resource* resource);

  // Create a global (but otherwise uninitialized) context.
  static Handle<Context> NewGlobalContext();

  // Create a function context.
  static Handle<Context> NewFunctionContext(int length,
                                            Handle<JSFunction> closure);

  // Create a 'with' context.
  static Handle<Context> NewWithContext(Handle<Context> previous,
                                        Handle<JSObject> extension);

  // Return the Symbol maching the passed in string.
  static Handle<String> SymbolFromString(Handle<String> value);

  // Allocate a new struct.  The struct is pretenured (allocated directly in
  // the old generation).
  static Handle<Struct> NewStruct(InstanceType type);

  static Handle<AccessorInfo> NewAccessorInfo();

  static Handle<Script> NewScript(Handle<String> source);

  // Proxies are pretenured when allocated by the bootstrapper.
  static Handle<Proxy> NewProxy(Address addr,
                                PretenureFlag pretenure = NOT_TENURED);

  // Allocate a new proxy.  The proxy is pretenured (allocated directly in
  // the old generation).
  static Handle<Proxy> NewProxy(const AccessorDescriptor* proxy);

  static Handle<ByteArray> NewByteArray(int length);

  static Handle<Map> NewMap(InstanceType type, int instance_size);

  static Handle<JSObject> NewFunctionPrototype(Handle<JSFunction> function);

  static Handle<Map> CopyMap(Handle<Map> map);

  static Handle<FixedArray> CopyFixedArray(Handle<FixedArray> array);

  // Numbers (eg, literals) are pretenured by the parser.
  static Handle<Object> NewNumber(double value,
                                  PretenureFlag pretenure = NOT_TENURED);

  static Handle<Object> NewNumberFromInt(int value);

  // These objects are used by the api to create env-independent data
  // structures in the heap.
  static Handle<JSObject> NewNeanderObject();

  static Handle<JSObject> NewArgumentsObject(Handle<Object> callee, int length);

  // JS objects are pretenured when allocated by the bootstrapper and
  // runtime.
  static Handle<JSObject> NewJSObject(Handle<JSFunction> constructor,
                                      PretenureFlag pretenure = NOT_TENURED);

  // Allocate a JS object representing an object literal.  The object is
  // pretenured (allocated directly in the old generation).
  static Handle<JSObject> NewObjectLiteral(int expected_number_of_properties);

  // Allocate a JS array representing an array literal.  The array is
  // pretenured (allocated directly in the old generation).
  static Handle<JSArray> NewArrayLiteral(int length);

  // JS arrays are pretenured when allocated by the parser.
  static Handle<JSArray> NewJSArray(int init_length,
                                    PretenureFlag pretenure = NOT_TENURED);

  static Handle<JSArray> NewJSArrayWithElements(
      Handle<FixedArray> elements,
      PretenureFlag pretenure = NOT_TENURED);

  static Handle<JSFunction> NewFunction(Handle<String> name,
                                        Handle<Object> prototype);

  static Handle<JSFunction> NewFunction(Handle<Object> super, bool is_global);

  static Handle<JSFunction> NewFunctionFromBoilerplate(
      Handle<JSFunction> boilerplate,
      Handle<Context> context);

  static Handle<Code> NewCode(const CodeDesc& desc, ScopeInfo<>* sinfo,
                              Code::Flags flags);

  static Handle<Code> CopyCode(Handle<Code> code);

  static Handle<Object> ToObject(Handle<Object> object,
                                 Handle<Context> global_context);

  // Interface for creating error objects.

  static Handle<Object> NewError(const char* maker, const char* type,
                                 Handle<JSArray> args);
  static Handle<Object> NewError(const char* maker, const char* type,
                                 Vector< Handle<Object> > args);
  static Handle<Object> NewError(const char* type,
                                 Vector< Handle<Object> > args);
  static Handle<Object> NewError(Handle<String> message);
  static Handle<Object> NewError(const char* constructor,
                                 Handle<String> message);

  static Handle<Object> NewTypeError(const char* type,
                                     Vector< Handle<Object> > args);
  static Handle<Object> NewTypeError(Handle<String> message);

  static Handle<Object> NewRangeError(const char* type,
                                      Vector< Handle<Object> > args);
  static Handle<Object> NewRangeError(Handle<String> message);

  static Handle<Object> NewSyntaxError(const char* type, Handle<JSArray> args);
  static Handle<Object> NewSyntaxError(Handle<String> message);

  static Handle<Object> NewReferenceError(const char* type,
                                          Vector< Handle<Object> > args);
  static Handle<Object> NewReferenceError(Handle<String> message);

  static Handle<Object> NewEvalError(const char* type,
                                     Vector< Handle<Object> > args);


  static Handle<JSFunction> NewFunction(Handle<String> name,
                                        InstanceType type,
                                        int instance_size,
                                        Handle<Code> code,
                                        bool force_initial_map);

  static Handle<JSFunction> NewFunctionBoilerplate(Handle<String> name,
                                                   int number_of_literals,
                                                   Handle<Code> code);

  static Handle<JSFunction> NewFunctionBoilerplate(Handle<String> name);

  static Handle<JSFunction> NewFunction(Handle<Map> function_map,
      Handle<SharedFunctionInfo> shared, Handle<Object> prototype);


  static Handle<JSFunction> NewFunctionWithPrototype(Handle<String> name,
                                                     InstanceType type,
                                                     int instance_size,
                                                     Handle<JSObject> prototype,
                                                     Handle<Code> code,
                                                     bool force_initial_map);

  static Handle<DescriptorArray> CopyAppendProxyDescriptor(
      Handle<DescriptorArray> array,
      Handle<String> key,
      Handle<Object> value,
      PropertyAttributes attributes);

  static Handle<JSFunction> CreateApiFunction(Handle<FunctionTemplateInfo> data,
                                              bool is_global = false);

  static Handle<JSFunction> InstallMembers(Handle<JSFunction> function);

  // Installs interceptors on the instance.  'desc' is a function template,
  // and instance is an object instance created by the function of this
  // function tempalte.
  static void ConfigureInstance(Handle<FunctionTemplateInfo> desc,
                                Handle<JSObject> instance,
                                bool* pending_exception);

#define ROOT_ACCESSOR(type, name) \
  static Handle<type> name() { return Handle<type>(&Heap::name##_); }
  ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR_ACCESSOR

#define SYMBOL_ACCESSOR(name, str) \
  static Handle<String> name() { return Handle<String>(&Heap::name##_); }
  SYMBOL_LIST(SYMBOL_ACCESSOR)
#undef SYMBOL_ACCESSOR

  static Handle<DescriptorArray> empty_descriptor_array() {
    return Handle<DescriptorArray>::cast(empty_fixed_array());
  }

  static Handle<SharedFunctionInfo> NewSharedFunctionInfo(Handle<String> name);

  static Handle<Dictionary> DictionaryAtNumberPut(Handle<Dictionary>,
                                                  uint32_t key,
                                                  Handle<Object> value);

 private:
  static Handle<JSFunction> NewFunctionHelper(Handle<String> name,
                                              Handle<Object> prototype);

  static Handle<DescriptorArray> CopyAppendCallbackDescriptors(
      Handle<DescriptorArray> array,
      Handle<Object> descriptors);

  static Handle<JSFunction> BaseNewFunctionFromBoilerplate(
      Handle<JSFunction> boilerplate,
      Handle<Map> function_map);
};


} }  // namespace v8::internal

#endif  // V8_FACTORY_H_
