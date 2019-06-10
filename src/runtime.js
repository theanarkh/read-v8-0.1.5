// Copyright 2006-2007 Google Inc. All Rights Reserved.
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

// This files contains runtime support implemented in JavaScript.

// CAUTION: Some of the functions specified in this file are called
// directly from compiled code. These are the functions with names in
// ALL CAPS. The compiled code passes the first argument in 'this' and
// it does not push the function onto the stack. This means that you
// cannot use contexts in all these functions.


/* -----------------------------------
   - - -   C o m p a r i s o n   - - -
   -----------------------------------
*/

// The following const declarations are shared with other native JS files.
// They are all declared at this one spot to avoid const redeclaration errors.
const $Object = global.Object;
const $Array = global.Array;
const $String = global.String;
const $Number = global.Number;
const $Function = global.Function;
const $Boolean = global.Boolean;
const $NaN = %NumberNaN(1);


// ECMA-262, section 11.9.1, page 55.
function EQUALS(y) {
  var x = this;

  // NOTE: We use iteration instead of recursion, because it is
  // difficult to call EQUALS with the correct setting of 'this' in
  // an efficient way.
  
  while (true) {
    
    if (IS_NUMBER(x)) {
      if (y == null) return 1;  // not equal
      return %NumberEquals(x, %ToNumber(y));
      
    } else if (IS_STRING(x)) {
      if (IS_STRING(y)) return %StringEquals(x, y);
      if (IS_NUMBER(y)) return %NumberEquals(%ToNumber(x), y);
      if (IS_BOOLEAN(y)) return %NumberEquals(%ToNumber(x), %ToNumber(y));
      if (y == null) return 1;  // not equal
      y = %ToPrimitive(y, NO_HINT);

    } else if (IS_BOOLEAN(x)) {
      if (IS_BOOLEAN(y)) return %ObjectEquals(x, y);
      if (y == null) return 1;  // not equal
      return %NumberEquals(%ToNumber(x), %ToNumber(y));
      
    } else if (x == null) {
      // NOTE: This checks for both null and undefined.
      return (y == null) ? 0 : 1;
      
    } else {
      if (IS_OBJECT(y)) return %ObjectEquals(x, y);
      if (IS_FUNCTION(y)) return %ObjectEquals(x, y);
      x = %ToPrimitive(x, NO_HINT);
      
    }
  }
};


// ECMA-262, section 11.9.4, page 56.
function STRICT_EQUALS(x) {
  if (IS_NUMBER(this)) {
    if (!IS_NUMBER(x)) return 1;  // not equal
    return %NumberEquals(this, x);
  }
  
  if (IS_STRING(this)) {
    if (!IS_STRING(x)) return 1;  // not equal
    return %StringEquals(this, x);
  }
  
  if (IS_BOOLEAN(this)) {
    if (!IS_BOOLEAN(x)) return 1;  // not equal
    if (this) return x ? 0 : 1;
    else return x ? 1 : 0;
  }
  
  if (IS_UNDEFINED(this)) {  // both undefined and undetectable
    return IS_UNDEFINED(x) ? 0 : 1;
  }
  
  return %ObjectEquals(this, x);
};


// ECMA-262, section 11.8.5, page 53. The 'ncr' parameter is used as
// the result when either (or both) the operands are NaN.
function COMPARE(x, ncr) {
  // Improve performance for floating point compares
  if (IS_NUMBER(this) && IS_NUMBER(x)) {
    return %NumberCompare(this, x, ncr);
  }
  
  var a = %ToPrimitive(this, NUMBER_HINT);
  var b = %ToPrimitive(x, NUMBER_HINT);
  if (IS_STRING(a) && IS_STRING(b)) {
    return %StringCompare(a, b);
  } else {
    return %NumberCompare(%ToNumber(a), %ToNumber(b), ncr);
  }
};



/* -----------------------------------
   - - -   A r i t h m e t i c   - - -
   -----------------------------------
*/

// ECMA-262, section 11.6.1, page 50.
function ADD(x) {
  // Fast case: Check for number operands and do the addition.
  if (IS_NUMBER(this) && IS_NUMBER(x)) {
    return %NumberAdd(this, x);
  }
  
  var a = %ToPrimitive(this, NO_HINT);
  var b = %ToPrimitive(x, NO_HINT);
  
  if (IS_STRING(a)) {
    return %StringAdd(a, %ToString(b));
  } else if (IS_STRING(b)) {
    return %StringAdd(%ToString(a), b);
  } else {
    return %NumberAdd(%ToNumber(a), %ToNumber(b));
  }
};


// ECMA-262, section 11.6.2, page 50.
function SUB(x) {
  return %NumberSub(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.5.1, page 48.
function MUL(x) {
  return %NumberMul(%ToNumber(this), %ToNumber(x));
};


function MULNEG(x) {
  return %NumberUnaryMinus(%NumberMul(%ToNumber(this), %ToNumber(x)));
};


// ECMA-262, section 11.5.2, page 49.
function DIV(x) {
  return %NumberDiv(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.5.3, page 49.
function MOD(x) {
  return %NumberMod(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.4.4, page 47.
function INC() {
  return %NumberAdd(%ToNumber(this), 1);
};


// ECMA-262, section 11.4.5, page 48.
function DEC() {
  return %NumberSub(%ToNumber(this), 1);
};



/* -------------------------------------------
   - - -   B i t   o p e r a t i o n s   - - -
   -------------------------------------------
*/

// ECMA-262, section 11.10, page 57.
function BIT_OR(x) {
  return %NumberOr(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.10, page 57.
function BIT_AND(x) {
  return %NumberAnd(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.10, page 57.
function BIT_XOR(x) {
  return %NumberXor(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.4.7, page 47.
function UNARY_MINUS() {
  return %NumberUnaryMinus(%ToNumber(this));
};


// ECMA-262, section 11.4.8, page 48.
function BIT_NOT() {
  return %NumberNot(%ToNumber(this));
};


// ECMA-262, section 11.7.1, page 51.
function SHL(x) {
  return %NumberShl(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.7.2, page 51.
function SAR(x) {
  return %NumberSar(%ToNumber(this), %ToNumber(x));
};


// ECMA-262, section 11.7.3, page 52.
function SHR(x) {
  return %NumberShr(%ToNumber(this), %ToNumber(x));
};



/* -----------------------------
   - - -   H e l p e r s   - - -
   -----------------------------
*/

// ECMA-262, section 11.4.1, page 46.
function DELETE(key) {
  return %DeleteProperty(%ToObject(this), %ToString(key));
};


// ECMA-262, section 11.8.7, page 54.
function IN(x) {
  if (x == null || (!IS_OBJECT(x) && !IS_FUNCTION(x))) {
    throw %MakeTypeError('invalid_in_operator_use', [this, x]);
  }
  return %_IsSmi(this) ? %HasElement(x, this) : %HasProperty(x, %ToString(this));
};


// ECMA-262, section 11.8.6, page 54.
function INSTANCE_OF(F) {
  var V = this;
  if (!IS_FUNCTION(F)) {
    throw %MakeTypeError('instanceof_function_expected', [V]);
  }

  // If V is not an object, return false.
  if (IS_NULL(V) || (!IS_OBJECT(V) && !IS_FUNCTION(V))) {
    return false;
  }

  // Get the prototype of F; if it is not an object, throw an error.
  var O = F.prototype;
  if (IS_NULL(O) || (!IS_OBJECT(O) && !IS_FUNCTION(O))) {
    throw %MakeTypeError('instanceof_nonobject_proto', [O]);
  }

  // Return whether or not O is in the prototype chain of V.
  return %IsInPrototypeChain(O, V);
};


// Get an array of property keys for the given object. Used in
// for-in statements.
function GET_KEYS() {
  return %GetPropertyNames(this);
};


// Filter a given key against an object by checking if the object
// has a property with the given key; return the key as a string if
// it has. Otherwise returns null. Used in for-in statements.
function FILTER_KEY(key) {
  var string = %ToString(key);
  if (%HasProperty(this, string)) return string;
  return null;
};


function CALL_NON_FUNCTION() {
  var callee = %GetCalledFunction(0);
  var delegate = %GetFunctionDelegate(callee);
  if (!IS_FUNCTION(delegate)) {
    throw %MakeTypeError('called_non_callable', [typeof callee]);
  }

  var parameters = %NewArguments(delegate);
  return delegate.apply(callee, parameters);
};


function APPLY_PREPARE(args) {
  var length;
  // First check whether length is a positive Smi and args is an array.  This is the
  // fast case.  If this fails, we do the slow case that takes care of more eventualities
  if (%_IsArray(args)) {
    length = args.length;
    if (%_IsSmi(length) && length >= 0 && length < 0x800000 && IS_FUNCTION(this)) {
      return length;
    }
  }

  length = (args == null) ? 0 : %ToUint32(args.length);

  // We can handle any number of apply arguments if the stack is
  // big enough, but sanity check the value to avoid overflow when
  // multiplying with pointer size.
  if (length > 0x800000) {
    throw %MakeRangeError('apply_overflow', [length]);
  }

  if (!IS_FUNCTION(this)) {
    throw %MakeTypeError('apply_non_function', [ %ToString(this), typeof this ]);
  }

  // Make sure the arguments list has the right type.
  if (args != null &&
      %ClassOf(args) != 'Array' &&
      %ClassOf(args) != 'Arguments') {
    throw %MakeTypeError('apply_wrong_args', []);
  }
  
  // Return the length which is the number of arguments to copy to the
  // stack. It is guaranteed to be a small integer at this point.
  return length;
};


function APPLY_OVERFLOW(length) {
  throw %MakeRangeError('apply_overflow', [length]);
};


// Convert the receiver to an object - forward to ToObject.
function TO_OBJECT() {
  return %ToObject(this);
};


// Convert the receiver to a number - forward to ToNumber.
function TO_NUMBER() {
  return %ToNumber(this);
};


// Convert the receiver to a string - forward to ToString.
function TO_STRING() {
  return %ToString(this);
};


/* -------------------------------------
   - - -   C o n v e r s i o n s   - - -
   -------------------------------------
*/

// ECMA-262, section 9.1, page 30. Use null/undefined for no hint,
// (1) for number hint, and (2) for string hint.
function ToPrimitive(x, hint) {
  if (!IS_OBJECT(x) && !IS_FUNCTION(x)) return x;
  if (x == null) return x;  // check for null, undefined
  if (hint == NO_HINT) hint = (IS_DATE(x)) ? STRING_HINT : NUMBER_HINT;
  return (hint == NUMBER_HINT) ? %DefaultNumber(x) : %DefaultString(x);
};


// ECMA-262, section 9.3, page 31.
function ToNumber(x) {
  if (IS_NUMBER(x)) return x;
  if (IS_STRING(x)) return %StringToNumber(x);
  if (IS_BOOLEAN(x)) return x ? 1 : 0;
  if (IS_UNDEFINED(x)) return %NumberNaN(1);
  return (IS_NULL(x)) ? 0 : ToNumber(%DefaultNumber(x));
};


// ECMA-262, section 9.8, page 35.
function ToString(x) {
  if (IS_STRING(x)) return x;
  if (IS_NUMBER(x)) return %NumberToString(x);
  if (IS_BOOLEAN(x)) return x ? 'true' : 'false';
  if (IS_UNDEFINED(x)) return 'undefined';
  return (IS_NULL(x)) ? 'null' : %ToString(%DefaultString(x));
};


// ... where did this come from?
function ToBoolean(x) {
  if (IS_BOOLEAN(x)) return x;
  if (IS_STRING(x)) return x.length != 0;
  if (x == null) return false;
  if (IS_NUMBER(x)) return !((x == 0) || %NumberIsNaN(x));
  return true;
};


// ECMA-262, section 9.9, page 36.
function ToObject(x) {
  if (IS_STRING(x)) return new $String(x);
  if (IS_NUMBER(x)) return new $Number(x);
  if (IS_BOOLEAN(x)) return new $Boolean(x);
  if (x == null) throw %MakeTypeError('null_to_object');
  return x;
};


// ECMA-262, section 9.4, page 34.
function ToInteger(x) {
  if (%_IsSmi(x)) return x;
  return %NumberToInteger(ToNumber(x));
};


// ECMA-262, section 9.6, page 34.
function ToUint32(x) {
  return %NumberToJSUint32(ToNumber(x));
};


// ECMA-262, section 9.5, page 34
function ToInt32(x) {
  if (%_IsSmi(x)) return x;
  return %NumberToJSInt32(ToNumber(x));
};



/* ---------------------------------
   - - -   U t i l i t i e s   - - -
   ---------------------------------
*/

// Returns if the given x is a primitive value - not an object or a
// function.
function IsPrimitive(x) {
  if (!IS_OBJECT(x) && !IS_FUNCTION(x)) {
    return true;
  } else {
    // Even though the type of null is "object", null is still
    // considered a primitive value.
    return IS_NULL(x);
  }
};


// ECMA-262, section 8.6.2.6, page 28.
function DefaultNumber(x) {
  if (IS_FUNCTION(x.valueOf)) {
    var v = x.valueOf();
    if (%IsPrimitive(v)) return v;
  }

  if (IS_FUNCTION(x.toString)) {
    var s = x.toString();
    if (%IsPrimitive(s)) return s;
  }

  throw %MakeTypeError('cannot_convert_to_primitive', []);
};


// ECMA-262, section 8.6.2.6, page 28.
function DefaultString(x) {
  if (IS_FUNCTION(x.toString)) {
    var s = x.toString();
    if (%IsPrimitive(s)) return s;
  }

  if (IS_FUNCTION(x.valueOf)) {
    var v = x.valueOf();
    if (%IsPrimitive(v)) return v;
  }

  throw %MakeTypeError('cannot_convert_to_primitive', []);
};


// NOTE: Setting the prototype for Array must take place as early as
// possible due to code generation for array literals.  When
// generating code for a array literal a boilerplate array is created
// that is cloned when running the code.  It is essiential that the
// boilerplate gets the right prototype.
%FunctionSetPrototype($Array, new $Array(0));
