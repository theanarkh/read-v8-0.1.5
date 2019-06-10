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

#ifndef V8_STUB_CACHE_H_
#define V8_STUB_CACHE_H_

#include "macro-assembler.h"

namespace v8 { namespace internal {


// The stub cache is used for megamorphic calls and property accesses.
// It maps (map, name, type)->Code*

// The design of the table uses the inline cache stubs used for
// mono-morphic calls. The beauty of this, we do not have to
// invalidate the cache whenever a prototype map is changed.  The stub
// validates the map chain as in the mono-morphic case.

class SCTableReference;

class StubCache : public AllStatic {
 public:
  struct Entry {
    String* key;
    Code* value;
  };


  static void Initialize(bool create_heap_objects);

  // Computes the right stub matching. Inserts the result in the
  // cache before returning.  This might compile a stub if needed.
  static Object* ComputeLoadField(String* name,
                                  JSObject* receiver,
                                  JSObject* holder,
                                  int field_index);

  static Object* ComputeLoadCallback(String* name,
                                     JSObject* receiver,
                                     JSObject* holder,
                                     AccessorInfo* callback);

  static Object* ComputeLoadConstant(String* name,
                                     JSObject* receiver,
                                     JSObject* holder,
                                     Object* value);

  static Object* ComputeLoadInterceptor(String* name,
                                        JSObject* receiver,
                                        JSObject* holder);

  static Object* ComputeLoadNormal(String* name, JSObject* receiver);


  // ---

  static Object* ComputeKeyedLoadField(String* name,
                                       JSObject* receiver,
                                       JSObject* holder,
                                       int field_index);

  static Object* ComputeKeyedLoadCallback(String* name,
                                          JSObject* receiver,
                                          JSObject* holder,
                                          AccessorInfo* callback);

  static Object* ComputeKeyedLoadConstant(String* name, JSObject* receiver,
                                          JSObject* holder, Object* value);

  static Object* ComputeKeyedLoadInterceptor(String* name,
                                             JSObject* receiver,
                                             JSObject* holder);

  static Object* ComputeKeyedLoadArrayLength(String* name, JSArray* receiver);

  static Object* ComputeKeyedLoadShortStringLength(String* name,
                                                   String* receiver);

  static Object* ComputeKeyedLoadMediumStringLength(String* name,
                                                    String* receiver);

  static Object* ComputeKeyedLoadLongStringLength(String* name,
                                                  String* receiver);

  static Object* ComputeKeyedLoadFunctionPrototype(String* name,
                                                   JSFunction* receiver);

  // ---

  static Object* ComputeStoreField(String* name,
                                   JSObject* receiver,
                                   int field_index,
                                   Map* transition = NULL);

  static Object* ComputeStoreCallback(String* name,
                                      JSObject* receiver,
                                      AccessorInfo* callback);

  static Object* ComputeStoreInterceptor(String* name, JSObject* receiver);

  // ---

  static Object* ComputeKeyedStoreField(String* name,
                                        JSObject* receiver,
                                        int field_index,
                                        Map* transition = NULL);

  // ---

  static Object* ComputeCallField(int argc,
                                  String* name,
                                  Object* object,
                                  JSObject* holder,
                                  int index);

  static Object* ComputeCallConstant(int argc,
                                     String* name,
                                     Object* object,
                                     JSObject* holder,
                                     JSFunction* function);

  static Object* ComputeCallNormal(int argc, String* name, JSObject* receiver);

  static Object* ComputeCallInterceptor(int argc,
                                        String* name,
                                        Object* object,
                                        JSObject* holder);

  // ---

  static Object* ComputeCallInitialize(int argc);
  static Object* ComputeCallPreMonomorphic(int argc);
  static Object* ComputeCallNormal(int argc);
  static Object* ComputeCallMegamorphic(int argc);
  static Object* ComputeCallMiss(int argc);

  // Finds the Code object stored in the Heap::non_monomorphic_cache().
  static Code* FindCallInitialize(int argc);

  static Object* ComputeCallDebugBreak(int argc);
  static Object* ComputeCallDebugPrepareStepIn(int argc);

  static Object* ComputeLazyCompile(int argc);


  // Update cache for entry hash(name, map).
  static Code* Set(String* name, Map* map, Code* code);

  // Clear the lookup table (@ mark compact collection).
  static void Clear();

  // Functions for generating stubs at startup.
  static void GenerateMiss(MacroAssembler* masm);

  // Generate code for probing the stub cache table.
  static void GenerateProbe(MacroAssembler* masm,
                            Code::Flags flags,
                            Register receiver,
                            Register name,
                            Register scratch);

  enum Table {
    kPrimary,
    kSecondary
  };

 private:
  friend class SCTableReference;
  static const int kPrimaryTableSize = 2048;
  static const int kSecondaryTableSize = 512;
  static Entry primary_[];
  static Entry secondary_[];

  // Computes the hashed offsets for primary and secondary caches.
  static int PrimaryOffset(String* name, Code::Flags flags, Map* map) {
    // Compute the hash of the name (use entire length field).
    uint32_t name_hash = name->length_field();
    ASSERT(name_hash & String::kHashComputedMask);
    // Base the offset on a simple combination of name, flags, and map.
    uint32_t key = (reinterpret_cast<uint32_t>(map) + name_hash) ^ flags;
    return key & ((kPrimaryTableSize - 1) << kHeapObjectTagSize);
  }

  static int SecondaryOffset(String* name, Code::Flags flags, int seed) {
    // Use the seed from the primary cache in the secondary cache.
    uint32_t key = seed - reinterpret_cast<uint32_t>(name) + flags;
    return key & ((kSecondaryTableSize - 1) << kHeapObjectTagSize);
  }

  // Compute the entry for a given offset in exactly the same way as
  // we done in generated code. This makes it a lot easier to avoid
  // making mistakes in the hashed offset computations.
  static Entry* entry(Entry* table, int offset) {
    return reinterpret_cast<Entry*>(
        reinterpret_cast<Address>(table) + (offset << 1));
  }
};


class SCTableReference {
 public:
  static SCTableReference keyReference(StubCache::Table table) {
    return SCTableReference(
        reinterpret_cast<Address>(&first_entry(table)->key));
  }


  static SCTableReference valueReference(StubCache::Table table) {
    return SCTableReference(
        reinterpret_cast<Address>(&first_entry(table)->value));
  }

  Address address() const { return address_; }

 private:
  explicit SCTableReference(Address address) : address_(address) {}

  static StubCache::Entry* first_entry(StubCache::Table table) {
    switch (table) {
      case StubCache::kPrimary: return StubCache::primary_;
      case StubCache::kSecondary: return StubCache::secondary_;
    }
    UNREACHABLE();
    return NULL;
  }

  Address address_;
};

// ------------------------------------------------------------------------


// Support functions for IC stubs for callbacks.
Object* LoadCallbackProperty(Arguments args);
Object* StoreCallbackProperty(Arguments args);


// Support functions for IC stubs for interceptors.
Object* LoadInterceptorProperty(Arguments args);
Object* StoreInterceptorProperty(Arguments args);
Object* CallInterceptorProperty(Arguments args);


// Support function for computing call IC miss stubs.
Handle<Code> ComputeCallMiss(int argc);


// The stub compiler compiles stubs for the stub cache.
class StubCompiler BASE_EMBEDDED {
 public:
  enum CheckType {
    RECEIVER_MAP_CHECK,
    STRING_CHECK,
    NUMBER_CHECK,
    BOOLEAN_CHECK,
    JSARRAY_HAS_FAST_ELEMENTS_CHECK
  };

  StubCompiler() : masm_(NULL, 256) { }

  Object* CompileCallInitialize(Code::Flags flags);
  Object* CompileCallPreMonomorphic(Code::Flags flags);
  Object* CompileCallNormal(Code::Flags flags);
  Object* CompileCallMegamorphic(Code::Flags flags);
  Object* CompileCallMiss(Code::Flags flags);
  Object* CompileCallDebugBreak(Code::Flags flags);
  Object* CompileCallDebugPrepareStepIn(Code::Flags flags);
  Object* CompileLazyCompile(Code::Flags flags);

  // Static functions for generating parts of stubs.
  static void GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                  int index,
                                                  Register prototype);
  static void GenerateLoadField(MacroAssembler* masm,
                                JSObject* object,
                                JSObject* holder,
                                Register receiver,
                                Register scratch1,
                                Register scratch2,
                                int index,
                                Label* miss_label);
  static void GenerateLoadCallback(MacroAssembler* masm,
                                   JSObject* object,
                                   JSObject* holder,
                                   Register receiver,
                                   Register name,
                                   Register scratch1,
                                   Register scratch2,
                                   AccessorInfo* callback,
                                   Label* miss_label);
  static void GenerateLoadConstant(MacroAssembler* masm,
                                   JSObject* object,
                                   JSObject* holder,
                                   Register receiver,
                                   Register scratch1,
                                   Register scratch2,
                                   Object* value,
                                   Label* miss_label);
  static void GenerateLoadInterceptor(MacroAssembler* masm,
                                      JSObject* object,
                                      JSObject* holder,
                                      Register receiver,
                                      Register name,
                                      Register scratch1,
                                      Register scratch2,
                                      Label* miss_label);
  static void GenerateLoadArrayLength(MacroAssembler* masm,
                                      Register receiver,
                                      Register scratch,
                                      Label* miss_label);
  static void GenerateLoadShortStringLength(MacroAssembler* masm,
                                            Register receiver,
                                            Register scratch,
                                            Label* miss_label);
  static void GenerateLoadMediumStringLength(MacroAssembler* masm,
                                             Register receiver,
                                             Register scratch,
                                             Label* miss_label);
  static void GenerateLoadLongStringLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label);
  static void GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                            Register receiver,
                                            Register scratch1,
                                            Register scratch2,
                                            Label* miss_label);
  static void GenerateStoreField(MacroAssembler* masm,
                                 JSObject* object,
                                 int index,
                                 Map* transition,
                                 Register receiver_reg,
                                 Register name_reg,
                                 Register scratch,
                                 Label* miss_label);
  static void GenerateLoadMiss(MacroAssembler* masm, Code::Kind kind);

 protected:
  Object* GetCodeWithFlags(Code::Flags flags);

  MacroAssembler* masm() { return &masm_; }

 private:
  MacroAssembler masm_;
};


class LoadStubCompiler: public StubCompiler {
 public:
  Object* CompileLoadField(JSObject* object, JSObject* holder, int index);
  Object* CompileLoadCallback(JSObject* object,
                              JSObject* holder,
                              AccessorInfo* callback);
  Object* CompileLoadConstant(JSObject* object,
                              JSObject* holder,
                              Object* value);
  Object* CompileLoadInterceptor(JSObject* object,
                                 JSObject* holder,
                                 String* name);

 private:
  Object* GetCode(PropertyType);
};


class KeyedLoadStubCompiler: public StubCompiler {
 public:
  Object* CompileLoadField(String* name,
                           JSObject* object,
                           JSObject* holder,
                           int index);
  Object* CompileLoadCallback(String* name,
                              JSObject* object,
                              JSObject* holder,
                              AccessorInfo* callback);
  Object* CompileLoadConstant(String* name,
                              JSObject* object,
                              JSObject* holder,
                              Object* value);
  Object* CompileLoadInterceptor(JSObject* object,
                                 JSObject* holder,
                                 String* name);
  Object* CompileLoadArrayLength(String* name);
  Object* CompileLoadShortStringLength(String* name);
  Object* CompileLoadMediumStringLength(String* name);
  Object* CompileLoadLongStringLength(String* name);
  Object* CompileLoadFunctionPrototype(String* name);

 private:
  Object* GetCode(PropertyType);
};


class StoreStubCompiler: public StubCompiler {
 public:
  Object* CompileStoreField(JSObject* object,
                            int index,
                            Map* transition,
                            String* name);
  Object* CompileStoreCallback(JSObject* object,
                               AccessorInfo* callbacks,
                               String* name);
  Object* CompileStoreInterceptor(JSObject* object, String* name);

 private:
  Object* GetCode(PropertyType type);
};


class KeyedStoreStubCompiler: public StubCompiler {
 public:
  Object* CompileStoreField(JSObject* object,
                            int index,
                            Map* transition,
                            String* name);

 private:
  Object* GetCode(PropertyType type);
};


class CallStubCompiler: public StubCompiler {
 public:
  explicit CallStubCompiler(int argc) : arguments_(argc) { }

  Object* CompileCallField(Object* object, JSObject* holder, int index);
  Object* CompileCallConstant(Object* object,
                              JSObject* holder,
                              JSFunction* function,
                              CheckType check);
  Object* CompileCallInterceptor(Object* object,
                                 JSObject* holder,
                                 String* name);

 private:
  const ParameterCount arguments_;

  const ParameterCount& arguments() { return arguments_; }

  Object* GetCode(PropertyType type);
};


} }  // namespace v8::internal

#endif  // V8_STUB_CACHE_H_
