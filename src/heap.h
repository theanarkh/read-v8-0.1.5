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

#ifndef V8_HEAP_H_
#define V8_HEAP_H_

namespace v8 { namespace internal {

// Defines all the roots in Heap.
#define STRONG_ROOT_LIST(V)                             \
  V(Map, meta_map)                                      \
  V(Map, heap_number_map)                               \
  V(Map, short_string_map)                              \
  V(Map, medium_string_map)                             \
  V(Map, long_string_map)                               \
  V(Map, short_ascii_string_map)                        \
  V(Map, medium_ascii_string_map)                       \
  V(Map, long_ascii_string_map)                         \
  V(Map, short_symbol_map)                              \
  V(Map, medium_symbol_map)                             \
  V(Map, long_symbol_map)                               \
  V(Map, short_ascii_symbol_map)                        \
  V(Map, medium_ascii_symbol_map)                       \
  V(Map, long_ascii_symbol_map)                         \
  V(Map, short_cons_symbol_map)                         \
  V(Map, medium_cons_symbol_map)                        \
  V(Map, long_cons_symbol_map)                          \
  V(Map, short_cons_ascii_symbol_map)                   \
  V(Map, medium_cons_ascii_symbol_map)                  \
  V(Map, long_cons_ascii_symbol_map)                    \
  V(Map, short_sliced_symbol_map)                       \
  V(Map, medium_sliced_symbol_map)                      \
  V(Map, long_sliced_symbol_map)                        \
  V(Map, short_sliced_ascii_symbol_map)                 \
  V(Map, medium_sliced_ascii_symbol_map)                \
  V(Map, long_sliced_ascii_symbol_map)                  \
  V(Map, short_external_symbol_map)                     \
  V(Map, medium_external_symbol_map)                    \
  V(Map, long_external_symbol_map)                      \
  V(Map, short_external_ascii_symbol_map)               \
  V(Map, medium_external_ascii_symbol_map)              \
  V(Map, long_external_ascii_symbol_map)                \
  V(Map, short_cons_string_map)                         \
  V(Map, medium_cons_string_map)                        \
  V(Map, long_cons_string_map)                          \
  V(Map, short_cons_ascii_string_map)                   \
  V(Map, medium_cons_ascii_string_map)                  \
  V(Map, long_cons_ascii_string_map)                    \
  V(Map, short_sliced_string_map)                       \
  V(Map, medium_sliced_string_map)                      \
  V(Map, long_sliced_string_map)                        \
  V(Map, short_sliced_ascii_string_map)                 \
  V(Map, medium_sliced_ascii_string_map)                \
  V(Map, long_sliced_ascii_string_map)                  \
  V(Map, short_external_string_map)                     \
  V(Map, medium_external_string_map)                    \
  V(Map, long_external_string_map)                      \
  V(Map, short_external_ascii_string_map)               \
  V(Map, medium_external_ascii_string_map)              \
  V(Map, long_external_ascii_string_map)                \
  V(Map, undetectable_short_string_map)                 \
  V(Map, undetectable_medium_string_map)                \
  V(Map, undetectable_long_string_map)                  \
  V(Map, undetectable_short_ascii_string_map)           \
  V(Map, undetectable_medium_ascii_string_map)          \
  V(Map, undetectable_long_ascii_string_map)            \
  V(Map, byte_array_map)                                \
  V(Map, fixed_array_map)                               \
  V(Map, hash_table_map)                                \
  V(Map, context_map)                                   \
  V(Map, global_context_map)                            \
  V(Map, code_map)                                      \
  V(Map, oddball_map)                                   \
  V(Map, boilerplate_function_map)                      \
  V(Map, shared_function_info_map)                      \
  V(Map, proxy_map)                                     \
  V(Map, one_word_filler_map)                           \
  V(Map, two_word_filler_map)                           \
  V(Object, nan_value)                                  \
  V(Object, infinity_value)                             \
  V(Object, negative_infinity_value)                    \
  V(Object, number_max_value)                           \
  V(Object, number_min_value)                           \
  V(Object, undefined_value)                            \
  V(Object, minus_zero_value)                           \
  V(Object, null_value)                                 \
  V(Object, true_value)                                 \
  V(Object, false_value)                                \
  V(String, empty_string)                               \
  V(FixedArray, empty_fixed_array)                      \
  V(Object, the_hole_value)                             \
  V(Map, neander_map)                                   \
  V(JSObject, message_listeners)                        \
  V(Proxy, prototype_accessors)                         \
  V(JSObject, debug_event_listeners)                    \
  V(Dictionary, code_stubs)                             \
  V(Dictionary, non_monomorphic_cache)                  \
  V(Code, js_entry_code)                                \
  V(Code, js_construct_entry_code)                      \
  V(Code, c_entry_code)                                 \
  V(Code, c_entry_debug_break_code)                     \
  V(FixedArray, number_string_cache)                    \
  V(FixedArray, single_character_string_cache)          \
  V(FixedArray, natives_source_cache)

#define ROOT_LIST(V)                                  \
  STRONG_ROOT_LIST(V)                                 \
  V(Object, symbol_table)

#define SYMBOL_LIST(V)                                                   \
  V(Array_symbol, "Array")                                               \
  V(Object_symbol, "Object")                                             \
  V(Proto_symbol, "__proto__")                                           \
  V(StringImpl_symbol, "StringImpl")                                     \
  V(arguments_symbol, "arguments")                                       \
  V(arguments_shadow_symbol, ".arguments")                               \
  V(call_symbol, "call")                                                 \
  V(apply_symbol, "apply")                                               \
  V(caller_symbol, "caller")                                             \
  V(boolean_symbol, "boolean")                                           \
  V(callee_symbol, "callee")                                             \
  V(constructor_symbol, "constructor")                                   \
  V(code_symbol, ".code")                                                \
  V(result_symbol, ".result")                                            \
  V(catch_var_symbol, ".catch-var")                                      \
  V(finally_state_symbol, ".finally-state")                              \
  V(empty_symbol, "")                                                    \
  V(eval_symbol, "eval")                                                 \
  V(function_symbol, "function")                                         \
  V(length_symbol, "length")                                             \
  V(name_symbol, "name")                                                 \
  V(number_symbol, "number")                                             \
  V(object_symbol, "object")                                             \
  V(prototype_symbol, "prototype")                                       \
  V(string_symbol, "string")                                             \
  V(this_symbol, "this")                                                 \
  V(to_string_symbol, "toString")                                        \
  V(char_at_symbol, "CharAt")                                            \
  V(undefined_symbol, "undefined")                                       \
  V(value_of_symbol, "valueOf")                                          \
  V(CreateObjectLiteralBoilerplate_symbol, "CreateObjectLiteralBoilerplate") \
  V(CreateArrayLiteral_symbol, "CreateArrayLiteral")                     \
  V(InitializeVarGlobal_symbol, "InitializeVarGlobal")                   \
  V(InitializeConstGlobal_symbol, "InitializeConstGlobal")               \
  V(stack_overflow_symbol, "kStackOverflowBoilerplate")                  \
  V(illegal_access_symbol, "illegal access")                             \
  V(out_of_memory_symbol, "out-of-memory")                               \
  V(illegal_execution_state_symbol, "illegal execution state")           \
  V(get_symbol, "get")                                                   \
  V(set_symbol, "set")                                                   \
  V(function_class_symbol, "Function")                                   \
  V(illegal_argument_symbol, "illegal argument")                         \
  V(MakeReferenceError_symbol, "MakeReferenceError")                     \
  V(MakeSyntaxError_symbol, "MakeSyntaxError")                           \
  V(MakeTypeError_symbol, "MakeTypeError")                               \
  V(invalid_lhs_in_assignment_symbol, "invalid_lhs_in_assignment")       \
  V(invalid_lhs_in_for_in_symbol, "invalid_lhs_in_for_in")               \
  V(invalid_lhs_in_postfix_op_symbol, "invalid_lhs_in_postfix_op")       \
  V(invalid_lhs_in_prefix_op_symbol, "invalid_lhs_in_prefix_op")         \
  V(illegal_return_symbol, "illegal_return")                             \
  V(illegal_break_symbol, "illegal_break")                               \
  V(illegal_continue_symbol, "illegal_continue")                         \
  V(unknown_label_symbol, "unknown_label")                               \
  V(redeclaration_symbol, "redeclaration")                               \
  V(failure_symbol, "<failure>")                                         \
  V(space_symbol, " ")                                                   \
  V(exec_symbol, "exec")                                                 \
  V(zero_symbol, "0")


// The all static Heap captures the interface to the global object heap.
// All JavaScript contexts by this process share the same object heap.

class Heap : public AllStatic {
 public:
  // Configure heap size before setup. Return false if the heap has been
  // setup already.
  static bool ConfigureHeap(int semispace_size, int old_gen_size);

  // Initializes the global object heap. If create_heap_objects is true,
  // also creates the basic non-mutable objects.
  // Returns whether it succeeded.
  static bool Setup(bool create_heap_objects);

  // Destroys all memory allocated by the heap.
  static void TearDown();

  // Returns whether Setup has been called.
  static bool HasBeenSetup();

  // Returns the maximum heap capacity.
  static int MaxCapacity() {
    return young_generation_size_ + old_generation_size_;
  }
  static int SemiSpaceSize() { return semispace_size_; }
  static int InitialSemiSpaceSize() { return initial_semispace_size_; }
  static int YoungGenerationSize() { return young_generation_size_; }
  static int OldGenerationSize() { return old_generation_size_; }

  // Returns the capacity of the heap in bytes w/o growing. Heap grows when
  // more spaces are needed until it reaches the limit.
  static int Capacity();

  // Returns the available bytes in space w/o growing.
  // Heap doesn't guarantee that it can allocate an object that requires
  // all available bytes. Check MaxHeapObjectSize() instead.
  static int Available();

  // Returns the maximum object size that heap supports. Objects larger than
  // the maximum heap object size are allocated in a large object space.
  static inline int MaxHeapObjectSize();

  // Returns of size of all objects residing in the heap.
  static int SizeOfObjects();

  // Return the starting address and a mask for the new space.  And-masking an
  // address with the mask will result in the start address of the new space
  // for all addresses in either semispace.
  static Address NewSpaceStart() { return new_space_->start(); }
  static uint32_t NewSpaceMask() { return new_space_->mask(); }
  static Address NewSpaceTop() { return new_space_->top(); }

  static NewSpace* new_space() { return new_space_; }
  static OldSpace* old_space() { return old_space_; }
  static OldSpace* code_space() { return code_space_; }
  static MapSpace* map_space() { return map_space_; }
  static LargeObjectSpace* lo_space() { return lo_space_; }

  static Address* NewSpaceAllocationTopAddress() {
    return new_space_->allocation_top_address();
  }
  static Address* NewSpaceAllocationLimitAddress() {
    return new_space_->allocation_limit_address();
  }

  // Allocates and initializes a new JavaScript object based on a
  // constructor.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateJSObject(JSFunction* constructor,
                                  PretenureFlag pretenure = NOT_TENURED);

  // Allocates the function prototype.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateFunctionPrototype(JSFunction* function);

  // Reinitialize a JSGlobalObject based on a constructor.  The JSObject
  // must have the same size as objects allocated using the
  // constructor.  The JSObject is reinitialized and behaves as an
  // object that has been freshly allocated using the constructor.
  static Object* ReinitializeJSGlobalObject(JSFunction* constructor,
                                            JSGlobalObject* global);

  // Allocates and initializes a new JavaScript object based on a map.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateJSObjectFromMap(Map* map,
                                         PretenureFlag pretenure = NOT_TENURED);

  // Allocates a heap object based on the map.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* Allocate(Map* map, AllocationSpace space);

  // Allocates a JS Map in the heap.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* AllocateMap(InstanceType instance_type, int instance_size);

  // Allocates a partial map for bootstrapping.
  static Object* AllocatePartialMap(InstanceType instance_type,
                                    int instance_size);

  // Allocate a map for the specified function
  static Object* AllocateInitialMap(JSFunction* fun);

  // Allocates and fully initializes a String.  There are two String
  // encodings: ASCII and two byte. One should choose between the three string
  // allocation functions based on the encoding of the string buffer used to
  // initialized the string.
  //   - ...FromAscii initializes the string from a buffer that is ASCII
  //     encoded (it does not check that the buffer is ASCII encoded) and the
  //     result will be ASCII encoded.
  //   - ...FromUTF8 initializes the string from a buffer that is UTF-8
  //     encoded.  If the characters are all single-byte characters, the
  //     result will be ASCII encoded, otherwise it will converted to two
  //     byte.
  //   - ...FromTwoByte initializes the string from a buffer that is two-byte
  //     encoded.  If the characters are all single-byte characters, the
  //     result will be converted to ASCII, otherwise it will be left as
  //     two-byte.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateStringFromAscii(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);
  static Object* AllocateStringFromUtf8(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);
  static Object* AllocateStringFromTwoByte(
      Vector<const uc16> str,
      PretenureFlag pretenure = NOT_TENURED);

  // Allocates a symbol in old space based on the character stream.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* AllocateSymbol(unibrow::CharacterStream* buffer,
                                int chars,
                                int hash);

  // Allocates and partially initializes a String.  There are two String
  // encodings: ASCII and two byte.  These functions allocate a string of the
  // given length and set its map and length fields.  The characters of the
  // string are uninitialized.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateRawAsciiString(
      int length,
      PretenureFlag pretenure = NOT_TENURED);
  static Object* AllocateRawTwoByteString(
      int length,
      PretenureFlag pretenure = NOT_TENURED);

  // Computes a single character string where the character has code.
  // A cache is used for ascii codes.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed. Please note this does not perform a garbage collection.
  static Object* LookupSingleCharacterStringFromCode(uint16_t code);

  // Allocate a byte array of the specified length
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please not this does not perform a garbage collection.
  static Object* AllocateByteArray(int length);

  // Allocates a fixed array initialized with undefined values
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateFixedArray(int length,
                                    PretenureFlag pretenure = NOT_TENURED);


  // Allocates a fixed array initialized with the hole values.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateFixedArrayWithHoles(int length);

  // AllocateHashTable is identical to AllocateFixedArray except
  // that the resulting object has hash_table_map as map.
  static Object* AllocateHashTable(int length);

  // Allocate a global (but otherwise uninitialized) context.
  static Object* AllocateGlobalContext();

  // Allocate a function context.
  static Object* AllocateFunctionContext(int length, JSFunction* closure);

  // Allocate a 'with' context.
  static Object* AllocateWithContext(Context* previous, JSObject* extension);

  // Allocates a new utility object in the old generation.
  static Object* AllocateStruct(InstanceType type);


  // Initializes a function with a shared part and prototype.
  // Returns the function.
  // Note: this code was factored out of AllocateFunction such that
  // other parts of the VM could use it. Specifically, a function that creates
  // instances of type JS_FUNCTION_TYPE benefit from the use of this function.
  // Please note this does not perform a garbage collection.
  static Object* InitializeFunction(JSFunction* function,
                                    SharedFunctionInfo* shared,
                                    Object* prototype);

  // Allocates a function initialized with a shared part.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateFunction(Map* function_map,
                                  SharedFunctionInfo* shared,
                                  Object* prototype);

  // Indicies for direct access into argument objects.
  static const int arguments_callee_index = 0;
  static const int arguments_length_index = 1;

  // Allocates an arguments object - optionally with an elements array.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateArgumentsObject(Object* callee, int length);

  // Converts a double into either a Smi or a HeapNumber object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* NewNumberFromDouble(double value,
                                     PretenureFlag pretenure = NOT_TENURED);

  // Same as NewNumberFromDouble, but may return a preallocated/immutable
  // number object (e.g., minus_zero_value_, nan_value_)
  static Object* NumberFromDouble(double value,
                                  PretenureFlag pretenure = NOT_TENURED);

  // Allocated a HeapNumber from value.
  static Object* AllocateHeapNumber(double value, PretenureFlag pretenure);
  static Object* AllocateHeapNumber(double value);  // pretenure = NOT_TENURED

  // Converts an int into either a Smi or a HeapNumber object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static inline Object* NumberFromInt32(int32_t value);

  // Converts an int into either a Smi or a HeapNumber object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static inline Object* NumberFromUint32(uint32_t value);

  // Allocates a new proxy object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateProxy(Address proxy,
                               PretenureFlag pretenure = NOT_TENURED);

  // Allocates a new SharedFunctionInfo object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateSharedFunctionInfo(Object* name);

  // Allocates a new cons string object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateConsString(String* first, String* second);

  // Allocates a new sliced string object which is a slice of an underlying
  // string buffer stretching from the index start (inclusive) to the index
  // end (exclusive).
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateSlicedString(String* buffer, int start, int end);

  // Allocates a new sub string object which is a substring of an underlying
  // string buffer stretching from the index start (inclusive) to the index
  // end (exclusive).
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateSubString(String* buffer, int start, int end);

  // Allocate a new external string object, which is backed by a string
  // resource that resides outside the V8 heap.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  static Object* AllocateExternalStringFromAscii(
      ExternalAsciiString::Resource* resource);
  static Object* AllocateExternalStringFromTwoByte(
      ExternalTwoByteString::Resource* resource);

  // Allocates an uninitialized object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static inline Object* AllocateRaw(int size_in_bytes, AllocationSpace space);

  // Makes a new native code object
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* CreateCode(const CodeDesc& desc,
                            ScopeInfo<>* sinfo,
                            Code::Flags flags);

  static Object* CopyCode(Code* code);
  // Finds the symbol for string in the symbol table.
  // If not found, a new symbol is added to the table and returned.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* LookupSymbol(Vector<const char> str);
  static Object* LookupAsciiSymbol(const char* str) {
    return LookupSymbol(CStrVector(str));
  }
  static Object* LookupSymbol(String* str);

  // Compute the matching symbol map for a string if possible.
  // NULL is returned if string is in new space or not flattened.
  static Map* SymbolMapForString(String* str);

  // Converts the given boolean condition to JavaScript boolean value.
  static Object* ToBoolean(bool condition) {
    return condition ? true_value() : false_value();
  }

  // Code that should be run before and after each GC.  Includes some
  // reporting/verification activities when compiled with DEBUG set.
  static void GarbageCollectionPrologue();
  static void GarbageCollectionEpilogue();

  // Performs garbage collection operation.
  // Returns whether required_space bytes are available after the collection.
  static bool CollectGarbage(int required_space, AllocationSpace space);

  // Utility to invoke the scavenger. This is needed in test code to
  // ensure correct callback for weak global handles.
  static void PerformScavenge() {
    PerformGarbageCollection(NEW_SPACE, SCAVENGER);
  }

  static void SetGlobalGCPrologueCallback(GCCallback callback) {
    global_gc_prologue_callback_ = callback;
  }
  static void SetGlobalGCEpilogueCallback(GCCallback callback) {
    global_gc_epilogue_callback_ = callback;
  }

  // Heap roots
#define ROOT_ACCESSOR(type, name) static type* name() { return name##_; }
  ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

// Utility type maps
#define STRUCT_MAP_ACCESSOR(NAME, Name, name) \
    static Map* name##_map() { return name##_map_; }
  STRUCT_LIST(STRUCT_MAP_ACCESSOR)
#undef STRUCT_MAP_ACCESSOR

#define SYMBOL_ACCESSOR(name, str) static String* name() { return name##_; }
  SYMBOL_LIST(SYMBOL_ACCESSOR)
#undef SYMBOL_ACCESSOR

  // Iterates over all roots in the heap.
  static void IterateRoots(ObjectVisitor* v);
  // Iterates over all strong roots in the heap.
  static void IterateStrongRoots(ObjectVisitor* v);

  // Iterates remembered set of an old space.
  static void IterateRSet(PagedSpace* space, ObjectSlotCallback callback);

  // Iterates a range of remembered set addresses starting with rset_start
  // corresponding to the range of allocated pointers
  // [object_start, object_end).
  static void IterateRSetRange(Address object_start,
                               Address object_end,
                               Address rset_start,
                               ObjectSlotCallback copy_object_func);

  // Returns whether the object resides in new space.
  static inline bool InNewSpace(Object* object);
  static inline bool InFromSpace(Object* object);
  static inline bool InToSpace(Object* object);

  // Checks whether an address/object in the heap (including auxiliary
  // area and unused area).
  static bool Contains(Address addr);
  static bool Contains(HeapObject* value);

  // Checks whether an address/object in a space.
  // Currently used by tests and heap verification only.
  static bool InSpace(Address addr, AllocationSpace space);
  static bool InSpace(HeapObject* value, AllocationSpace space);

  // Sets the stub_cache_ (only used when expanding the dictionary).
  static void set_code_stubs(Dictionary* value) { code_stubs_ = value; }

  // Sets the non_monomorphic_cache_ (only used when expanding the dictionary).
  static void set_non_monomorphic_cache(Dictionary* value) {
    non_monomorphic_cache_ = value;
  }

#ifdef DEBUG
  static void Print();
  static void PrintHandles();

  // Verify the heap is in its normal state before or after a GC.
  static void Verify();

  // Report heap statistics.
  static void ReportHeapStatistics(const char* title);
  static void ReportCodeStatistics(const char* title);

  // Fill in bogus values in from space
  static void ZapFromSpace();
#endif

  // Makes a new symbol object
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  static Object* CreateSymbol(const char* str, int length, int hash);
  static Object* CreateSymbol(String* str);

  // Write barrier support for address[offset] = o.
  inline static void RecordWrite(Address address, int offset);

  // Given an address in the heap, returns a pointer to the object which
  // body contains the address. Returns Failure::Exception() if the
  // operation fails.
  static Object* FindCodeObject(Address a);

  // Invoke Shrink on shrinkable spaces.
  static void Shrink();

  enum HeapState { NOT_IN_GC, SCAVENGE, MARK_COMPACT };
  static inline HeapState gc_state() { return gc_state_; }

#ifdef DEBUG
  static bool IsAllocationAllowed() { return allocation_allowed_; }
  static inline bool allow_allocation(bool enable);

  static bool disallow_allocation_failure() {
    return disallow_allocation_failure_;
  }

  static void TracePathToObject();
  static void TracePathToGlobal();
#endif

  // Helper for Serialization/Deserialization that restricts memory allocation
  // to the predictable LINEAR_ONLY policy
  static void SetLinearAllocationOnly(bool linear_only) {
    old_space_->SetLinearAllocationOnly(linear_only);
    code_space_->SetLinearAllocationOnly(linear_only);
    map_space_->SetLinearAllocationOnly(linear_only);
  }

  // Callback function pased to Heap::Iterate etc.  Copies an object if
  // necessary, the object might be promoted to an old space.  The caller must
  // ensure the precondition that the object is (a) a heap object and (b) in
  // the heap's from space.
  static void CopyObject(HeapObject** p);

  // Clear a range of remembered set addresses corresponding to the object
  // area address 'start' with size 'size_in_bytes', eg, when adding blocks
  // to the free list.
  static void ClearRSetRange(Address start, int size_in_bytes);

  // Rebuild remembered set in old and map spaces.
  static void RebuildRSets();

  //
  // Support for the API.
  //

  static bool CreateApiObjects();

  // Attempt to find the number in a small cache.  If we finds it, return
  // the string representation of the number.  Otherwise return undefined.
  static Object* GetNumberStringCache(Object* number);

  // Update the cache with a new number-string pair.
  static void SetNumberStringCache(Object* number, String* str);

  // Entries in the cache.  Must be a power of 2.
  static const int kNumberStringCacheSize = 64;

 private:
  static int semispace_size_;
  static int initial_semispace_size_;
  static int young_generation_size_;
  static int old_generation_size_;

  static int new_space_growth_limit_;
  static int scavenge_count_;

  static const int kMaxMapSpaceSize = 8*MB;

  static NewSpace* new_space_;
  static OldSpace* old_space_;
  static OldSpace* code_space_;
  static MapSpace* map_space_;
  static LargeObjectSpace* lo_space_;
  static HeapState gc_state_;

  // Returns the size of object residing in non new spaces.
  static int PromotedSpaceSize();

#ifdef DEBUG
  static bool allocation_allowed_;
  static int mc_count_;  // how many mark-compact collections happened
  static int gc_count_;  // how many gc happened

  // If the --gc-interval flag is set to a positive value, this
  // variable holds the value indicating the number of allocations
  // remain until the next failure and garbage collection.
  static int allocation_timeout_;

  // Do we expect to be able to handle allocation failure at this
  // time?
  static bool disallow_allocation_failure_;
#endif  // DEBUG

  // Promotion limit that trigger a global GC
  static int promoted_space_limit_;

  // Indicates that an allocation has failed in the old generation since the
  // last GC.
  static int old_gen_exhausted_;

  // Declare all the roots
#define ROOT_DECLARATION(type, name) static type* name##_;
  ROOT_LIST(ROOT_DECLARATION)
#undef ROOT_DECLARATION

// Utility type maps
#define DECLARE_STRUCT_MAP(NAME, Name, name) static Map* name##_map_;
  STRUCT_LIST(DECLARE_STRUCT_MAP)
#undef DECLARE_STRUCT_MAP

#define SYMBOL_DECLARATION(name, str) static String* name##_;
  SYMBOL_LIST(SYMBOL_DECLARATION)
#undef SYMBOL_DECLARATION

  // GC callback function, called before and after mark-compact GC.
  // Allocations in the callback function are disallowed.
  static GCCallback global_gc_prologue_callback_;
  static GCCallback global_gc_epilogue_callback_;

  // Checks whether a global GC is necessary
  static GarbageCollector SelectGarbageCollector(AllocationSpace space);

  // Performs garbage collection
  static void PerformGarbageCollection(AllocationSpace space,
                                       GarbageCollector collector);

  // Returns either a Smi or a Number object from 'value'. If 'new_object'
  // is false, it may return a preallocated immutable object.
  static Object* SmiOrNumberFromDouble(double value,
                                       bool new_object,
                                       PretenureFlag pretenure = NOT_TENURED);

  // Allocate an uninitialized object in map space.  The behavior is
  // identical to Heap::AllocateRaw(size_in_bytes, MAP_SPACE), except that
  // (a) it doesn't have to test the allocation space argument and (b) can
  // reduce code size (since both AllocateRaw and AllocateRawMap are
  // inlined).
  static inline Object* AllocateRawMap(int size_in_bytes);

  // Allocate storage for JSObject properties.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  static inline Object* AllocatePropertyStorageForMap(Map* map);

  // Initializes a JSObject based on its map.
  static void InitializeJSObjectFromMap(JSObject* obj,
                                        FixedArray* properties,
                                        Map* map);

  static bool CreateInitialMaps();
  static bool CreateInitialObjects();
  static void CreateFixedStubs();
  static Object* CreateOddball(Map* map,
                               const char* to_string,
                               Object* to_number);

  // Allocate empty fixed array.
  static Object* AllocateEmptyFixedArray();

  // Performs a minor collection in new generation.
  static void Scavenge();

  // Performs a major collection in the whole heap.
  static void MarkCompact();

  // Code to be run before and after mark-compact.
  static void MarkCompactPrologue();
  static void MarkCompactEpilogue();

  // Helper function used by CopyObject to copy a source object to an
  // allocated target object and update the forwarding pointer in the source
  // object.  Returns the target object.
  static HeapObject* MigrateObject(HeapObject** source_p,
                                   HeapObject* target,
                                   int size);

  // Helper function that governs the promotion policy from new space to
  // old.  If the object's old address lies below the new space's age
  // mark or if we've already filled the bottom 1/16th of the to space,
  // we try to promote this object.
  static inline bool ShouldBePromoted(Address old_address, int object_size);
#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  // Record the copy of an object in the NewSpace's statistics.
  static void RecordCopiedObject(HeapObject* obj);

  // Record statistics before and after garbage collection.
  static void ReportStatisticsBeforeGC();
  static void ReportStatisticsAfterGC();
#endif

  // Update an old object's remembered set
  static int UpdateRSet(HeapObject* obj);

  // Rebuild remembered set in an old space.
  static void RebuildRSets(PagedSpace* space);

  // Rebuild remembered set in the large object space.
  static void RebuildRSets(LargeObjectSpace* space);

  static const int kInitialSymbolTableSize = 2048;

  friend class Factory;
  friend class DisallowAllocationFailure;
};


#ifdef DEBUG
// Visitor class to verify interior pointers that do not have remembered set
// bits.  All heap object pointers have to point into the heap to a location
// that has a map pointer at its first word.  Caveat: Heap::Contains is an
// approximation because it can return true for objects in a heap space but
// above the allocation pointer.
class VerifyPointersVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(Heap::Contains(object));
        ASSERT(object->map()->IsMap());
      }
    }
  }
};


// Visitor class to verify interior pointers that have remembered set bits.
// As VerifyPointersVisitor but also checks that remembered set bits are
// always set for pointers into new space.
class VerifyPointersAndRSetVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(Heap::Contains(object));
        ASSERT(object->map()->IsMap());
        if (Heap::InNewSpace(object)) {
          ASSERT(Page::IsRSetSet(reinterpret_cast<Address>(current), 0));
        }
      }
    }
  }
};
#endif


// A HeapIterator provides iteration over the whole heap It aggregates a the
// specific iterators for the different spaces as these can only iterate over
// one space only.

class HeapIterator BASE_EMBEDDED {
 public:
  explicit HeapIterator();
  virtual ~HeapIterator();

  bool has_next();
  HeapObject* next();
  void reset();

 private:
  // Perform the initialization.
  void Init();

  // Perform all necessary shutdown (destruction) work.
  void Shutdown();

  // Space iterator for iterating all the spaces.
  SpaceIterator* space_iterator_;
  // Object iterator for the space currently being iterated.
  ObjectIterator* object_iterator_;
};


// ----------------------------------------------------------------------------
// Marking stack for tracing live objects.

class MarkingStack {
 public:
  void Initialize(Address low, Address high) {
    top_ = low_ = reinterpret_cast<HeapObject**>(low);
    high_ = reinterpret_cast<HeapObject**>(high);
    overflowed_ = false;
  }

  bool is_full() { return top_ >= high_; }

  bool is_empty() { return top_ <= low_; }

  bool overflowed() { return overflowed_; }

  void clear_overflowed() { overflowed_ = false; }

  void Push(HeapObject* p) {
    ASSERT(!is_full());
    *(top_++) = p;
    if (is_full()) overflowed_ = true;
  }

  HeapObject* Pop() {
    ASSERT(!is_empty());
    return *(--top_);
  }

 private:
  HeapObject** low_;
  HeapObject** top_;
  HeapObject** high_;
  bool overflowed_;
};


// ----------------------------------------------------------------------------
// Functions and constants used for marking live objects.
//

// Many operations (eg, Object::Size()) are based on an object's map.  When
// objects are marked as live or overflowed, their map pointer is changed.
// Use clear_mark_bit and/or clear_overflow_bit to recover the original map
// word.
static inline intptr_t clear_mark_bit(intptr_t map_word) {
  return map_word | kMarkingMask;
}


static inline intptr_t clear_overflow_bit(intptr_t map_word) {
  return map_word & ~kOverflowMask;
}


// True if the object is marked live.
static inline bool is_marked(HeapObject* obj) {
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  return (map_word & kMarkingMask) == 0;
}


// Mutate an object's map pointer to indicate that the object is live.
static inline void set_mark(HeapObject* obj) {
  ASSERT(!is_marked(obj));
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  obj->set_map(reinterpret_cast<Map*>(map_word & ~kMarkingMask));
}


// Mutate an object's map pointer to remove the indication that the object
// is live, ie, (partially) restore the map pointer.
static inline void clear_mark(HeapObject* obj) {
  ASSERT(is_marked(obj));
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  obj->set_map(reinterpret_cast<Map*>(clear_mark_bit(map_word)));
}


// True if the object is marked overflowed.
static inline bool is_overflowed(HeapObject* obj) {
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  return (map_word & kOverflowMask) != 0;
}


// Mutate an object's map pointer to indicate that the object is overflowed.
// Overflowed objects have been reached during marking of the heap but not
// pushed on the marking stack (and thus their children have not necessarily
// been marked).
static inline void set_overflow(HeapObject* obj) {
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  obj->set_map(reinterpret_cast<Map*>(map_word | kOverflowMask));
}


// Mutate an object's map pointer to remove the indication that the object
// is overflowed, ie, (partially) restore the map pointer.
static inline void clear_overflow(HeapObject* obj) {
  ASSERT(is_overflowed(obj));
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  obj->set_map(reinterpret_cast<Map*>(clear_overflow_bit(map_word)));
}


// A helper class to document/test C++ scopes where we do not
// expect a GC. Usage:
//
// /* Allocation not allowed: we cannot handle a GC in this scope. */
// { AssertNoAllocation nogc;
//   ...
// }

#ifdef DEBUG

class DisallowAllocationFailure {
 public:
  DisallowAllocationFailure() {
    old_state_ = Heap::disallow_allocation_failure_;
    Heap::disallow_allocation_failure_ = true;
  }
  ~DisallowAllocationFailure() {
    Heap::disallow_allocation_failure_ = old_state_;
  }
 private:
  bool old_state_;
};

class AssertNoAllocation {
 public:
  AssertNoAllocation() {
    old_state_ = Heap::allow_allocation(false);
  }

  ~AssertNoAllocation() {
    Heap::allow_allocation(old_state_);
  }

 private:
  bool old_state_;
};

#else  // ndef DEBUG

class AssertNoAllocation {
 public:
  AssertNoAllocation() { }
  ~AssertNoAllocation() { }
};

#endif

#ifdef ENABLE_LOGGING_AND_PROFILING
// The HeapProfiler writes data to the log files, which can be postprocessed
// to generate .hp files for use by the GHC/Valgrind tool hp2ps.
class HeapProfiler {
 public:
  // Write a single heap sample to the log file.
  static void WriteSample();

 private:
  // Update the array info with stats from obj.
  static void CollectStats(HeapObject* obj, HistogramInfo* info);
};
#endif

} }  // namespace v8::internal

#endif  // V8_HEAP_H_
