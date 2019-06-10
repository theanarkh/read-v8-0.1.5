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

#ifndef V8_RUNTIME_H_
#define V8_RUNTIME_H_

namespace v8 { namespace internal {

// The interface to C++ runtime functions.

// ----------------------------------------------------------------------------
// RUNTIME_FUNCTION_LIST_ALWAYS defines runtime calls available in both
// release and debug mode.
// This macro should only be used by the macro RUNTIME_FUNCTION_LIST.

#define RUNTIME_FUNCTION_LIST_ALWAYS(F) \
  /* Property access */ \
  F(AddProperty, 4) \
  F(GetProperty, 2) \
  F(DeleteProperty, 2) \
  F(HasLocalProperty, 2) \
  F(HasProperty, 2) \
  F(HasElement, 2) \
  F(IsPropertyEnumerable, 2) \
  F(GetPropertyNames, 1) \
  F(GetPropertyNamesFast, 1) \
  F(GetArgumentsProperty, 1) \
  \
  F(IsInPrototypeChain, 2) \
  \
  F(IsConstructCall, 1) \
  \
  /* Utilities */ \
  F(GetBuiltins, 1) \
  F(GetCalledFunction, 1) \
  F(GetFunctionDelegate, 1) \
  F(NewArguments, 1) \
  F(LazyCompile, 1) \
  F(SetNewFunctionAttributes, 1) \
  \
  /* ConsStrings */ \
  F(ConsStringFst, 1) \
  F(ConsStringSnd, 1) \
  \
  /* Conversions */ \
  F(ToBool, 1) \
  F(Typeof, 1) \
  \
  F(StringToNumber, 1) \
  F(StringFromCharCodeArray, 1) \
  F(StringParseInt, 2) \
  F(StringParseFloat, 1) \
  F(StringToLowerCase, 1) \
  F(StringToUpperCase, 1) \
  F(CharFromCode, 1) \
  F(URIEscape, 1) \
  F(URIUnescape, 1) \
  \
  F(NumberToString, 1) \
  F(NumberToInteger, 1) \
  F(NumberToJSUint32, 1) \
  F(NumberToJSInt32, 1) \
  \
  /* Arithmetic operations */ \
  F(NumberAdd, 2) \
  F(NumberSub, 2) \
  F(NumberMul, 2) \
  F(NumberDiv, 2) \
  F(NumberMod, 2) \
  F(NumberAlloc, 1) \
  F(NumberUnaryMinus, 1) \
  \
  F(StringAdd, 2) \
  F(StringBuilderConcat, 2) \
  \
  /* Bit operations */ \
  F(NumberOr, 2) \
  F(NumberAnd, 2) \
  F(NumberXor, 2) \
  F(NumberNot, 1) \
  \
  F(NumberShl, 2) \
  F(NumberShr, 2) \
  F(NumberSar, 2) \
  \
  F(NumberIsNaN, 1) \
  F(NumberIsFinite, 1) \
  \
  /* Comparisons */ \
  F(ObjectEquals, 2) \
  F(NumberEquals, 2) \
  F(StringEquals, 2) \
  \
  F(NumberCompare, 3) \
  F(StringCompare, 2) \
  \
  /* Math */ \
  F(Math_abs, 1) \
  F(Math_acos, 1) \
  F(Math_asin, 1) \
  F(Math_atan, 1) \
  F(Math_atan2, 2) \
  F(Math_ceil, 1) \
  F(Math_cos, 1) \
  F(Math_exp, 1) \
  F(Math_floor, 1) \
  F(Math_log, 1) \
  F(Math_pow, 2) \
  F(Math_random, 1) \
  F(Math_round, 1) \
  F(Math_sin, 1) \
  F(Math_sqrt, 1) \
  F(Math_tan, 1) \
  \
  /* Regular expressions */ \
  F(RegExpCompile, 3) \
  F(RegExpExec, 3) \
  F(RegExpExecGlobal, 2) \
  \
  /* Strings */ \
  F(StringCharCodeAt, 2) \
  F(StringIndexOf, 3) \
  F(StringLastIndexOf, 3) \
  F(StringLocaleCompare, 2) \
  F(StringSlice, 3) \
  \
  /* Numbers */ \
  F(NumberToRadixString, 2) \
  F(NumberToFixed, 2) \
  F(NumberToExponential, 2) \
  F(NumberToPrecision, 2) \
  \
  /* Reflection */ \
  F(FunctionSetInstanceClassName, 2) \
  F(FunctionSetLength, 2) \
  F(FunctionSetPrototype, 2) \
  F(FunctionGetName, 1) \
  F(FunctionGetSourceCode, 1) \
  F(FunctionGetScript, 1) \
  F(FunctionGetScriptSourcePosition, 1) \
  F(GetScript, 1) \
  \
  F(ClassOf, 1) \
  F(SetCode, 2) \
  \
  F(CreateApiFunction, 1) \
  F(IsTemplate, 1) \
  F(GetTemplateField, 2) \
  \
  /* Dates */ \
  F(DateCurrentTime, 1) \
  F(DateParseString, 1) \
  F(DateLocalTimezone, 1) \
  F(DateLocalTimeOffset, 1) \
  F(DateDaylightSavingsOffset, 1) \
  \
  /* Numbers */ \
  F(NumberMaxValue, 1) \
  F(NumberMinValue, 1) \
  F(NumberNaN, 1) \
  F(NumberNegativeInfinity, 1) \
  F(NumberPositiveInfinity, 1) \
  \
  /* Globals */ \
  F(CompileString, 2) \
  F(CompileScript, 4) \
  F(GlobalPrint, 1) \
  \
  /* Eval */ \
  F(EvalReceiver, 1) \
  \
  F(SetProperty, -1 /* 3 or 4 */) \
  F(IgnoreAttributesAndSetProperty, 3) \
  \
  /* Arrays */ \
  F(RemoveArrayHoles, 1) \
  F(GetArrayKeys, 2) \
  F(MoveArrayContents, 2) \
  F(EstimateNumberOfElements, 1) \
  \
  /* Getters and Setters */ \
  F(DefineAccessor, -1 /* 4 or 5 */) \
  F(LookupAccessor, 3) \
  \
  /* Debugging */ \
  F(AddDebugEventListener, 2) \
  F(RemoveDebugEventListener, 1) \
  F(Break, 1) \
  F(DebugGetLocalPropertyDetails, 2) \
  F(DebugGetProperty, 2) \
  F(DebugLocalPropertyNames, 1) \
  F(DebugLocalElementNames, 1) \
  F(DebugPropertyTypeFromDetails, 1) \
  F(DebugPropertyAttributesFromDetails, 1) \
  F(DebugPropertyIndexFromDetails, 1) \
  F(DebugInterceptorInfo, 1) \
  F(DebugNamedInterceptorPropertyNames, 1) \
  F(DebugIndexedInterceptorElementNames, 1) \
  F(DebugNamedInterceptorPropertyValue, 2) \
  F(DebugIndexedInterceptorElementValue, 2) \
  F(CheckExecutionState, 1) \
  F(GetFrameCount, 1) \
  F(GetFrameDetails, 2) \
  F(GetCFrames, 1) \
  F(GetBreakLocations, 1) \
  F(SetFunctionBreakPoint, 3) \
  F(SetScriptBreakPoint, 3) \
  F(ClearBreakPoint, 1) \
  F(ChangeBreakOnException, 2) \
  F(PrepareStep, 3) \
  F(ClearStepping, 1) \
  F(DebugEvaluate, 4) \
  F(DebugEvaluateGlobal, 3) \
  F(DebugGetLoadedScripts, 1) \
  F(DebugReferencedBy, 3) \
  F(DebugConstructedBy, 2) \
  F(GetPrototype, 1) \
  F(SystemBreak, 1) \
  \
  /* Literals */ \
  F(MaterializeRegExpLiteral, 4)\
  F(CreateArrayLiteral, 1) \
  F(CreateObjectLiteralBoilerplate, 3) \
  F(CloneObjectLiteralBoilerplate, 1) \
  \
  /* Statements */ \
  F(NewClosure, 2) \
  F(NewObject, 1) \
  F(Throw, 1) \
  F(ReThrow, 1) \
  F(ThrowReferenceError, 1) \
  F(StackGuard, 1) \
  \
  /* Contexts */ \
  F(NewContext, 2) \
  F(PushContext, 2) \
  F(LookupContext, 2) \
  F(LoadContextSlot, 2) \
  F(LoadContextSlotNoReferenceError, 2) \
  F(StoreContextSlot, 3) \
  \
  /* Declarations and initialization */ \
  F(DeclareGlobals, 3) \
  F(DeclareContextSlot, 5) \
  F(InitializeVarGlobal, -1 /* 1 or 2 */) \
  F(InitializeConstGlobal, -1 /* 1 or 2 */) \
  F(InitializeConstContextSlot, 3) \
  \
  /* Debugging */ \
  F(DebugPrint, 1) \
  F(DebugTrace, 1) \
  F(TraceEnter, 1) \
  F(TraceExit, 1) \
  F(DebugBreak, 1) \
  F(FunctionGetAssemblerCode, 1) \
  F(Abort, 2) \
  \
  /* Pseudo functions - handled as macros by parser */ \
  F(IS_VAR, 1)


#ifdef DEBUG
#define RUNTIME_FUNCTION_LIST_DEBUG(F) \
  /* Testing */ \
  F(ListNatives, 1)
#else
#define RUNTIME_FUNCTION_LIST_DEBUG(F)
#endif


// ----------------------------------------------------------------------------
// RUNTIME_FUNCTION_LIST defines all runtime functions accessed
// either directly by id (via the code generator), or indirectly
// via a native call by name (from within JS code).

#define RUNTIME_FUNCTION_LIST(F) \
  RUNTIME_FUNCTION_LIST_ALWAYS(F) \
  RUNTIME_FUNCTION_LIST_DEBUG(F)

// ----------------------------------------------------------------------------
// Runtime provides access to all C++ runtime functions.

class Runtime : public AllStatic {
 public:
  enum FunctionId {
#define F(name, nargs) k##name,
    RUNTIME_FUNCTION_LIST(F)
    kNofFunctions
#undef F
  };
  static Object* CreateArrayLiteral(Arguments args);

  // Runtime function descriptor.
  struct Function {
    // The JS name of the function.
    const char* name;

    // The name of the stub that calls the runtime function.
    const char* stub_name;

    // The C++ (native) entry point.
    byte* entry;

    // The number of arguments expected; nargs < 0 if variable no. of
    // arguments.
    int nargs;
    int stub_id;
  };

  // Get the runtime function with the given function id.
  static Function* FunctionForId(FunctionId fid);

  // Get the runtime function with the given name.
  static Function* FunctionForName(const char* name);

  // TODO(1240886): The following three methods are *not* handle safe,
  // but accept handle arguments. This seems fragile.

  // Support getting the characters in a string using [] notation as
  // in Firefox/SpiderMonkey, Safari and Opera.
  static Object* GetElementOrCharAt(Handle<Object> object, uint32_t index);

  static Object* SetObjectProperty(Handle<Object> object,
                                   Handle<Object> key,
                                   Handle<Object> value,
                                   PropertyAttributes attr);

  static Object* GetObjectProperty(Handle<Object> object, Object* key);

  // Helper functions used stubs.
  static void PerformGC(Object* result);
};


} }  // namespace v8::internal

#endif  // V8_RUNTIME_H_
