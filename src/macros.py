# Copyright 2006-2008 Google Inc. All Rights Reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Dictionary that is passed as defines for js2c.py.
# Used for defines that must be defined for all native js files.

const NONE = 0;
const READ_ONLY = 1;
const DONT_ENUM = 2;
const DONT_DELETE = 4;

# Constants used for getter and setter operations.
const GETTER = 0;
const SETTER = 1;

# These definitions must match the index of the properties in objects.h.
const kApiTagOffset =                  0;
const kApiPropertyListOffset =         1;
const kApiSerialNumberOffset =         2;
const kApiConstructorOffset =          2;
const kApiPrototypeTemplateOffset =    5;
const kApiParentTemplateOffset =       6;

const NO_HINT     = 0;
const NUMBER_HINT = 1;
const STRING_HINT = 2;

const kFunctionTag =          0;
const kNewObjectTag =         1;

# For date.js
const HoursPerDay =                24;
const MinutesPerHour =             60;
const SecondsPerMinute =           60;
const msPerSecond =              1000;
const msPerMinute =             60000;
const msPerHour =             3600000;
const msPerDay =             86400000;

# Note: kDayZeroInJulianDay = ToJulianDay(1970, 0, 1)
const kInvalidDate =   'Invalid Date';
const kDayZeroInJulianDay =   2440588;
const kMonthMask = 0x1e0;
const kDayMask   = 0x01f;
const kYearShift = 9;
const kMonthShift = 5;

# Type query macros
macro IS_NULL(arg)              = (arg === null);
macro IS_NULL_OR_UNDEFINED(arg) = (arg == null);
macro IS_UNDEFINED(arg)         = (typeof(arg) === 'undefined');
macro IS_FUNCTION(arg)          = (typeof(arg) === 'function');
macro IS_NUMBER(arg)            = (typeof(arg) === 'number');
macro IS_STRING(arg)            = (typeof(arg) === 'string');
macro IS_OBJECT(arg)            = (typeof(arg) === 'object');
macro IS_BOOLEAN(arg)           = (typeof(arg) === 'boolean');
macro IS_REGEXP(arg)            = (%ClassOf(arg) === 'RegExp');
macro IS_ARRAY(arg)             = (%ClassOf(arg) === 'Array');
macro IS_DATE(arg)              = (%ClassOf(arg) === 'Date');
macro IS_ERROR(arg)             = (%ClassOf(arg) === 'Error');
macro IS_SCRIPT(arg)            = (%ClassOf(arg) === 'Script');

# 'Inline' macros
# (Make sure arg is evaluated only once via %IS_VAR)
macro TO_INTEGER(arg)           = (%_IsSmi(%IS_VAR(arg)) ? arg : ToInteger(arg));
macro TO_INT32(arg)             = (%_IsSmi(%IS_VAR(arg)) ? arg : ToInt32(arg));

python macro CHAR_CODE(str)     = ord(str[1]);

# Accessors for original global properties that ensure they have been loaded.
const ORIGINAL_REGEXP     = (global.RegExp, $RegExp);
const ORIGINAL_DATE       = (global.Date, $Date);
