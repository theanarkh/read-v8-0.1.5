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

// This file relies on the fact that the following declarations have been made
// in runtime.js:
// const $Array = global.Array;

// -------------------------------------------------------------------

// Determines if the array contains the element.
function Contains(array, element) {
  var length = array.length;
  for (var i = 0; i < length; i++) {
    if (array[i] === element) return true;
  }
  return false;
};


// Global list of arrays visited during toString, toLocaleString and
// join invocations.
var visited_arrays = new $Array();


// Gets a sorted array of array keys.  Useful for operations on sparse
// arrays.  Dupes have not been removed.
function GetSortedArrayKeys(array, intervals) {
  var length = intervals.length;
  var keys = [];
  for (var k = 0; k < length; k++) {
    var key = intervals[k];
    if (key < 0) {
      var j = -1 - key;
      var limit = j + intervals[++k];
      for (; j < limit; j++) {
        var e = array[j];
        if (!IS_UNDEFINED(e) || j in array) {
          keys.push(j);
        }
      }
    } else {
      // The case where key is undefined also ends here.
      if (!IS_UNDEFINED(key)) {
        var e = array[key];
        if (!IS_UNDEFINED(e) || key in array) {
          keys.push(key);
        }
      }
    }
  }
  keys.sort(function(a, b) { return a - b; });
  return keys;
}


// Optimized for sparse arrays if separator is ''.
function SparseJoin(array, len, convert) {
  var keys = GetSortedArrayKeys(array, %GetArrayKeys(array, len));
  var builder = new StringBuilder();
  var last_key = -1;
  var keys_length = keys.length;
  for (var i = 0; i < keys_length; i++) {
    var key = keys[i];
    if (key != last_key) {
      var e = array[key];
      builder.add(convert(e));
      last_key = key;
    }
  }
  return builder.generate();
}


function UseSparseVariant(object, length, is_array) {
   return is_array &&
       length > 1000 &&
       (!%_IsSmi(length) ||
        %EstimateNumberOfElements(object) < (length >> 2));
}


function Join(array, length, separator, convert) {
  if (length == 0) return '';

  var is_array = IS_ARRAY(array);

  if (is_array) {
    // If the array is cyclic, return the empty string for already
    // visited arrays.
    if (Contains(visited_arrays, array)) return '';
    visited_arrays[visited_arrays.length] = array;
  }

  // Attempt to convert the elements.
  try {
    if (UseSparseVariant(array, length, is_array) && separator === '') {
      return SparseJoin(array, length, convert);
    }

    var builder = new StringBuilder();

    for (var i = 0; i < length; i++) {
      var e = array[i];
      if (i != 0) builder.add(separator);
      if (!IS_UNDEFINED(e) || (i in array)) {
        builder.add(convert(e));
      }
    }
    return builder.generate();
  } finally {
    // Make sure to pop the visited array no matter what happens.
    if (is_array) visited_arrays.pop();
  }
};


function ConvertToString(e) {
  if (e == null) return '';
  else return ToString(e);
};


function ConvertToLocaleString(e) {
  if (e == null) return '';
  else {
    // e_obj's toLocaleString might be overwritten, check if it is a function.
    // Call ToString if toLocaleString is not a function.
    // See issue 877615.
    var e_obj = ToObject(e);
    if (IS_FUNCTION(e_obj.toLocaleString))
      return e_obj.toLocaleString();
    else
      return ToString(e);
  }
};


// This function implements the optimized splice implementation that can use
// special array operations to handle sparse arrays in a sensible fashion.
function SmartSlice(array, start_i, del_count, len, deleted_elements) {
  // Move deleted elements to a new array (the return value from splice).
  // Intervals array can contain keys and intervals.  See comment in Concat.
  var intervals = %GetArrayKeys(array, start_i + del_count);
  var length = intervals.length;
  for (var k = 0; k < length; k++) {
    var key = intervals[k];
    if (key < 0) {
      var j = -1 - key;
      var interval_limit = j + intervals[++k];
      if (j < start_i) {
        j = start_i;
      }
      for (; j < interval_limit; j++) {
        // ECMA-262 15.4.4.12 line 10.  The spec could also be
        // interpreted such that %HasLocalProperty would be the
        // appropriate test.  We follow KJS in consulting the
        // prototype.
        var current = array[j];
        if (!IS_UNDEFINED(current) || j in array) {
          deleted_elements[j - start_i] = current;
        }
      }
    } else {
      if (!IS_UNDEFINED(key)) {
        if (key >= start_i) {
          // ECMA-262 15.4.4.12 line 10.  The spec could also be
          // interpreted such that %HasLocalProperty would be the
          // appropriate test.  We follow KJS in consulting the
          // prototype.
          var current = array[key];
          if (!IS_UNDEFINED(current) || key in array) {
            deleted_elements[key - start_i] = current;
          }
        }
      }
    }
  }
};


// This function implements the optimized splice implementation that can use
// special array operations to handle sparse arrays in a sensible fashion.
function SmartMove(array, start_i, del_count, len, num_additional_args) {
  // Move data to new array.
  var new_array = new $Array(len - del_count + num_additional_args);
  var intervals = %GetArrayKeys(array, len);
  var length = intervals.length;
  for (var k = 0; k < length; k++) {
    var key = intervals[k];
    if (key < 0) {
      var j = -1 - key;
      var interval_limit = j + intervals[++k];
      while (j < start_i && j < interval_limit) {
        // The spec could also be interpreted such that
        // %HasLocalProperty would be the appropriate test.  We follow
        // KJS in consulting the prototype.
        var current = array[j];
        if (!IS_UNDEFINED(current) || j in array)
          new_array[j] = current;
        j++;
      }
      j = start_i + del_count;
      while (j < interval_limit) {
        // ECMA-262 15.4.4.12 lines 24 and 41.  The spec could also be
        // interpreted such that %HasLocalProperty would be the
        // appropriate test.  We follow KJS in consulting the
        // prototype.
        var current = array[j];
        if (!IS_UNDEFINED(current) || j in array)
          new_array[j - del_count + num_additional_args] = current;
        j++;
      }
    } else {
      if (!IS_UNDEFINED(key)) {
        if (key < start_i) {
          // The spec could also be interpreted such that
          // %HasLocalProperty would be the appropriate test.  We follow
          // KJS in consulting the prototype.
          var current = array[key];
          if (!IS_UNDEFINED(current) || key in array)
            new_array[key] = current;
        } else if (key >= start_i + del_count) {
          // ECMA-262 15.4.4.12 lines 24 and 41.  The spec could also
          // be interpreted such that %HasLocalProperty would be the
          // appropriate test.  We follow KJS in consulting the
          // prototype.
          var current = array[key];
          if (!IS_UNDEFINED(current) || key in array)
            new_array[key - del_count + num_additional_args] = current;
        }
      }
    }
  }
  // Move contents of new_array into this array
  %MoveArrayContents(new_array, array);
};


// This is part of the old simple-minded splice.  We are using it either
// because the receiver is not an array (so we have no choice) or because we
// know we are not deleting or moving a lot of elements.
function SimpleSlice(array, start_i, del_count, len, deleted_elements) {
  for (var i = 0; i < del_count; i++) {
    var index = start_i + i;
    // The spec could also be interpreted such that %HasLocalProperty
    // would be the appropriate test.  We follow KJS in consulting the
    // prototype.
    var current = array[index];
    if (!IS_UNDEFINED(current) || index in array)
      deleted_elements[i] = current;
  }
};


function SimpleMove(array, start_i, del_count, len, num_additional_args) {
  if (num_additional_args !== del_count) {
    // Move the existing elements after the elements to be deleted
    // to the right position in the resulting array.
    if (num_additional_args > del_count) {
      for (var i = len - del_count; i > start_i; i--) {
        var from_index = i + del_count - 1;
        var to_index = i + num_additional_args - 1;
        // The spec could also be interpreted such that
        // %HasLocalProperty would be the appropriate test.  We follow
        // KJS in consulting the prototype.
        var current = array[from_index];
        if (!IS_UNDEFINED(current) || from_index in array) {
          array[to_index] = current;
        } else {
          delete array[to_index];
        }
      }
    } else {
      for (var i = start_i; i < len - del_count; i++) {
        var from_index = i + del_count;
        var to_index = i + num_additional_args;
        // The spec could also be interpreted such that
        // %HasLocalProperty would be the appropriate test.  We follow
        // KJS in consulting the prototype.
        var current = array[from_index];
        if (!IS_UNDEFINED(current) || from_index in array) {
          array[to_index] = current;
        } else {
          delete array[to_index];
        }
      }
      for (var i = len; i > len - del_count + num_additional_args; i--) {
        delete array[i - 1];
      }
    }
  }
};


// -------------------------------------------------------------------


function ArrayToString() {
  if (!IS_ARRAY(this)) {
    throw new $TypeError('Array.prototype.toString is not generic');
  }
  return Join(this, this.length, ',', ConvertToString);
};


function ArrayToLocaleString() {
  if (!IS_ARRAY(this)) {
    throw new $TypeError('Array.prototype.toString is not generic');
  }
  return Join(this, this.length, ',', ConvertToLocaleString);
};


function ArrayJoin(separator) {
  if (IS_UNDEFINED(separator)) separator = ',';
  else separator = ToString(separator);
  return Join(this, ToUint32(this.length), separator, ConvertToString);
};


// Removes the last element from the array and returns it. See
// ECMA-262, section 15.4.4.6.
function ArrayPop() {
  var n = ToUint32(this.length);
  if (n == 0) {
    this.length = n;
    return;
  }
  n--;
  var value = this[n];
  this.length = n;
  delete this[n];
  return value;
};


// Appends the arguments to the end of the array and returns the new
// length of the array. See ECMA-262, section 15.4.4.7.
function ArrayPush() {
  var n = ToUint32(this.length);
  var m = %_ArgumentsLength();
  for (var i = 0; i < m; i++) {
    this[i+n] = %_Arguments(i);
  }
  this.length = n + m;
  return this.length;
};


function ArrayConcat(arg1) {  // length == 1
  var arg_number = 0, arg_count = %_ArgumentsLength();
  var n = 0;

  var A = $Array(1 + arg_count);
  var E = this;

  while (true) {
    if (IS_ARRAY(E)) {
      // This is an array of intervals or an array of keys.  Keys are
      // represented by non-negative integers.  Intervals are represented by
      // negative integers, followed by positive counts.  The interval start
      // is determined by subtracting the entry from -1.  There may also be
      // undefined entries in the array which should be skipped.
      var intervals = %GetArrayKeys(E, E.length);
      var length = intervals.length;
      for (var k = 0; k < length; k++) {
        var key = intervals[k];
        if (key < 0) {
          var j = -1 - key;
          var limit = j + intervals[++k];
          for (; j < limit; j++) {
            if (j in E) {
              A[n + j] = E[j];
            }
          }
        } else {
          // The case where key is undefined also ends here.
          if (!IS_UNDEFINED(key)) {
            A[n + key] = E[key];
          }
        }
      }
      n += E.length;
    } else {
      A[n++] = E;
    }
    if (arg_number == arg_count) break;
    E = %_Arguments(arg_number++);
  }

  A.length = n;  // may contain empty arrays
  return A;
};


// For implementing reverse() on large, sparse arrays.
function SparseReverse(array, len) {
  var keys = GetSortedArrayKeys(array, %GetArrayKeys(array, len));
  var high_counter = keys.length - 1;
  var low_counter = 0;
  while (low_counter <= high_counter) {
    var i = keys[low_counter];
    var j = keys[high_counter];

    var j_complement = len - j - 1;
    var low, high;

    if (j_complement <= i) {
      high = j;
      while (keys[--high_counter] == j);
      low = j_complement;
    }
    if (j_complement >= i) {
      low = i;
      while (keys[++low_counter] == i);
      high = len - i - 1;
    }

    var current_i = array[low];
    if (!IS_UNDEFINED(current_i) || low in array) {
      var current_j = array[high];
      if (!IS_UNDEFINED(current_j) || high in array) {
        array[low] = current_j;
        array[high] = current_i;
      } else {
        array[high] = current_i;
        delete array[low];
      }
    } else {
      var current_j = array[high];
      if (!IS_UNDEFINED(current_j) || high in array) {
        array[low] = current_j;
        delete array[high];
      }
    }
  }
}


function ArrayReverse() {
  var j = ToUint32(this.length) - 1;

  if (UseSparseVariant(this, j, IS_ARRAY(this))) {
    SparseReverse(this, j+1);
    return this;
  }

  for (var i = 0; i < j; i++, j--) {
    var current_i = this[i];
    if (!IS_UNDEFINED(current_i) || i in this) {
      var current_j = this[j];
      if (!IS_UNDEFINED(current_j) || j in this) {
        this[i] = current_j;
        this[j] = current_i;
      } else {
        this[j] = current_i;
        delete this[i];
      }
    } else {
      var current_j = this[j];
      if (!IS_UNDEFINED(current_j) || j in this) {
        this[i] = current_j;
        delete this[j];
      }
    }
  }
  return this;
};


function ArrayShift() {
  var len = ToUint32(this.length);
  
  if (len === 0) {
    this.length = 0;
    return;
  }
  
  var first = this[0];
  
  if (IS_ARRAY(this))
    SmartMove(this, 0, 1, len, 0);
  else
    SimpleMove(this, 0, 1, len, 0);
  
  this.length = len - 1;
  
  return first;
};


function ArrayUnshift(arg1) {  // length == 1
  var len = ToUint32(this.length);
  var num_arguments = %_ArgumentsLength();
  
  if (IS_ARRAY(this))
    SmartMove(this, 0, 0, len, num_arguments);
  else
    SimpleMove(this, 0, 0, len, num_arguments);
  
  for (var i = 0; i < num_arguments; i++) {
    this[i] = %_Arguments(i);
  }
  
  this.length = len + num_arguments;
  
  return len + num_arguments;
};


function ArraySlice(start, end) {
  var len = ToUint32(this.length);
  var start_i = TO_INTEGER(start);
  var end_i = len;
  
  if (end !== void 0) end_i = TO_INTEGER(end);
  
  if (start_i < 0) {
    start_i += len;
    if (start_i < 0) start_i = 0;
  } else {
    if (start_i > len) start_i = len;
  }
  
  if (end_i < 0) {
    end_i += len;
    if (end_i < 0) end_i = 0;
  } else {
    if (end_i > len) end_i = len;
  }
  
  var result = [];
  
  if (end_i < start_i)
    return result;
  
  if (IS_ARRAY(this))
    SmartSlice(this, start_i, end_i - start_i, len, result);
  else 
    SimpleSlice(this, start_i, end_i - start_i, len, result);
  
  result.length = end_i - start_i;
  
  return result;
};


function ArraySplice(start, delete_count) {
  var num_arguments = %_ArgumentsLength();
  
  // SpiderMonkey and KJS return undefined in the case where no
  // arguments are given instead of using the implicit undefined
  // arguments.  This does not follow ECMA-262, but we do the same for
  // compatibility.
  if (num_arguments == 0) return;
  
  var len = ToUint32(this.length);
  var start_i = TO_INTEGER(start);
  
  if (start_i < 0) {
    start_i += len;
    if (start_i < 0) start_i = 0;
  } else {
    if (start_i > len) start_i = len;
  }
  
  // SpiderMonkey and KJS treat the case where no delete count is
  // given differently from when an undefined delete count is given.
  // This does not follow ECMA-262, but we do the same for
  // compatibility.
  var del_count = 0;
  if (num_arguments > 1) {
    del_count = TO_INTEGER(delete_count);
    if (del_count < 0) del_count = 0;
    if (del_count > len - start_i) del_count = len - start_i;
  } else {
    del_count = len - start_i;
  }
  
  var deleted_elements = [];
  deleted_elements.length = del_count;
  
  // Number of elements to add.
  var num_additional_args = 0;
  if (num_arguments > 2) {
    num_additional_args = num_arguments - 2;
  }
  
  var use_simple_splice = true;
  
  if (IS_ARRAY(this) && num_additional_args !== del_count) {
    // If we are only deleting/moving a few things near the end of the
    // array then the simple version is going to be faster, because it
    // doesn't touch most of the array.
    var estimated_non_hole_elements = %EstimateNumberOfElements(this);
    if (len > 20 && (estimated_non_hole_elements >> 2) < (len - start_i)) {
      use_simple_splice = false;
    }
  }
  
  if (use_simple_splice) {
    SimpleSlice(this, start_i, del_count, len, deleted_elements);
    SimpleMove(this, start_i, del_count, len, num_additional_args);
  } else {
    SmartSlice(this, start_i, del_count, len, deleted_elements);
    SmartMove(this, start_i, del_count, len, num_additional_args);
  }
  
  // Insert the arguments into the resulting array in
  // place of the deleted elements.
  var i = start_i;
  var arguments_index = 2;
  var arguments_length = %_ArgumentsLength();
  while (arguments_index < arguments_length) {
    this[i++] = %_Arguments(arguments_index++);
  }
  this.length = len - del_count + num_additional_args;
  
  // Return the deleted elements.
  return deleted_elements;
};


function ArraySort(comparefn) {
  // Standard in-place HeapSort algorithm.

  function Compare(x,y) {
    if (IS_UNDEFINED(x)) {
      if (IS_UNDEFINED(y)) return 0;
      return 1;
    }
    if (IS_UNDEFINED(y)) return -1;

    if (IS_FUNCTION(comparefn)) {
      return comparefn.call(null, x, y);
    }
    x = ToString(x);
    y = ToString(y);
    if (x == y) return 0;
    else return x < y ? -1 : 1;
  };

  var old_length = ToUint32(this.length);

  %RemoveArrayHoles(this);

  var length = ToUint32(this.length);

  // Bottom-up max-heap construction.
  for (var i = 1; i < length; ++i) {
    var child_index = i;
    while (child_index > 0) {
      var parent_index = ((child_index + 1) >> 1) - 1;
      var parent_value = this[parent_index], child_value = this[child_index];
      if (Compare(parent_value, child_value) < 0) {
        this[parent_index] = child_value; 
        this[child_index] = parent_value;
      } else {
        break;
      }
      child_index = parent_index;
    }
  }

  // Extract element and create sorted array.
  for (var i = length - 1; i > 0; --i) {
    // Put the max element at the back of the array.
    var t0 = this[0]; this[0] = this[i]; this[i] = t0;
    // Sift down the new top element.
    var parent_index = 0;
    while (true) {
      var child_index = ((parent_index + 1) << 1) - 1;
      if (child_index >= i) break;
      var child1_value = this[child_index]; 
      var child2_value = this[child_index + 1];
      var parent_value = this[parent_index];
      if (child_index + 1 >= i || Compare(child1_value, child2_value) > 0) {
        if (Compare(parent_value, child1_value) > 0) break;
        this[child_index] = parent_value; 
        this[parent_index] = child1_value;
        parent_index = child_index;
      } else {
        if (Compare(parent_value, child2_value) > 0) break;
        this[child_index + 1] = parent_value; 
        this[parent_index] = child2_value;
        parent_index = child_index + 1;
      }
    }
  }

  // We only changed the length of the this object (in
  // RemoveArrayHoles) if it was an array.  We are not allowed to set
  // the length of the this object if it is not an array because this
  // might introduce a new length property.
  if (IS_ARRAY(this)) {
    this.length = old_length;
  }

  return this;
};


// The following functions cannot be made efficient on sparse arrays while
// preserving the semantics, since the calls to the receiver function can add
// or delete elements from the array.

function ArrayFilter(f, receiver) {
  if (!IS_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [ f ]);
  }
  // Pull out the length so that modifications to the length in the
  // loop will not affect the looping.
  var length = this.length;
  var result = [];
  for (var i = 0; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      if (f.call(receiver, current, i, this)) result.push(current);
    }
  }
  return result;
};


function ArrayForEach(f, receiver) {
  if (!IS_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [ f ]);
  }
  // Pull out the length so that modifications to the length in the
  // loop will not affect the looping.
  var length = this.length;
  for (var i = 0; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      f.call(receiver, current, i, this);
    }
  }
};


// Executes the function once for each element present in the
// array until it finds one where callback returns true.
function ArraySome(f, receiver) {
  if (!IS_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [ f ]);
  }
  // Pull out the length so that modifications to the length in the
  // loop will not affect the looping.
  var length = this.length;
  for (var i = 0; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      if (f.call(receiver, current, i, this)) return true;
    }
  }
  return false;
};


function ArrayEvery(f, receiver) {
  if (!IS_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [ f ]);
  }
  // Pull out the length so that modifications to the length in the
  // loop will not affect the looping.
  var length = this.length;
  for (var i = 0; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      if (!f.call(receiver, current, i, this)) return false;
    }
  }
  
  return true;
};


function ArrayMap(f, receiver) {
  if (!IS_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [ f ]);
  }
  // Pull out the length so that modifications to the length in the
  // loop will not affect the looping.
  var length = this.length;
  var result = new $Array(length);
  for (var i = 0; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      result[i] = f.call(receiver, current, i, this);
    }
  }
  return result;
};


function ArrayIndexOf(element, index) {
  var length = this.length;
  if (index == null) {
    index = 0;
  } else {
    index = TO_INTEGER(index);
    // If index is negative, index from the end of the array.
    if (index < 0) index = length + index;
    // If index is still negative, search the entire array.
    if (index < 0) index = 0;
  }
  // Lookup through the array.
  for (var i = index; i < length; i++) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      if (current === element) return i;
    }
  }
  return -1;
};


function ArrayLastIndexOf(element, index) {
  var length = this.length;
  if (index == null) {
    index = length - 1;
  } else {
    index = TO_INTEGER(index);
    // If index is negative, index from end of the array.
    if (index < 0) index = length + index;
    // If index is still negative, do not search the array.
    if (index < 0) index = -1;
    else if (index >= length) index = length - 1;
  }
  // Lookup through the array.
  for (var i = index; i >= 0; i--) {
    var current = this[i];
    if (!IS_UNDEFINED(current) || i in this) {
      if (current === element) return i;
    }
  }
  return -1;
};


// -------------------------------------------------------------------

function InstallProperties(prototype, attributes, properties) {
  for (var key in properties) {
    %AddProperty(prototype, key, properties[key], attributes);
  }
};


function UpdateFunctionLengths(lengths) {
  for (var key in lengths) {
    %FunctionSetLength(this[key], lengths[key]);
  }
};


// -------------------------------------------------------------------

function SetupArray() {
  // Setup non-enumerable properties of the Array.prototype object.
  InstallProperties($Array.prototype, DONT_ENUM, {
    constructor: $Array,
    toString: ArrayToString,
    toLocaleString: ArrayToLocaleString,
    join: ArrayJoin,
    pop: ArrayPop,
    push: ArrayPush,
    concat: ArrayConcat,
    reverse: ArrayReverse,
    shift: ArrayShift,
    unshift: ArrayUnshift,
    slice: ArraySlice,
    splice: ArraySplice,
    sort: ArraySort,
    filter: ArrayFilter,
    forEach: ArrayForEach,
    some: ArraySome,
    every: ArrayEvery,
    map: ArrayMap,
    indexOf: ArrayIndexOf,
    lastIndexOf: ArrayLastIndexOf
  });

  // Manipulate the length of some of the functions to meet
  // expectations set by ECMA-262 or Mozilla.
  UpdateFunctionLengths({
    ArrayFilter: 1,
    ArrayForEach: 1,
    ArraySome: 1,
    ArrayEvery: 1,
    ArrayMap: 1,
    ArrayIndexOf: 1,
    ArrayLastIndexOf: 1,
    ArrayPush: 1
  });
};


SetupArray();
