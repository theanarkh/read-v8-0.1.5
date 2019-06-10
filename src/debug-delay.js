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

// Default number of frames to include in the response to backtrace request.
const kDefaultBacktraceLength = 10;

const Debug = {};

// Regular expression to skip "crud" at the beginning of a source line which is
// not really code. Currently the regular expression matches whitespace and
// comments.
const sourceLineBeginningSkip = /^(?:[ \v\h]*(?:\/\*.*?\*\/)*)*/;

// Debug events which can occour in the V8 JavaScript engine. These originate
// from the API include file debug.h.
Debug.DebugEvent = { Break: 1,
                     Exception: 2,
                     NewFunction: 3,
                     BeforeCompile: 4,
                     AfterCompile: 5 };

// Types of exceptions that can be broken upon.
Debug.ExceptionBreak = { All : 0,
                         Uncaught: 1 };

// The different types of steps.
Debug.StepAction = { StepOut: 0,
                     StepNext: 1,
                     StepIn: 2,
                     StepMin: 3,
                     StepInMin: 4 };

// The different types of scripts matching enum ScriptType in objects.h.
Debug.ScriptType = { Native: 0,
                     Extension: 1,
                     Normal: 2 };

function ScriptTypeFlag(type) {
  return (1 << type);
}

// Globals.
var next_response_seq = 0;
var next_break_point_number = 1;
var break_points = [];
var script_break_points = [];


// Create a new break point object and add it to the list of break points.
function MakeBreakPoint(source_position, opt_line, opt_column, opt_script_break_point) {
  var break_point = new BreakPoint(source_position, opt_line, opt_column, opt_script_break_point);
  break_points.push(break_point);
  return break_point;
};


// Object representing a break point.
// NOTE: This object does not have a reference to the function having break
// point as this would cause function not to be garbage collected when it is
// not used any more. We do not want break points to keep functions alive.
function BreakPoint(source_position, opt_line, opt_column, opt_script_break_point) {
  this.source_position_ = source_position;
  this.source_line_ = opt_line;
  this.source_column_ = opt_column;
  if (opt_script_break_point) {
    this.script_break_point_ = opt_script_break_point;
  } else {
    this.number_ = next_break_point_number++;
  }
  this.hit_count_ = 0;
  this.active_ = true;
  this.condition_ = null;
  this.ignoreCount_ = 0;
};


BreakPoint.prototype.number = function() {
  return this.number_;
};


BreakPoint.prototype.func = function() {
  return this.func_;
};


BreakPoint.prototype.source_position = function() {
  return this.source_position_;
};


BreakPoint.prototype.hit_count = function() {
  return this.hit_count_;
};


BreakPoint.prototype.active = function() {
  if (this.script_break_point()) {
    return this.script_break_point().active();
  }
  return this.active_;
};


BreakPoint.prototype.condition = function() {
  if (this.script_break_point() && this.script_break_point().condition()) {
    return this.script_break_point().condition();
  }
  return this.condition_;
};


BreakPoint.prototype.ignoreCount = function() {
  return this.ignoreCount_;
};


BreakPoint.prototype.script_break_point = function() {
  return this.script_break_point_;
};


BreakPoint.prototype.enable = function() {
  this.active_ = true;
};


BreakPoint.prototype.disable = function() {
  this.active_ = false;
};


BreakPoint.prototype.setCondition = function(condition) {
  this.condition_ = condition;
};


BreakPoint.prototype.setIgnoreCount = function(ignoreCount) {
  this.ignoreCount_ = ignoreCount;
};


BreakPoint.prototype.isTriggered = function(exec_state) {
  // Break point not active - not triggered.
  if (!this.active()) return false;

  // Check for conditional break point.
  if (this.condition()) {
    // If break point has condition try to evaluate it in the top frame.
    try {
      var mirror = exec_state.GetFrame(0).evaluate(this.condition());
      // If no sensible mirror or non true value break point not triggered.
      if (!(mirror instanceof ValueMirror) || !%ToBoolean(mirror.value_)) {
        return false;
      }
    } catch (e) {
      // Exception evaluating condition counts as not triggered.
      return false;
    }
  }

  // Update the hit count.
  this.hit_count_++;
  if (this.script_break_point_) {
    this.script_break_point_.hit_count_++;
  }

  // If the break point has an ignore count it is not triggered.
  if (this.ignoreCount_ > 0) {
    this.ignoreCount_--;
    return false;
  }

  // Break point triggered.
  return true;
};


// Function called from the runtime when a break point is hit. Returns true if
// the break point is triggered and supposed to break execution.
function IsBreakPointTriggered(break_id, break_point) {
  return break_point.isTriggered(MakeExecutionState(break_id));
};


// Object representing a script break point. The script is referenced by its
// script name and the break point is represented as line and column.
function ScriptBreakPoint(script_name, opt_line, opt_column) {
  this.script_name_ = script_name;
  this.line_ = opt_line || 0;
  this.column_ = opt_column;
  this.hit_count_ = 0;
  this.active_ = true;
  this.condition_ = null;
  this.ignoreCount_ = 0;
};


ScriptBreakPoint.prototype.number = function() {
  return this.number_;
};


ScriptBreakPoint.prototype.script_name = function() {
  return this.script_name_;
};


ScriptBreakPoint.prototype.line = function() {
  return this.line_;
};


ScriptBreakPoint.prototype.column = function() {
  return this.column_;
};


ScriptBreakPoint.prototype.hit_count = function() {
  return this.hit_count_;
};


ScriptBreakPoint.prototype.active = function() {
  return this.active_;
};


ScriptBreakPoint.prototype.condition = function() {
  return this.condition_;
};


ScriptBreakPoint.prototype.ignoreCount = function() {
  return this.ignoreCount_;
};


ScriptBreakPoint.prototype.enable = function() {
  this.active_ = true;
};


ScriptBreakPoint.prototype.disable = function() {
  this.active_ = false;
};


ScriptBreakPoint.prototype.setCondition = function(condition) {
  this.condition_ = condition;
};


ScriptBreakPoint.prototype.setIgnoreCount = function(ignoreCount) {
  this.ignoreCount_ = ignoreCount;

  // Set ignore count on all break points created from this script break point.
  for (var i = 0; i < break_points.length; i++) {
    if (break_points[i].script_break_point() === this) {
      break_points[i].setIgnoreCount(ignoreCount);
    }
  }
};


// Check whether a script matches this script break point. Currently this is
// only based on script name.
ScriptBreakPoint.prototype.matchesScript = function(script) {
  return this.script_name_ == script.name &&
         script.line_offset <= this.line_  &&
         this.line_ < script.line_offset + script.lineCount();
};


// Set the script break point in a script.
ScriptBreakPoint.prototype.set = function (script) {
  var column = this.column();
  var line = this.line();
  // If the column is undefined the break is on the line. To help locate the
  // first piece of breakable code on the line try to find the column on the
  // line which contains some source.
  if (IS_UNDEFINED(column)) {
    var source_line = script.sourceLine(this.line());

    // Allocate array for caching the columns where the actual source starts.
    if (!script.sourceColumnStart_) {
      script.sourceColumnStart_ = new Array(script.lineCount());
    }
    
    // Fill cache if needed and get column where the actual source starts.
    if (IS_UNDEFINED(script.sourceColumnStart_[line])) {
      script.sourceColumnStart_[line] =
          source_line.match(sourceLineBeginningSkip)[0].length;
    }
    column = script.sourceColumnStart_[line];
  }

  // Convert the line and column into an absolute position within the script.
  var pos = Debug.findScriptSourcePosition(script, this.line(), column);
  
  // Create a break point object and set the break point.
  break_point = MakeBreakPoint(pos, this.line(), this.column(), this);
  break_point.setIgnoreCount(this.ignoreCount());
  %SetScriptBreakPoint(script, pos, break_point);

  return break_point;
};


// Clear all the break points created from this script break point
ScriptBreakPoint.prototype.clear = function () {
  var remaining_break_points = [];
  for (var i = 0; i < break_points.length; i++) {
    if (break_points[i].script_break_point() &&
        break_points[i].script_break_point() === this) {
      %ClearBreakPoint(break_points[i]);
    } else {
      remaining_break_points.push(break_points[i]);
    }
  }
  break_points = remaining_break_points;
};


// Function called from runtime when a new script is compiled to set any script
// break points set in this script.
function UpdateScriptBreakPoints(script) {
  for (var i = 0; i < script_break_points.length; i++) {
    if (script_break_points[i].script_name() == script.name) {
      script_break_points[i].set(script);
    }
  }
};


// Function called from the runtime to handle a debug request receiced from the
// debugger. When this function is called the debugger is in the broken state
// reflected by the exec_state parameter. When pending requests are handled the
// parameter stopping indicate the expected running state.
function ProcessDebugRequest(exec_state, request, stopping) {
  return exec_state.debugCommandProcessor().processDebugJSONRequest(request, stopping);
}


// Helper function to check whether the JSON request is a plain break request.
// This is used form the runtime handling of pending debug requests. If one of
// the pending requests is a plain break execution should be broken after
// processing the pending break requests.
function IsPlainBreakRequest(json_request) {
  try {
    // Convert the JSON string to an object.
    request = %CompileString('(' + json_request + ')', false)();

    // Check for break command without arguments.
    return request.command && request.command == "break" && !request.arguments;
  } catch (e) {
    // If there is a exception parsing the JSON request just return false.
    return false;
  }
}


Debug.addListener = function(listener, opt_data) {
  if (!IS_FUNCTION(listener)) throw new Error('Parameters have wrong types.');
  %AddDebugEventListener(listener, opt_data);
};

Debug.removeListener = function(listener) {
  if (!IS_FUNCTION(listener)) throw new Error('Parameters have wrong types.');
  %RemoveDebugEventListener(listener);
};

Debug.Break = function(f) {
  %Break(0);
};

Debug.breakLocations = function(f) {
  if (!IS_FUNCTION(f)) throw new Error('Parameters have wrong types.');
  return %GetBreakLocations(f);
};

// Returns a Script object. If the parameter is a function the return value
// is the script in which the function is defined. If the parameter is a string
// the return value is the script for which the script name has that string
// value.
Debug.findScript = function(func_or_script_name) {
  if (IS_FUNCTION(func_or_script_name)) {
    return %FunctionGetScript(func_or_script_name);
  } else {
    return %GetScript(func_or_script_name);
  }
};

// Returns the script source. If the parameter is a function the return value
// is the script source for the script in which the function is defined. If the
// parameter is a string the return value is the script for which the script
// name has that string value.
Debug.scriptSource = function(func_or_script_name) {
  return this.findScript(func_or_script_name).source;
};

Debug.source = function(f) {
  if (!IS_FUNCTION(f)) throw new Error('Parameters have wrong types.');
  return %FunctionGetSourceCode(f);
};

Debug.assembler = function(f) {
  if (!IS_FUNCTION(f)) throw new Error('Parameters have wrong types.');
  return %FunctionGetAssemblerCode(f);
};

Debug.sourcePosition = function(f) {
  if (!IS_FUNCTION(f)) throw new Error('Parameters have wrong types.');
  return %FunctionGetScriptSourcePosition(f);
};

Debug.findFunctionSourcePosition = function(func, opt_line, opt_column) {
  var script = %FunctionGetScript(func);
  var script_offset = %FunctionGetScriptSourcePosition(func);
  return script.locationFromLine(opt_line, opt_column, script_offset).position;
}


// Returns the character position in a script based on a line number and an
// optional position within that line.
Debug.findScriptSourcePosition = function(script, opt_line, opt_column) {
  return script.locationFromLine(opt_line, opt_column).position;
}


Debug.findBreakPoint = function(break_point_number, remove) {
  var break_point;
  for (var i = 0; i < break_points.length; i++) {
    if (break_points[i].number() == break_point_number) {
      break_point = break_points[i];
      // Remove the break point from the list if requested.
      if (remove) {
        break_points.splice(i, 1);
      }
      break;
    }
  }
  if (break_point) {
    return break_point;
  } else {
    return this.findScriptBreakPoint(break_point_number, remove);
  }
};


Debug.setBreakPoint = function(func, opt_line, opt_column, opt_condition) {
  if (!IS_FUNCTION(func)) throw new Error('Parameters have wrong types.');
  var source_position = this.findFunctionSourcePosition(func, opt_line, opt_column) -
                        this.sourcePosition(func);
  // Find the script for the function.
  var script = %FunctionGetScript(func);
  // If the script for the function has a name convert this to a script break
  // point.
  if (script && script.name) {
    // Adjust the source position to be script relative.
    source_position += %FunctionGetScriptSourcePosition(func);
    // Find line and column for the position in the script and set a script
    // break point from that.
    var location = script.locationFromPosition(source_position);
    return this.setScriptBreakPoint(script.name,
                                    location.line, location.column,
                                    opt_condition);
  } else {
    // Set a break point directly on the function.
    var break_point = MakeBreakPoint(source_position, opt_line, opt_column);
    %SetFunctionBreakPoint(func, source_position, break_point);
    break_point.setCondition(opt_condition);
    return break_point.number();
  }
};


Debug.enableBreakPoint = function(break_point_number) {
  var break_point = this.findBreakPoint(break_point_number, false);
  break_point.enable();
};


Debug.disableBreakPoint = function(break_point_number) {
  var break_point = this.findBreakPoint(break_point_number, false);
  break_point.disable();
};


Debug.changeBreakPointCondition = function(break_point_number, condition) {
  var break_point = this.findBreakPoint(break_point_number, false);
  break_point.setCondition(condition);
};


Debug.changeBreakPointIgnoreCount = function(break_point_number, ignoreCount) {
  if (ignoreCount < 0) {
    throw new Error('Invalid argument');
  }
  var break_point = this.findBreakPoint(break_point_number, false);
  break_point.setIgnoreCount(ignoreCount);
};


Debug.clearBreakPoint = function(break_point_number) {
  var break_point = this.findBreakPoint(break_point_number, true);
  if (break_point) {
    return %ClearBreakPoint(break_point);
  } else {
    break_point = this.findScriptBreakPoint(break_point_number, true);
    if (!break_point) {
      throw new Error('Invalid breakpoint');
    }
  }
};


Debug.clearAllBreakPoints = function() {
  for (var i = 0; i < break_points.length; i++) {
    break_point = break_points[i];
    %ClearBreakPoint(break_point);
  }
  break_points = [];
};


Debug.findScriptBreakPoint = function(break_point_number, remove) {
  var script_break_point;
  for (var i = 0; i < script_break_points.length; i++) {
    if (script_break_points[i].number() == break_point_number) {
      script_break_point = script_break_points[i];
      // Remove the break point from the list if requested.
      if (remove) {
        script_break_point.clear();
        script_break_points.splice(i,1);
      }
      break;
    }
  }
  return script_break_point;
}


// Sets a breakpoint in a script identified through script name at the
// specified source line and column within that line.
Debug.setScriptBreakPoint = function(script_name, opt_line, opt_column, opt_condition) {
  // Create script break point object.
  var script_break_point = new ScriptBreakPoint(script_name, opt_line, opt_column);

  // Assign number to the new script break point and add it.
  script_break_point.number_ = next_break_point_number++;
  script_break_point.setCondition(opt_condition);
  script_break_points.push(script_break_point);

  // Run through all scripts to see it this script break point matches any
  // loaded scripts.
  var scripts = this.scripts();
  for (var i = 0; i < scripts.length; i++) {
    if (script_break_point.matchesScript(scripts[i])) {
      script_break_point.set(scripts[i]);
    }
  }

  return script_break_point.number();
}


Debug.enableScriptBreakPoint = function(break_point_number) {
  var script_break_point = this.findScriptBreakPoint(break_point_number, false);
  script_break_point.enable();
};


Debug.disableScriptBreakPoint = function(break_point_number) {
  var script_break_point = this.findScriptBreakPoint(break_point_number, false);
  script_break_point.disable();
};


Debug.changeScriptBreakPointCondition = function(break_point_number, condition) {
  var script_break_point = this.findScriptBreakPoint(break_point_number, false);
  script_break_point.setCondition(condition);
};


Debug.changeScriptBreakPointIgnoreCount = function(break_point_number, ignoreCount) {
  if (ignoreCount < 0) {
    throw new Error('Invalid argument');
  }
  var script_break_point = this.findScriptBreakPoint(break_point_number, false);
  script_break_point.setIgnoreCount(ignoreCount);
};


Debug.scriptBreakPoints = function() {
  return script_break_points;
}


Debug.clearStepping = function() {
  %ClearStepping(0);
}

Debug.setBreakOnException = function() {
  return %ChangeBreakOnException(Debug.ExceptionBreak.All, true);
};

Debug.clearBreakOnException = function() {
  return %ChangeBreakOnException(Debug.ExceptionBreak.All, false);
};

Debug.setBreakOnUncaughtException = function() {
  return %ChangeBreakOnException(Debug.ExceptionBreak.Uncaught, true);
};

Debug.clearBreakOnUncaughtException = function() {
  return %ChangeBreakOnException(Debug.ExceptionBreak.Uncaught, false);
};

Debug.showBreakPoints = function(f, full) {
  if (!IS_FUNCTION(f)) throw new Error('Parameters have wrong types.');
  var source = full ? this.scriptSource(f) : this.source(f);
  var offset = full ? this.sourcePosition(f) : 0;
  var locations = this.breakLocations(f);
  if (!locations) return source;
  locations.sort(function(x, y) { return x - y; });
  var result = "";
  var prev_pos = 0;
  var pos;
  for (var i = 0; i < locations.length; i++) {
    pos = locations[i] - offset;
    result += source.slice(prev_pos, pos);
    result += "[B" + i + "]";
    prev_pos = pos;
  }
  pos = source.length;
  result += source.substring(prev_pos, pos);
  return result;
};


// Get all the scripts currently loaded. Locating all the scripts is based on
// scanning the heap.
Debug.scripts = function() {
  // Collect all scripts in the heap.
  return %DebugGetLoadedScripts(0);
}

function MakeExecutionState(break_id) {
  return new ExecutionState(break_id);
};

function ExecutionState(break_id) {
  this.break_id = break_id;
  this.selected_frame = 0;
};

ExecutionState.prototype.prepareStep = function(opt_action, opt_count) {
  var action = Debug.StepAction.StepIn;
  if (!IS_UNDEFINED(opt_action)) action = %ToNumber(opt_action);
  var count = opt_count ? %ToNumber(opt_count) : 1;

  return %PrepareStep(this.break_id, action, count);
}

ExecutionState.prototype.evaluateGlobal = function(source, disable_break) {
  return %DebugEvaluateGlobal(this.break_id, source, Boolean(disable_break));
};

ExecutionState.prototype.GetFrameCount = function() {
  return %GetFrameCount(this.break_id);
};

ExecutionState.prototype.GetFrame = function(opt_index) {
  // If no index supplied return the selected frame.
  if (opt_index == null) opt_index = this.selected_frame;
  return new FrameMirror(this.break_id, opt_index);
};

ExecutionState.prototype.cframesValue = function(opt_from_index, opt_to_index) {
  return %GetCFrames(this.break_id);
};

ExecutionState.prototype.setSelectedFrame = function(index) {
  var i = %ToNumber(index);
  if (i < 0 || i >= this.GetFrameCount()) throw new Error('Illegal frame index.');
  this.selected_frame = i;
};

ExecutionState.prototype.getSelectedFrame = function() {
  return this.selected_frame;
};

ExecutionState.prototype.debugCommandProcessor = function(protocol) {
  return new DebugCommandProcessor(this, protocol);
};


function MakeBreakEvent(exec_state, break_points_hit) {
  return new BreakEvent(exec_state, break_points_hit);
};


function BreakEvent(exec_state, break_points_hit) {
  this.exec_state_ = exec_state;
  this.break_points_hit_ = break_points_hit;
};


BreakEvent.prototype.func = function() {
  return this.exec_state_.GetFrame(0).func();
};


BreakEvent.prototype.sourceLine = function() {
  return this.exec_state_.GetFrame(0).sourceLine();
};


BreakEvent.prototype.sourceColumn = function() {
  return this.exec_state_.GetFrame(0).sourceColumn();
};


BreakEvent.prototype.sourceLineText = function() {
  return this.exec_state_.GetFrame(0).sourceLineText();
};


BreakEvent.prototype.breakPointsHit = function() {
  return this.break_points_hit_;
};


BreakEvent.prototype.details = function() {
  // Build the break details.
  var details = '';
  if (this.breakPointsHit()) {
    details += 'breakpoint';
    if (this.breakPointsHit().length > 1) {
      details += 's';
    }
    details += ' ';
    for (var i = 0; i < this.breakPointsHit().length; i++) {
      if (i > 0) {
        details += ',';
      }
      details += this.breakPointsHit()[i].number();
    }
  } else {
    details += 'break';
  }
  details += ' in ';
  details += this.exec_state_.GetFrame(0).invocationText();
  details += ' at ';
  details += this.exec_state_.GetFrame(0).sourceAndPositionText();
  details += '\n'
  if (this.func().script()) {
    details += FrameSourceUnderline(this.exec_state_.GetFrame(0));
  }
  return details;
};


BreakEvent.prototype.debugPrompt = function() {
  // Build the debug break prompt.
  if (this.breakPointsHit()) {
    return 'breakpoint';
  } else {
    return 'break';
  }
};


BreakEvent.prototype.toJSONProtocol = function() {
  var o = { seq: next_response_seq++,
            type: "event",
            event: "break",
            body: { invocationText: this.exec_state_.GetFrame(0).invocationText(),
                  }
          }

  // Add script related information to the event if available.
  var script = this.func().script();
  if (script) {
    o.body.sourceLine = this.sourceLine(),
    o.body.sourceColumn = this.sourceColumn(),
    o.body.sourceLineText = this.sourceLineText(),
    o.body.script = { name: script.name(),
                      lineOffset: script.lineOffset(),
                      columnOffset: script.columnOffset(),
                      lineCount: script.lineCount()
                    };
  }

  // Add an Array of break points hit if any.
  if (this.breakPointsHit()) {
    o.body.breakpoints = [];
    for (var i = 0; i < this.breakPointsHit().length; i++) {
      // Find the break point number. For break points originating from a
      // script break point supply the script break point number.
      var breakpoint = this.breakPointsHit()[i];
      var script_break_point = breakpoint.script_break_point();
      var number;
      if (script_break_point) {
        number = script_break_point.number();
      } else {
        number = breakpoint.number();
      }
      o.body.breakpoints.push(number);
    }
  }

  return SimpleObjectToJSON_(o);
};


function MakeExceptionEvent(exec_state, exception, uncaught) {
  return new ExceptionEvent(exec_state, exception, uncaught);
};

function ExceptionEvent(exec_state, exception, uncaught) {
  this.exec_state_ = exec_state;
  this.exception_ = exception;
  this.uncaught_ = uncaught;
};

ExceptionEvent.prototype.uncaught = function() {
  return this.uncaught_;
}

ExceptionEvent.prototype.func = function() {
  return this.exec_state_.GetFrame(0).func();
};


ExceptionEvent.prototype.sourceLine = function() {
  return this.exec_state_.GetFrame(0).sourceLine();
};


ExceptionEvent.prototype.sourceColumn = function() {
  return this.exec_state_.GetFrame(0).sourceColumn();
};


ExceptionEvent.prototype.sourceLineText = function() {
  return this.exec_state_.GetFrame(0).sourceLineText();
};


ExceptionEvent.prototype.details = function() {
  var details = "";
  if (this.uncaught_) {
    details += "Uncaught: ";
  } else {
    details += "Exception: ";
  }

  details += '"';
  details += MakeMirror(this.exception_).toText();
  details += '" at ';
  details += this.exec_state_.GetFrame(0).sourceAndPositionText();
  details += '\n';
  details += FrameSourceUnderline(this.exec_state_.GetFrame(0));

  return details;
};

ExceptionEvent.prototype.debugPrompt = function() {
  if (this.uncaught_) {
    return "uncaught exception";
  } else {
    return "exception";
  }
};

ExceptionEvent.prototype.toJSONProtocol = function() {
  var o = { seq: next_response_seq++,
            type: "event",
            event: "exception",
            body: { uncaught: this.uncaught_,
                    exception: MakeMirror(this.exception_),
                    sourceLine: this.sourceLine(),
                    sourceColumn: this.sourceColumn(),
                    sourceLineText: this.sourceLineText(),
                  }
          }

  // Add script information to the event if available.
  var script = this.func().script();
  if (script) {
    o.body.script = { name: script.name(),
                      lineOffset: script.lineOffset(),
                      columnOffset: script.columnOffset(),
                      lineCount: script.lineCount()
                    };
  }

  return SimpleObjectToJSON_(o);
};

function MakeCompileEvent(script_source, script_name, script_function) {
  return new CompileEvent(script_source, script_name, script_function);
};

function CompileEvent(script_source, script_name, script_function) {
  this.scriptSource = script_source;
  this.scriptName = script_name;
  this.scriptFunction = script_function;
};

CompileEvent.prototype.details = function() {
  var result = "";
  result = "Script added"
  if (this.scriptData) {
    result += ": '";
    result += this.scriptData;
    result += "'";
  }
  return result;
};

CompileEvent.prototype.debugPrompt = function() {
  var result = "source"
  if (this.scriptData) {
    result += " '";
    result += this.scriptData;
    result += "'";
  }
  if (this.func) {
    result += " added";
  } else {
    result += " compiled";
  }
  return result;
};

function MakeNewFunctionEvent(func) {
  return new NewFunctionEvent(func);
};

function NewFunctionEvent(func) {
  this.func = func;
};

NewFunctionEvent.prototype.details = function() {
  var result = "";
  result = "Function added: ";
  result += this.func.name;
  return result;
};

NewFunctionEvent.prototype.debugPrompt = function() {
  var result = "function";
  if (this.func.name) {
    result += " '";
    result += this.func.name;
    result += "'";
  }
  result += " added";
  return result;
};

NewFunctionEvent.prototype.name = function() {
  return this.func.name;
};

NewFunctionEvent.prototype.setBreakPoint = function(p) {
  Debug.setBreakPoint(this.func, p || 0);
};

function DebugCommandProcessor(exec_state) {
  this.exec_state_ = exec_state;
};


// Convenience function for C debugger code to process a text command. This
// function converts the text command to a JSON request, performs the request
// and converts the request to a text result for display. The result is an
// object containing the text result and the intermediate results.
DebugCommandProcessor.prototype.processDebugCommand = function (command) {
  var request;
  var response;
  var text_result;
  var running;

  request = this.commandToJSONRequest(command);
  response = this.processDebugJSONRequest(request);
  text_result = this.responseToText(response);
  running = this.isRunning(response);

  return { "request"       : request,
           "response"      : response,
           "text_result"   : text_result,
           "running"       : running };
}


// Converts a text command to a JSON request.
DebugCommandProcessor.prototype.commandToJSONRequest = function(cmd_line) {
  // If the wery first character is a { assume that a JSON request have been
  // entered as a command. Converting that to a JSON request is trivial.
  if (cmd_line && cmd_line.length > 0 && cmd_line.charAt(0) == '{') {
    return cmd_line;
  }

  // Trim string for leading and trailing whitespace.
  cmd_line = cmd_line.replace(/^\s+|\s+$/g, "");

  // Find the command.
  var pos = cmd_line.indexOf(" ");
  var cmd;
  var args;
  if (pos == -1) {
    cmd = cmd_line;
    args = "";
  } else {
    cmd = cmd_line.slice(0, pos);
    args = cmd_line.slice(pos).replace(/^\s+|\s+$/g, "");
  }

  // Switch on command.
  if (cmd == 'continue' || cmd == 'c') {
    return this.continueCommandToJSONRequest_(args);
  } else if (cmd == 'step' || cmd == 's') {
    return this.stepCommandToJSONRequest_(args);
  } else if (cmd == 'backtrace' || cmd == 'bt') {
    return this.backtraceCommandToJSONRequest_(args);
  } else if (cmd == 'frame' || cmd == 'f') {
    return this.frameCommandToJSONRequest_(args);
  } else if (cmd == 'print' || cmd == 'p') {
    return this.printCommandToJSONRequest_(args);
  } else if (cmd == 'source') {
    return this.sourceCommandToJSONRequest_(args);
  } else if (cmd == 'scripts') {
    return this.scriptsCommandToJSONRequest_(args);
  } else if (cmd[0] == '{') {
    return cmd_line;
  } else {
    throw new Error('Unknown command "' + cmd + '"');
  }
};


// Create a JSON request for the continue command.
DebugCommandProcessor.prototype.continueCommandToJSONRequest_ = function(args) {
  var request = this.createRequest('continue');
  return request.toJSONProtocol();
};


// Create a JSON request for the step command.
DebugCommandProcessor.prototype.stepCommandToJSONRequest_ = function(args) {
  // Requesting a step is through the continue command with additional
  // arguments.
  var request = this.createRequest('continue');
  request.arguments = {};

  // Process arguments if any.
  if (args && args.length > 0) {
    args = args.split(/\s*[ ]+\s*/g);

    if (args.length > 2) {
      throw new Error('Invalid step arguments.');
    }

    if (args.length > 0) {
      // Get step count argument if any.
      if (args.length == 2) {
        request.arguments.stepcount = %ToNumber(args[1]);
      }

      // Get the step action.
      if (args[0] == 'in' || args[0] == 'i') {
        request.arguments.stepaction = 'in';
      } else if (args[0] == 'min' || args[0] == 'm') {
        request.arguments.stepaction = 'min';
      } else if (args[0] == 'next' || args[0] == 'n') {
        request.arguments.stepaction = 'next';
      } else if (args[0] == 'out' || args[0] == 'o') {
        request.arguments.stepaction = 'out';
      } else {
        throw new Error('Invalid step argument "' + args[0] + '".');
      }
    }
  } else {
    // Default is step next.
    request.arguments.stepaction = 'next';
  }

  return request.toJSONProtocol();
};


// Create a JSON request for the backtrace command.
DebugCommandProcessor.prototype.backtraceCommandToJSONRequest_ = function(args) {
  // Build a backtrace request from the text command.
  var request = this.createRequest('backtrace');
  args = args.split(/\s*[ ]+\s*/g);
  if (args.length == 2) {
    request.arguments = {};
    request.arguments.fromFrame = %ToNumber(args[0]);
    request.arguments.toFrame = %ToNumber(args[1]) + 1;
  }
  return request.toJSONProtocol();
};


// Create a JSON request for the frame command.
DebugCommandProcessor.prototype.frameCommandToJSONRequest_ = function(args) {
  // Build a frame request from the text command.
  var request = this.createRequest('frame');
  args = args.split(/\s*[ ]+\s*/g);
  if (args.length > 0 && args[0].length > 0) {
    request.arguments = {};
    request.arguments.number = args[0];
  }
  return request.toJSONProtocol();
};


// Create a JSON request for the print command.
DebugCommandProcessor.prototype.printCommandToJSONRequest_ = function(args) {
  // Build a evaluate request from the text command.
  var request = this.createRequest('evaluate');
  if (args.length == 0) {
    throw new Error('Missing expression.');
  }

  request.arguments = {};
  request.arguments.expression = args;

  return request.toJSONProtocol();
};


// Create a JSON request for the source command.
DebugCommandProcessor.prototype.sourceCommandToJSONRequest_ = function(args) {
  // Build a evaluate request from the text command.
  var request = this.createRequest('source');

  // Default is one line before and two lines after current location.
  var before = 1;
  var after = 2;

  // Parse the arguments.
  args = args.split(/\s*[ ]+\s*/g);
  if (args.length > 1 && args[0].length > 0 && args[1].length > 0) {
    before = %ToNumber(args[0]);
    after = %ToNumber(args[1]);
  } else if (args.length > 0 && args[0].length > 0) {
    after = %ToNumber(args[0]);
  }

  // Request source arround current source location.
  request.arguments = {};
  request.arguments.fromLine = this.exec_state_.GetFrame().sourceLine() - before;
  if (request.arguments.fromLine < 0) {
    request.arguments.fromLine = 0
  }
  request.arguments.toLine = this.exec_state_.GetFrame().sourceLine() + after + 1;

  return request.toJSONProtocol();
};


// Create a JSON request for the scripts command.
DebugCommandProcessor.prototype.scriptsCommandToJSONRequest_ = function(args) {
  // Build a evaluate request from the text command.
  var request = this.createRequest('scripts');

  // Process arguments if any.
  if (args && args.length > 0) {
    args = args.split(/\s*[ ]+\s*/g);

    if (args.length > 1) {
      throw new Error('Invalid scripts arguments.');
    }

    request.arguments = {};
    if (args[0] == 'natives') {
      request.arguments.types = ScriptTypeFlag(Debug.ScriptType.Native);
    } else if (args[0] == 'extensions') {
      request.arguments.types = ScriptTypeFlag(Debug.ScriptType.Extension);
    } else if (args[0] == 'all') {
      request.arguments.types =
          ScriptTypeFlag(Debug.ScriptType.Normal) |
          ScriptTypeFlag(Debug.ScriptType.Native) |
          ScriptTypeFlag(Debug.ScriptType.Extension);
    } else {
      throw new Error('Invalid argument "' + args[0] + '".');
    }
  }

  return request.toJSONProtocol();
};


// Convert a JSON response to text for display in a text based debugger.
DebugCommandProcessor.prototype.responseToText = function(json_response) {
  try {
    // Convert the JSON string to an object.
    response = %CompileString('(' + json_response + ')', false)();

    if (!response.success) {
      return response.message;
    }

    if (response.command == 'backtrace') {
      var body = response.body;
      var result = 'Frames #' + body.fromFrame + ' to #' +
          (body.toFrame - 1) + ' of ' + body.totalFrames + '\n';
      for (i = 0; i < body.frames.length; i++) {
        if (i != 0) result += '\n';
        result += body.frames[i].text;
      }
      return result;
    } else if (response.command == 'frame') {
      return SourceUnderline(response.body.sourceLineText,
                             response.body.column);
    } else if (response.command == 'evaluate') {
      return response.body.text;
    } else if (response.command == 'source') {
      // Get the source from the response.
      var source = response.body.source;

      // Get rid of last line terminator.
      var remove_count = 0;
      if (source[source.length - 1] == '\n') remove_count++;
      if (source[source.length - 2] == '\r') remove_count++;
      if (remove_count > 0) source = source.substring(0, source.length - remove_count);

      return source;
    } else if (response.command == 'scripts') {
      var result = '';
      for (i = 0; i < response.body.length; i++) {
        if (i != 0) result += '\n';
        if (response.body[i].name) {
          result += response.body[i].name;
        } else {
          result += '[unnamed] ';
          var sourceStart = response.body[i].sourceStart;
          if (sourceStart.length > 40) {
            sourceStart = sourceStart.substring(0, 37) + '...';
          }
          result += sourceStart;
        }
        result += ' (lines: ';
        result += response.body[i].sourceLines;
        result += ', length: ';
        result += response.body[i].sourceLength;
        if (response.body[i].type == Debug.ScriptType.Native) {
          result += ', native';
        } else if (response.body[i].type == Debug.ScriptType.Extension) {
          result += ', extension';
        }
        result += ')';
      }
      return result;
    }
  } catch (e) {
    return 'Error: "' + %ToString(e) + '" formatting response';
  }
};


function SourceUnderline(source_text, position) {
  if (IS_UNDEFINED(source_text)) {
    return;
  }

  // Create an underline with a caret pointing to the source position. If the
  // source contains a tab character the underline will have a tab character in
  // the same place otherwise the underline will have a space character.
  var underline = '';
  for (var i = 0; i < position; i++) {
    if (source_text[i] == '\t') {
      underline += '\t';
    } else {
      underline += ' ';
    }
  }
  underline += '^';

  // Return the source line text with the underline beneath.
  return source_text + '\n' + underline;
};


function FrameSourceUnderline(frame) {
  var location = frame.sourceLocation();
  if (location) {
    return SourceUnderline(location.sourceText(), location.position - location.start);
  }
};


function RequestPacket(command) {
  this.seq = 0;
  this.type = 'request';
  this.command = command;
};


RequestPacket.prototype.toJSONProtocol = function() {
  // Encode the protocol header.
  var json = '{';
  json += '"seq":' + this.seq;
  json += ',"type":"' + this.type + '"';
  if (this.command) {
    json += ',"command":' + StringToJSON_(this.command);
  }
  if (this.arguments) {
    json += ',"arguments":';
    // Encode the arguments part.
    if (this.arguments.toJSONProtocol) {
      json += this.arguments.toJSONProtocol()
    } else {
      json += SimpleObjectToJSON_(this.arguments);
    }
  }
  json += '}';
  return json;
}


DebugCommandProcessor.prototype.createRequest = function(command) {
  return new RequestPacket(command);
};


function ResponsePacket(request) {
  // Build the initial response from the request.
  this.seq = next_response_seq++;
  this.type = 'response';
  if (request) this.request_seq = request.seq;
  if (request) this.command = request.command;
  this.success = true;
  this.running = false;
};


ResponsePacket.prototype.failed = function(message) {
  this.success = false;
  this.message = message;
}


ResponsePacket.prototype.toJSONProtocol = function() {
  // Encode the protocol header.
  var json = '{';
  json += '"seq":' + this.seq;
  if (this.request_seq) {
    json += ',"request_seq":' + this.request_seq;
  }
  json += ',"type":"' + this.type + '"';
  if (this.command) {
    json += ',"command":' + StringToJSON_(this.command);
  }
  if (this.success) {
    json += ',"success":' + this.success;
  } else {
    json += ',"success":false';
  }
  if (this.body) {
    json += ',"body":';
    // Encode the body part.
    if (this.body.toJSONProtocol) {
      json += this.body.toJSONProtocol(true);
    } else if (this.body instanceof Array) {
      json += '[';
      for (var i = 0; i < this.body.length; i++) {
        if (i != 0) json += ',';
        if (this.body[i].toJSONProtocol) {
          json += this.body[i].toJSONProtocol(true)
        } else {
          json += SimpleObjectToJSON_(this.body[i]);
        }
      }
      json += ']';
    } else {
      json += SimpleObjectToJSON_(this.body);
    }
  }
  if (this.message) {
    json += ',"message":' + StringToJSON_(this.message) ;
  }
  if (this.running) {
    json += ',"running":true';
  } else {
    json += ',"running":false';
  }
  json += '}';
  return json;
}


DebugCommandProcessor.prototype.createResponse = function(request) {
  return new ResponsePacket(request);
};


DebugCommandProcessor.prototype.processDebugJSONRequest = function(json_request, stopping) {
  var request;  // Current request.
  var response;  // Generated response.
  try {
    try {
      // Convert the JSON string to an object.
      request = %CompileString('(' + json_request + ')', false)();

      // Create an initial response.
      response = this.createResponse(request);

      if (!request.type) {
        throw new Error('Type not specified');
      }

      if (request.type != 'request') {
        throw new Error("Illegal type '" + request.type + "' in request");
      }

      if (!request.command) {
        throw new Error('Command not specified');
      }

      if (request.command == 'continue') {
        this.continueRequest_(request, response);
      } else if (request.command == 'break') {
        this.breakRequest_(request, response);
      } else if (request.command == 'setbreakpoint') {
        this.setBreakPointRequest_(request, response);
      } else if (request.command == 'changebreakpoint') {
        this.changeBreakPointRequest_(request, response);
      } else if (request.command == 'clearbreakpoint') {
        this.clearBreakPointRequest_(request, response);
      } else if (request.command == 'backtrace') {
        this.backtraceRequest_(request, response);
      } else if (request.command == 'frame') {
        this.frameRequest_(request, response);
      } else if (request.command == 'evaluate') {
        this.evaluateRequest_(request, response);
      } else if (request.command == 'source') {
        this.sourceRequest_(request, response);
      } else if (request.command == 'scripts') {
        this.scriptsRequest_(request, response);
      } else {
        throw new Error('Unknown command "' + request.command + '" in request');
      }
    } catch (e) {
      // If there is no response object created one (without command).
      if (!response) {
        response = this.createResponse();
      }
      response.success = false;
      response.message = %ToString(e);
    }

    // Return the response as a JSON encoded string.
    try {
      // Set the running state to what indicated.
      if (!IS_UNDEFINED(stopping)) {
        response.running = !stopping;
      }
      return response.toJSONProtocol();
    } catch (e) {
      // Failed to generate response - return generic error.
      return '{"seq":' + response.seq + ',' +
              '"request_seq":' + request.seq + ',' +
              '"type":"response",' +
              '"success":false,' +
              '"message":"Internal error: ' + %ToString(e) + '"}';
    }
  } catch (e) {
    // Failed in one of the catch blocks above - most generic error.
    return '{"seq":0,"type":"response","success":false,"message":"Internal error"}';
  }
};


DebugCommandProcessor.prototype.continueRequest_ = function(request, response) {
  // Check for arguments for continue.
  if (request.arguments) {
    var count = 1;
    var action = Debug.StepAction.StepIn;

    // Pull out arguments.
    var stepaction = request.arguments.stepaction;
    var stepcount = request.arguments.stepcount;

    // Get the stepcount argument if any.
    if (stepcount) {
      count = %ToNumber(stepcount);
      if (count < 0) {
        throw new Error('Invalid stepcount argument "' + stepcount + '".');
      }
    }

    // Get the stepaction argument.
    if (stepaction) {
      if (stepaction == 'in') {
        action = Debug.StepAction.StepIn;
      } else if (stepaction == 'min') {
        action = Debug.StepAction.StepMin;
      } else if (stepaction == 'next') {
        action = Debug.StepAction.StepNext;
      } else if (stepaction == 'out') {
        action = Debug.StepAction.StepOut;
      } else {
        throw new Error('Invalid stepaction argument "' + stepaction + '".');
      }
    }

    // Setup the VM for stepping.
    this.exec_state_.prepareStep(action, count);
  }

  // VM should be running after executing this request.
  response.running = true;
};


DebugCommandProcessor.prototype.breakRequest_ = function(request, response) {
  // Ignore as break command does not do anything when broken.
};


DebugCommandProcessor.prototype.setBreakPointRequest_ =
    function(request, response) {
  // Check for legal request.
  if (!request.arguments) {
    response.failed('Missing arguments');
    return;
  }

  // Pull out arguments.
  var type = request.arguments.type;
  var target = request.arguments.target;
  var line = request.arguments.line;
  var column = request.arguments.column;
  var enabled = IS_UNDEFINED(request.arguments.enabled) ?
      true : request.arguments.enabled;
  var condition = request.arguments.condition;
  var ignoreCount = request.arguments.ignoreCount;

  // Check for legal arguments.
  if (!type || !target) {
    response.failed('Missing argument "type" or "target"');
    return;
  }
  if (type != 'function' && type != 'script') {
    response.failed('Illegal type "' + type + '"');
    return;
  }

  // Either function or script break point.
  var break_point_number;
  if (type == 'function') {
    // Handle function break point.
    if (!IS_STRING(target)) {
      response.failed('Argument "target" is not a string value');
      return;
    }
    var f;
    try {
      // Find the function through a global evaluate.
      f = this.exec_state_.evaluateGlobal(target);
    } catch (e) {
      response.failed('Error: "' + %ToString(e) +
                      '" evaluating "' + target + '"');
      return;
    }
    if (!IS_FUNCTION(f)) {
      response.failed('"' + target + '" does not evaluate to a function');
      return;
    }

    // Set function break point.
    break_point_number = Debug.setBreakPoint(f, line, column, condition);
  } else {
    // set script break point.
    break_point_number = Debug.setScriptBreakPoint(target,
                                                   line, column,
                                                   condition);
  }

  // Set additional break point properties.
  var break_point = Debug.findBreakPoint(break_point_number);
  if (ignoreCount) {
    Debug.changeBreakPointIgnoreCount(break_point_number, ignoreCount);
  }
  if (!enabled) {
    Debug.disableBreakPoint(break_point_number);
  }

  // Add the break point number to the response.
  response.body = { type: type,
                    breakpoint: break_point_number }

  // Add break point information to the response.
  if (break_point instanceof ScriptBreakPoint) {
    response.body.type = 'script';
    response.body.script_name = break_point.script_name();
    response.body.line = break_point.line();
    response.body.column = break_point.column();
  } else {
    response.body.type = 'function';
  }
};


DebugCommandProcessor.prototype.changeBreakPointRequest_ = function(request, response) {
  // Check for legal request.
  if (!request.arguments) {
    response.failed('Missing arguments');
    return;
  }

  // Pull out arguments.
  var break_point = %ToNumber(request.arguments.breakpoint);
  var enabled = request.arguments.enabled;
  var condition = request.arguments.condition;
  var ignoreCount = request.arguments.ignoreCount;

  // Check for legal arguments.
  if (!break_point) {
    response.failed('Missing argument "breakpoint"');
    return;
  }

  // Change enabled state if supplied.
  if (!IS_UNDEFINED(enabled)) {
    if (enabled) {
      Debug.enableBreakPoint(break_point);
    } else {
      Debug.disableBreakPoint(break_point);
    }
  }

  // Change condition if supplied
  if (!IS_UNDEFINED(condition)) {
    Debug.changeBreakPointCondition(break_point, condition);
  }

  // Change ignore count if supplied
  if (!IS_UNDEFINED(ignoreCount)) {
    Debug.changeBreakPointIgnoreCount(break_point, ignoreCount);
  }
}


DebugCommandProcessor.prototype.clearBreakPointRequest_ = function(request, response) {
  // Check for legal request.
  if (!request.arguments) {
    response.failed('Missing arguments');
    return;
  }

  // Pull out arguments.
  var break_point = %ToNumber(request.arguments.breakpoint);

  // Check for legal arguments.
  if (!break_point) {
    response.failed('Missing argument "breakpoint"');
    return;
  }

  // Clear break point.
  Debug.clearBreakPoint(break_point);
}


DebugCommandProcessor.prototype.backtraceRequest_ = function(request, response) {
  // Get the number of frames.
  var total_frames = this.exec_state_.GetFrameCount();

  // Default frame range to include in backtrace.
  var from_index = 0
  var to_index = kDefaultBacktraceLength;

  // Get the range from the arguments.
  if (request.arguments) {
    from_index = request.arguments.fromFrame;
    if (from_index < 0) {
      return response.failed('Invalid frame number');
    }
    to_index = request.arguments.toFrame;
    if (to_index < 0) {
      return response.failed('Invalid frame number');
    }
  }

  // Adjust the index.
  to_index = Math.min(total_frames, to_index);

  if (to_index <= from_index) {
    var error = 'Invalid frame range';
    return response.failed(error);
  }

  // Create the response body.
  var frames = [];
  for (var i = from_index; i < to_index; i++) {
    frames.push(this.exec_state_.GetFrame(i));
  }
  response.body = {
    fromFrame: from_index,
    toFrame: to_index,
    totalFrames: total_frames,
    frames: frames
  }
};


DebugCommandProcessor.prototype.backtracec = function(cmd, args) {
  return this.exec_state_.cframesValue();
};


DebugCommandProcessor.prototype.frameRequest_ = function(request, response) {
  // With no arguments just keep the selected frame.
  if (request.arguments && request.arguments.number >= 0) {
    this.exec_state_.setSelectedFrame(request.arguments.number);
  }
  response.body = this.exec_state_.GetFrame();
};


DebugCommandProcessor.prototype.evaluateRequest_ = function(request, response) {
  if (!request.arguments) {
    return response.failed('Missing arguments');
  }

  // Pull out arguments.
  var expression = request.arguments.expression;
  var frame = request.arguments.frame;
  var global = request.arguments.global;
  var disable_break = request.arguments.disable_break;

  // The expression argument could be an integer so we convert it to a
  // string.
  try {
    expression = String(expression);
  } catch(e) {
    return response.failed('Failed to convert expression argument to string');
  }

  // Check for legal arguments.
  if (!IS_UNDEFINED(frame) && global) {
    return response.failed('Arguments "frame" and "global" are exclusive');
  }

  // Global evaluate.
  if (global) {
    // Evaluate in the global context.
    response.body = MakeMirror(
        this.exec_state_.evaluateGlobal(expression), Boolean(disable_break));
    return;
  }

  // Default value for disable_break is true.
  if (IS_UNDEFINED(disable_break)) {
    disable_break = true;
  }

  // Check whether a frame was specified.
  if (!IS_UNDEFINED(frame)) {
    var frame_number = %ToNumber(frame);
    if (frame_number < 0 || frame_number >= this.exec_state_.GetFrameCount()) {
      return response.failed('Invalid frame "' + frame + '"');
    }
    // Evaluate in the specified frame.
    response.body = this.exec_state_.GetFrame(frame_number).evaluate(
        expression, Boolean(disable_break));
    return;
  } else {
    // Evaluate in the selected frame.
    response.body = this.exec_state_.GetFrame().evaluate(
        expression, Boolean(disable_break));
    return;
  }
};


DebugCommandProcessor.prototype.sourceRequest_ = function(request, response) {
  var from_line;
  var to_line;
  var frame = this.exec_state_.GetFrame();
  if (request.arguments) {
    // Pull out arguments.
    from_line = request.arguments.fromLine;
    to_line = request.arguments.toLine;

    if (!IS_UNDEFINED(request.arguments.frame)) {
      var frame_number = %ToNumber(request.arguments.frame);
      if (frame_number < 0 || frame_number >= this.exec_state_.GetFrameCount()) {
        return response.failed('Invalid frame "' + frame + '"');
      }
      frame = this.exec_state_.GetFrame(frame_number);
    }
  }

  // Get the script selected.
  var script = frame.func().script();
  if (!script) {
    return response.failed('No source');
  }

  // Get the source slice and fill it into the response.
  var slice = script.sourceSlice(from_line, to_line);
  if (!slice) {
    return response.failed('Invalid line interval');
  }
  response.body = {};
  response.body.source = slice.sourceText();
  response.body.fromLine = slice.from_line;
  response.body.toLine = slice.to_line;
  response.body.fromPosition = slice.from_position;
  response.body.toPosition = slice.to_position;
  response.body.totalLines = script.lineCount();
};


DebugCommandProcessor.prototype.scriptsRequest_ = function(request, response) {
  var types = ScriptTypeFlag(Debug.ScriptType.Normal);
  if (request.arguments) {
    // Pull out arguments.
    if (!IS_UNDEFINED(request.arguments.types)) {
      types = %ToNumber(request.arguments.types);
      if (isNaN(types) || types < 0) {
        return response.failed('Invalid types "' + request.arguments.types + '"');
      }
    }
  }

  // Collect all scripts in the heap.
  var scripts = %DebugGetLoadedScripts(0);

  response.body = [];

  for (var i = 0; i < scripts.length; i++) {
    if (types & ScriptTypeFlag(scripts[i].type)) {
      var script = {};
      if (scripts[i].name) {
        script.name = scripts[i].name;
      }
      script.lineOffset = scripts[i].line_offset;
      script.columnOffset = scripts[i].column_offset;
      script.lineCount = scripts[i].lineCount();
      script.sourceStart = scripts[i].source.substring(0, 80);
      script.sourceLength = scripts[i].source.length;
      script.type = scripts[i].type;
      response.body.push(script);
    }
  }
};


// Check whether the JSON response indicate that the VM should be running.
DebugCommandProcessor.prototype.isRunning = function(json_response) {
  try {
    // Convert the JSON string to an object.
    response = %CompileString('(' + json_response + ')', false)();

    // Return whether VM should be running after this request.
    return response.running;

  } catch (e) {
     return false;
  }
}


DebugCommandProcessor.prototype.systemBreak = function(cmd, args) {
  return %SystemBreak(0);
};


function NumberToHex8Str(n) {
  var r = "";
  for (var i = 0; i < 8; ++i) {
    var c = hexCharArray[n & 0x0F];  // hexCharArray is defined in uri.js
    r = c + r;
    n = n >>> 4;
  }
  return r;
};

DebugCommandProcessor.prototype.formatCFrames = function(cframes_value) {
  var result = "";
  if (cframes_value == null || cframes_value.length == 0) {
    result += "(stack empty)";
  } else {
    for (var i = 0; i < cframes_value.length; ++i) {
      if (i != 0) result += "\n";
      result += this.formatCFrame(cframes_value[i]);
    }
  }
  return result;
};


DebugCommandProcessor.prototype.formatCFrame = function(cframe_value) {
  var result = "";
  result += "0x" + NumberToHex8Str(cframe_value.address);
  if (!IS_UNDEFINED(cframe_value.text)) {
    result += " " + cframe_value.text;
  }
  return result;
}


/**
 * Convert an Object to its JSON representation (see http://www.json.org/).
 * This implementation simply runs through all string property names and adds
 * each property to the JSON representation for some predefined types. For type
 * "object" the function calls itself recursively unless the object has the
 * function property "toJSONProtocol" in which case that is used. This is not
 * a general implementation but sufficient for the debugger. Note that circular
 * structures will cause infinite recursion.
 * @param {Object} object The object to format as JSON
 * @return {string} JSON formatted object value
 */
function SimpleObjectToJSON_(object) {
  var content = [];
  for (var key in object) {
    // Only consider string keys.
    if (typeof key == 'string') {
      var property_value = object[key];

      // Format the value based on its type.
      var property_value_json;
      switch (typeof property_value) {
        case 'object':
          if (typeof property_value.toJSONProtocol == 'function') {
            property_value_json = property_value.toJSONProtocol(true)
          } else if (IS_ARRAY(property_value)){
            property_value_json = SimpleArrayToJSON_(property_value);
          } else {
            property_value_json = SimpleObjectToJSON_(property_value);
          }
          break;

        case 'boolean':
          property_value_json = BooleanToJSON_(property_value);
          break;

        case 'number':
          property_value_json = NumberToJSON_(property_value);
          break;

        case 'string':
          property_value_json = StringToJSON_(property_value);
          break;

        default:
          property_value_json = null;
      }

      // Add the property if relevant.
      if (property_value_json) {
        content.push(StringToJSON_(key) + ':' + property_value_json);
      }
    }
  }

  // Make JSON object representation.
  return '{' + content.join(',') + '}';
};

/**
 * Convert an array to its JSON representation. This is a VERY simple
 * implementation just to support what is needed for the debugger.
 * @param {Array} arrya The array to format as JSON
 * @return {string} JSON formatted array value
 */
function SimpleArrayToJSON_(array) {
  // Make JSON array representation.
  var json = '[';
  for (var i = 0; i < array.length; i++) {
    if (i != 0) {
      json += ',';
    }
    var elem = array[i];
    if (elem.toJSONProtocol) {
      json += elem.toJSONProtocol(true)
    } else if (IS_OBJECT(elem))  {
      json += SimpleObjectToJSON_(elem);
    } else if (IS_BOOLEAN(elem)) {
      json += BooleanToJSON_(elem);
    } else if (IS_NUMBER(elem)) {
      json += NumberToJSON_(elem);
    } else if (IS_STRING(elem)) {
      json += StringToJSON_(elem);
    } else {
      json += elem;
    }
  }
  json += ']';
  return json;
};
