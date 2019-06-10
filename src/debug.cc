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

#include "v8.h"

#include "api.h"
#include "arguments.h"
#include "bootstrapper.h"
#include "code-stubs.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "natives.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

DEFINE_bool(remote_debugging, false, "enable remote debugging");
DEFINE_int(debug_port, 5858, "port for remote debugging");
DEFINE_bool(trace_debug_json, false, "trace debugging JSON request/response");
DECLARE_bool(allow_natives_syntax);


static void PrintLn(v8::Local<v8::Value> value) {
  v8::Local<v8::String> s = value->ToString();
  char* data = NewArray<char>(s->Length() + 1);
  if (data == NULL) {
    V8::FatalProcessOutOfMemory("PrintLn");
    return;
  }
  s->WriteAscii(data);
  PrintF("%s\n", data);
  DeleteArray(data);
}


PendingRequest::PendingRequest(const uint16_t* json_request, int length)
    : json_request_(Vector<uint16_t>::empty()),
      next_(NULL) {
  // Copy the request.
  json_request_ =
      Vector<uint16_t>(const_cast<uint16_t *>(json_request), length).Clone();
}


PendingRequest::~PendingRequest() {
  // Deallocate what was allocated.
  if (!json_request_.is_empty()) {
    json_request_.Dispose();
  }
}

Handle<String> PendingRequest::request() {
  // Create a string in the heap from the pending request.
  if (!json_request_.is_empty()) {
    return Factory::NewStringFromTwoByte(
        Vector<const uint16_t>(
            reinterpret_cast<const uint16_t*>(json_request_.start()),
            json_request_.length()));
  } else {
    return Handle<String>();
  }
}


static Handle<Code> ComputeCallDebugBreak(int argc) {
  CALL_HEAP_FUNCTION(StubCache::ComputeCallDebugBreak(argc), Code);
}


static Handle<Code> ComputeCallDebugPrepareStepIn(int argc) {
  CALL_HEAP_FUNCTION(StubCache::ComputeCallDebugPrepareStepIn(argc), Code);
}


BreakLocationIterator::BreakLocationIterator(Handle<DebugInfo> debug_info,
                                             BreakLocatorType type) {
  debug_info_ = debug_info;
  type_ = type;
  reloc_iterator_ = NULL;
  reloc_iterator_original_ = NULL;
  Reset();  // Initialize the rest of the member variables.
}


BreakLocationIterator::~BreakLocationIterator() {
  ASSERT(reloc_iterator_ != NULL);
  ASSERT(reloc_iterator_original_ != NULL);
  delete reloc_iterator_;
  delete reloc_iterator_original_;
}


void BreakLocationIterator::Next() {
  AssertNoAllocation nogc;
  ASSERT(!RinfoDone());

  // Iterate through reloc info for code and original code stopping at each
  // breakable code target.
  bool first = break_point_ == -1;
  while (!RinfoDone()) {
    if (!first) RinfoNext();
    first = false;
    if (RinfoDone()) return;

    // Update the current source position each time a source position is
    // passed.
    if (is_position(rmode())) {
      position_ = rinfo()->data() - debug_info_->shared()->start_position();
      if (is_statement_position(rmode())) {
        statement_position_ =
            rinfo()->data() - debug_info_->shared()->start_position();
      }
      ASSERT(position_ >= 0);
      ASSERT(statement_position_ >= 0);
    }

    // Check for breakable code target. Look in the original code as setting
    // break points can cause the code targets in the running (debugged) code to
    // be of a different kind than in the original code.
    if (is_code_target(rmode())) {
      Address target = original_rinfo()->target_address();
      Code* code = Debug::GetCodeTarget(target);
      if (code->is_inline_cache_stub() || is_js_construct_call(rmode())) {
        break_point_++;
        return;
      }
      if (code->kind() == Code::STUB) {
        if (type_ == ALL_BREAK_LOCATIONS) {
          if (Debug::IsBreakStub(code)) {
            break_point_++;
            return;
          }
        } else {
          ASSERT(type_ == SOURCE_BREAK_LOCATIONS);
          if (Debug::IsSourceBreakStub(code)) {
            break_point_++;
            return;
          }
        }
      }
    }

    // Check for break at return.
    // Currently is_exit_js_frame is used on ARM.
    if (is_js_return(rmode()) || is_exit_js_frame(rmode())) {
      // Set the positions to the end of the function.
      if (debug_info_->shared()->HasSourceCode()) {
        position_ = debug_info_->shared()->end_position() -
                    debug_info_->shared()->start_position();
      } else {
        position_ = 0;
      }
      statement_position_ = position_;
      break_point_++;
      return;
    }
  }
}


void BreakLocationIterator::Next(int count) {
  while (count > 0) {
    Next();
    count--;
  }
}


// Find the break point closest to the supplied address.
void BreakLocationIterator::FindBreakLocationFromAddress(Address pc) {
  // Run through all break points to locate the one closest to the address.
  int closest_break_point = 0;
  int distance = kMaxInt;
  while (!Done()) {
    // Check if this break point is closer that what was previously found.
    if (this->pc() < pc && pc - this->pc() < distance) {
      closest_break_point = break_point();
      distance = pc - this->pc();
      // Check whether we can't get any closer.
      if (distance == 0) break;
    }
    Next();
  }

  // Move to the break point found.
  Reset();
  Next(closest_break_point);
}


// Find the break point closest to the supplied source position.
void BreakLocationIterator::FindBreakLocationFromPosition(int position) {
  // Run through all break points to locate the one closest to the source
  // position.
  int closest_break_point = 0;
  int distance = kMaxInt;
  while (!Done()) {
    // Check if this break point is closer that what was previously found.
    if (position <= statement_position() &&
        statement_position() - position < distance) {
      closest_break_point = break_point();
      distance = statement_position() - position;
      // Check whether we can't get any closer.
      if (distance == 0) break;
    }
    Next();
  }

  // Move to the break point found.
  Reset();
  Next(closest_break_point);
}


void BreakLocationIterator::Reset() {
  // Create relocation iterators for the two code objects.
  if (reloc_iterator_ != NULL) delete reloc_iterator_;
  if (reloc_iterator_original_ != NULL) delete reloc_iterator_original_;
  reloc_iterator_ = new RelocIterator(debug_info_->code());
  reloc_iterator_original_ = new RelocIterator(debug_info_->original_code());

  // Position at the first break point.
  break_point_ = -1;
  position_ = 1;
  statement_position_ = 1;
  Next();
}


bool BreakLocationIterator::Done() const {
  return RinfoDone();
}


void BreakLocationIterator::SetBreakPoint(Handle<Object> break_point_object) {
  // If there is not already a real break point here patch code with debug
  // break.
  if (!HasBreakPoint()) {
    SetDebugBreak();
  }
  ASSERT(IsDebugBreak());
  // Set the break point information.
  DebugInfo::SetBreakPoint(debug_info_, code_position(),
                           position(), statement_position(),
                           break_point_object);
}


void BreakLocationIterator::ClearBreakPoint(Handle<Object> break_point_object) {
  // Clear the break point information.
  DebugInfo::ClearBreakPoint(debug_info_, code_position(), break_point_object);
  // If there are no more break points here remove the debug break.
  if (!HasBreakPoint()) {
    ClearDebugBreak();
    ASSERT(!IsDebugBreak());
  }
}


void BreakLocationIterator::SetOneShot() {
  // If there is a real break point here no more to do.
  if (HasBreakPoint()) {
    ASSERT(IsDebugBreak());
    return;
  }

  // Patch code with debug break.
  SetDebugBreak();
}


void BreakLocationIterator::ClearOneShot() {
  // If there is a real break point here no more to do.
  if (HasBreakPoint()) {
    ASSERT(IsDebugBreak());
    return;
  }

  // Patch code removing debug break.
  ClearDebugBreak();
  ASSERT(!IsDebugBreak());
}


void BreakLocationIterator::SetDebugBreak() {
  // If there is already a break point here just return. This might happen if
  // the same code is flodded with break points twice. Flodding the same
  // function twice might happen when stepping in a function with an exception
  // handler as the handler and the function is the same.
  if (IsDebugBreak()) {
    return;
  }

  if (is_js_return(rmode())) {
    // This path is currently only used on IA32 as JSExitFrame on ARM uses a
    // stub.
    // Patch the JS frame exit code with a debug break call. See
    // VisitReturnStatement and ExitJSFrame in codegen-ia32.cc for the
    // precise return instructions sequence.
    ASSERT(Debug::kIa32JSReturnSequenceLength >=
           Debug::kIa32CallInstructionLength);
    rinfo()->patch_code_with_call(Debug::debug_break_return_entry()->entry(),
        Debug::kIa32JSReturnSequenceLength - Debug::kIa32CallInstructionLength);
  } else {
    // Patch the original code with the current address as the current address
    // might have changed by the inline caching since the code was copied.
    original_rinfo()->set_target_address(rinfo()->target_address());

    // Patch the code to invoke the builtin debug break function matching the
    // calling convention used by the call site.
    Handle<Code> dbgbrk_code(Debug::FindDebugBreak(rinfo()));
    rinfo()->set_target_address(dbgbrk_code->entry());
  }
  ASSERT(IsDebugBreak());
}


void BreakLocationIterator::ClearDebugBreak() {
  if (is_js_return(rmode())) {
    // Restore the JS frame exit code.
    rinfo()->patch_code(original_rinfo()->pc(),
                        Debug::kIa32JSReturnSequenceLength);
  } else {
    // Patch the code to the original invoke.
    rinfo()->set_target_address(original_rinfo()->target_address());
  }
  ASSERT(!IsDebugBreak());
}


void BreakLocationIterator::PrepareStepIn() {
  // Step in can only be prepared if currently positioned on an IC call or
  // construct call.
  Address target = rinfo()->target_address();
  Code* code = Debug::GetCodeTarget(target);
  if (code->is_call_stub()) {
    // Step in through IC call is handled by the runtime system. Therefore make
    // sure that the any current IC is cleared and the runtime system is
    // called. If the executing code has a debug break at the location change
    // the call in the original code as it is the code there that will be
    // executed in place of the debug break call.
    Handle<Code> stub = ComputeCallDebugPrepareStepIn(code->arguments_count());
    if (IsDebugBreak()) {
      original_rinfo()->set_target_address(stub->entry());
    } else {
      rinfo()->set_target_address(stub->entry());
    }
  } else {
    // Step in through constructs call requires no changs to the running code.
    ASSERT(is_js_construct_call(rmode()));
  }
}


// Check whether the break point is at a position which will exit the function.
bool BreakLocationIterator::IsExit() const {
  // Currently is_exit_js_frame is used on ARM.
  return (is_js_return(rmode()) || is_exit_js_frame(rmode()));
}


bool BreakLocationIterator::HasBreakPoint() {
  return debug_info_->HasBreakPoint(code_position());
}


// Check whether there is a debug break at the current position.
bool BreakLocationIterator::IsDebugBreak() {
  if (is_js_return(rmode())) {
    // This is IA32 specific but works as long as the ARM version
    // still uses a stub for JSExitFrame.
    //
    // TODO(1240753): Make the test architecture independent or split
    // parts of the debugger into architecture dependent files.
    return (*(rinfo()->pc()) == 0xE8);
  } else {
    return Debug::IsDebugBreak(rinfo()->target_address());
  }
}


Object* BreakLocationIterator::BreakPointObjects() {
  return debug_info_->GetBreakPointObjects(code_position());
}


bool BreakLocationIterator::RinfoDone() const {
  ASSERT(reloc_iterator_->done() == reloc_iterator_original_->done());
  return reloc_iterator_->done();
}


void BreakLocationIterator::RinfoNext() {
  reloc_iterator_->next();
  reloc_iterator_original_->next();
#ifdef DEBUG
  ASSERT(reloc_iterator_->done() == reloc_iterator_original_->done());
  if (!reloc_iterator_->done()) {
    ASSERT(rmode() == original_rmode());
  }
#endif
}


bool Debug::has_break_points_ = false;
DebugInfoListNode* Debug::debug_info_list_ = NULL;


// Threading support.
void Debug::ThreadInit() {
  thread_local_.last_step_action_ = StepNone;
  thread_local_.last_statement_position_ = kNoPosition;
  thread_local_.step_count_ = 0;
  thread_local_.last_fp_ = 0;
  thread_local_.step_into_fp_ = 0;
  thread_local_.after_break_target_ = 0;
}


JSCallerSavedBuffer Debug::registers_;
Debug::ThreadLocal Debug::thread_local_;


char* Debug::ArchiveDebug(char* storage) {
  char* to = storage;
  memcpy(to, reinterpret_cast<char*>(&thread_local_), sizeof(ThreadLocal));
  to += sizeof(ThreadLocal);
  memcpy(to, reinterpret_cast<char*>(&registers_), sizeof(registers_));
  ThreadInit();
  ASSERT(to <= storage + ArchiveSpacePerThread());
  return storage + ArchiveSpacePerThread();
}


char* Debug::RestoreDebug(char* storage) {
  char* from = storage;
  memcpy(reinterpret_cast<char*>(&thread_local_), from, sizeof(ThreadLocal));
  from += sizeof(ThreadLocal);
  memcpy(reinterpret_cast<char*>(&registers_), from, sizeof(registers_));
  ASSERT(from <= storage + ArchiveSpacePerThread());
  return storage + ArchiveSpacePerThread();
}


int Debug::ArchiveSpacePerThread() {
  return sizeof(ThreadLocal) + sizeof(registers_);
}


// Default break enabled.
bool Debug::disable_break_ = false;

// Default call debugger on uncaught exception.
bool Debug::break_on_exception_ = false;
bool Debug::break_on_uncaught_exception_ = true;

Handle<Context> Debug::debug_context_ = Handle<Context>();
Code* Debug::debug_break_return_entry_ = NULL;
Code* Debug::debug_break_return_ = NULL;


void Debug::HandleWeakDebugInfo(v8::Persistent<v8::Object> obj, void* data) {
  DebugInfoListNode* node = reinterpret_cast<DebugInfoListNode*>(data);
  RemoveDebugInfo(node->debug_info());
#ifdef DEBUG
  node = Debug::debug_info_list_;
  while (node != NULL) {
    ASSERT(node != reinterpret_cast<DebugInfoListNode*>(data));
    node = node->next();
  }
#endif
}


DebugInfoListNode::DebugInfoListNode(DebugInfo* debug_info): next_(NULL) {
  // Globalize the request debug info object and make it weak.
  debug_info_ = Handle<DebugInfo>::cast((GlobalHandles::Create(debug_info)));
  GlobalHandles::MakeWeak(reinterpret_cast<Object**>(debug_info_.location()),
                          this, Debug::HandleWeakDebugInfo);
}


DebugInfoListNode::~DebugInfoListNode() {
  GlobalHandles::Destroy(reinterpret_cast<Object**>(debug_info_.location()));
}


void Debug::Setup(bool create_heap_objects) {
  ThreadInit();
  if (create_heap_objects) {
    // Get code to handle entry to debug break on return.
    debug_break_return_entry_ =
        Builtins::builtin(Builtins::Return_DebugBreakEntry);
    ASSERT(debug_break_return_entry_->IsCode());

    // Get code to handle debug break on return.
    debug_break_return_ =
        Builtins::builtin(Builtins::Return_DebugBreak);
    ASSERT(debug_break_return_->IsCode());
  }
}


bool Debug::CompileDebuggerScript(int index) {
  HandleScope scope;

  // Bail out if the index is invalid.
  if (index == -1) {
    return false;
  }

  // Find source and name for the requested script.
  Handle<String> source_code = Bootstrapper::NativesSourceLookup(index);
  Vector<const char> name = Natives::GetScriptName(index);
  Handle<String> script_name = Factory::NewStringFromAscii(name);

  // Compile the script.
  bool allow_natives_syntax = FLAG_allow_natives_syntax;
  FLAG_allow_natives_syntax = true;
  Handle<JSFunction> boilerplate;
  boilerplate = Compiler::Compile(source_code, script_name, 0, 0, NULL, NULL);
  FLAG_allow_natives_syntax = allow_natives_syntax;

  // Silently ignore stack overflows during compilation.
  if (boilerplate.is_null()) {
    ASSERT(Top::has_pending_exception());
    Top::clear_pending_exception();
    return false;
  }

  // Execute the boilerplate function in the debugger context.
  Handle<Context> context = Top::global_context();
  bool caught_exception = false;
  Handle<JSFunction> function =
      Factory::NewFunctionFromBoilerplate(boilerplate, context);
  Handle<Object> result =
      Execution::TryCall(function, Handle<Object>(context->global()),
                         0, NULL, &caught_exception);

  // Check for caught exceptions.
  if (caught_exception) {
    MessageHandler::ReportMessage("error_loading_debugger", NULL,
                                  HandleVector<Object>(&result, 1));
    return false;
  }

  // Mark this script as native and return successfully.
  Handle<Script> script(Script::cast(function->shared()->script()));
  script->set_type(Smi::FromInt(SCRIPT_TYPE_NATIVE));
  return true;
}


bool Debug::Load() {
  // Return if debugger is already loaded.
  if (IsLoaded()) return true;

  // Bail out if we're already in the process of compiling the native
  // JavaScript source code for the debugger.
  if (Debugger::compiling_natives()) return false;

  // Disable breakpoints and interrupts while compiling and running the
  // debugger scripts including the context creation code.
  DisableBreak disable(true);
  PostponeInterruptsScope postpone;

  // Create the debugger context.
  HandleScope scope;
  Handle<Context> context =
      Bootstrapper::CreateEnvironment(Handle<Object>::null(),
                                      v8::Handle<ObjectTemplate>(),
                                      NULL);

  // Use the debugger context.
  SaveContext save;
  Top::set_context(*context);
  Top::set_security_context(*context);

  // Expose the builtins object in the debugger context.
  Handle<String> key = Factory::LookupAsciiSymbol("builtins");
  Handle<GlobalObject> global = Handle<GlobalObject>(context->global());
  SetProperty(global, key, Handle<Object>(global->builtins()), NONE);

  // Compile the JavaScript for the debugger in the debugger context.
  Debugger::set_compiling_natives(true);
  bool caught_exception =
      !CompileDebuggerScript(Natives::GetIndex("mirror")) ||
      !CompileDebuggerScript(Natives::GetIndex("debug"));
  Debugger::set_compiling_natives(false);

  // Check for caught exceptions.
  if (caught_exception) return false;

  // Debugger loaded.
  debug_context_ = Handle<Context>::cast(GlobalHandles::Create(*context));
  return true;
}


void Debug::Unload() {
  // Return debugger is not loaded.
  if (!IsLoaded()) {
    return;
  }

  // Clear debugger context global handle.
  GlobalHandles::Destroy(reinterpret_cast<Object**>(debug_context_.location()));
  debug_context_ = Handle<Context>();
}


void Debug::Iterate(ObjectVisitor* v) {
#define VISIT(field) v->VisitPointer(reinterpret_cast<Object**>(&(field)));
  VISIT(debug_break_return_entry_);
  VISIT(debug_break_return_);
#undef VISIT
}


Object* Debug::Break(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  // Get the top-most JavaScript frame.
  JavaScriptFrameIterator it;
  JavaScriptFrame* frame = it.frame();

  // Just continue if breaks are disabled or debugger cannot be loaded.
  if (disable_break() || !Load()) {
    SetAfterBreakTarget(frame);
    return args[0];
  }

  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // Postpone interrupt during breakpoint processing.
  PostponeInterruptsScope postpone;

  // Get the debug info (create it if it does not exist).
  Handle<SharedFunctionInfo> shared =
      Handle<SharedFunctionInfo>(JSFunction::cast(frame->function())->shared());
  Handle<DebugInfo> debug_info = GetDebugInfo(shared);

  // Find the break point where execution has stopped.
  BreakLocationIterator break_location_iterator(debug_info,
                                                ALL_BREAK_LOCATIONS);
  break_location_iterator.FindBreakLocationFromAddress(frame->pc());

  // Check whether step next reached a new statement.
  if (!StepNextContinue(&break_location_iterator, frame)) {
    // Decrease steps left if performing multiple steps.
    if (thread_local_.step_count_ > 0) {
      thread_local_.step_count_--;
    }
  }

  // If there is one or more real break points check whether any of these are
  // triggered.
  Handle<Object> break_points_hit(Heap::undefined_value());
  if (break_location_iterator.HasBreakPoint()) {
    Handle<Object> break_point_objects =
        Handle<Object>(break_location_iterator.BreakPointObjects());
    break_points_hit = CheckBreakPoints(break_point_objects);
  }

  // Notify debugger if a real break point is triggered or if performing single
  // stepping with no more steps to perform. Otherwise do another step.
  if (!break_points_hit->IsUndefined() ||
    (thread_local_.last_step_action_ != StepNone &&
     thread_local_.step_count_ == 0)) {
    // Clear all current stepping setup.
    ClearStepping();

    // Notify the debug event listeners.
    Debugger::OnDebugBreak(break_points_hit);
  } else if (thread_local_.last_step_action_ != StepNone) {
    // Hold on to last step action as it is cleared by the call to
    // ClearStepping.
    StepAction step_action = thread_local_.last_step_action_;
    int step_count = thread_local_.step_count_;

    // Clear all current stepping setup.
    ClearStepping();

    // Set up for the remaining steps.
    PrepareStep(step_action, step_count);
  }

  // Install jump to the call address which was overwritten.
  SetAfterBreakTarget(frame);

  return args[0];
}


// Check the break point objects for whether one or more are actually
// triggered. This function returns a JSArray with the break point objects
// which is triggered.
Handle<Object> Debug::CheckBreakPoints(Handle<Object> break_point_objects) {
  int break_points_hit_count = 0;
  Handle<JSArray> break_points_hit = Factory::NewJSArray(1);

  // If there are multiple break points they are in a Fixedrray.
  ASSERT(!break_point_objects->IsUndefined());
  if (break_point_objects->IsFixedArray()) {
    Handle<FixedArray> array(FixedArray::cast(*break_point_objects));
    for (int i = 0; i < array->length(); i++) {
      Handle<Object> o(array->get(i));
      if (CheckBreakPoint(o)) {
        break_points_hit->SetElement(break_points_hit_count++, *o);
      }
    }
  } else {
    if (CheckBreakPoint(break_point_objects)) {
      break_points_hit->SetElement(break_points_hit_count++,
                                   *break_point_objects);
    }
  }

  // Return undefined if no break points where triggered.
  if (break_points_hit_count == 0) {
    return Factory::undefined_value();
  }
  return break_points_hit;
}


// Check whether a single break point object is triggered.
bool Debug::CheckBreakPoint(Handle<Object> break_point_object) {
  // Ignore check if break point object is not a JSObject.
  if (!break_point_object->IsJSObject()) return true;

  // Get the function CheckBreakPoint (defined in debug.js).
  Handle<JSFunction> check_break_point =
    Handle<JSFunction>(JSFunction::cast(
      debug_context()->global()->GetProperty(
          *Factory::LookupAsciiSymbol("IsBreakPointTriggered"))));

  // Get the break id as an object.
  Handle<Object> break_id = Factory::NewNumberFromInt(Top::break_id());

  // Call HandleBreakPointx.
  bool caught_exception = false;
  const int argc = 2;
  Object** argv[argc] = {
    break_id.location(),
    reinterpret_cast<Object**>(break_point_object.location())
  };
  Handle<Object> result = Execution::TryCall(check_break_point,
                                             Top::builtins(), argc, argv,
                                             &caught_exception);

  // If exception or non boolean result handle as not triggered
  if (caught_exception || !result->IsBoolean()) {
    return false;
  }

  // Return whether the break point is triggered.
  return *result == Heap::true_value();
}


// Check whether the function has debug information.
bool Debug::HasDebugInfo(Handle<SharedFunctionInfo> shared) {
  return !shared->debug_info()->IsUndefined();
}


// Return the debug info for this function. EnsureDebugInfo must be called
// prior to ensure the debug info has been generated for shared.
Handle<DebugInfo> Debug::GetDebugInfo(Handle<SharedFunctionInfo> shared) {
  ASSERT(HasDebugInfo(shared));
  return Handle<DebugInfo>(DebugInfo::cast(shared->debug_info()));
}


void Debug::SetBreakPoint(Handle<SharedFunctionInfo> shared,
                          int source_position,
                          Handle<Object> break_point_object) {
  if (!EnsureDebugInfo(shared)) {
    // Return if retrieving debug info failed.
    return;
  }

  Handle<DebugInfo> debug_info = GetDebugInfo(shared);
  // Source positions starts with zero.
  ASSERT(source_position >= 0);

  // Find the break point and change it.
  BreakLocationIterator it(debug_info, SOURCE_BREAK_LOCATIONS);
  it.FindBreakLocationFromPosition(source_position);
  it.SetBreakPoint(break_point_object);

  // At least one active break point now.
  ASSERT(debug_info->GetBreakPointCount() > 0);
}


void Debug::ClearBreakPoint(Handle<Object> break_point_object) {
  DebugInfoListNode* node = debug_info_list_;
  while (node != NULL) {
    Object* result = DebugInfo::FindBreakPointInfo(node->debug_info(),
                                                   break_point_object);
    if (!result->IsUndefined()) {
      // Get information in the break point.
      BreakPointInfo* break_point_info = BreakPointInfo::cast(result);
      Handle<DebugInfo> debug_info = node->debug_info();
      Handle<SharedFunctionInfo> shared(debug_info->shared());
      int source_position =  break_point_info->statement_position()->value();

      // Source positions starts with zero.
      ASSERT(source_position >= 0);

      // Find the break point and clear it.
      BreakLocationIterator it(debug_info, SOURCE_BREAK_LOCATIONS);
      it.FindBreakLocationFromPosition(source_position);
      it.ClearBreakPoint(break_point_object);

      // If there are no more break points left remove the debug info for this
      // function.
      if (debug_info->GetBreakPointCount() == 0) {
        RemoveDebugInfo(debug_info);
      }

      return;
    }
    node = node->next();
  }
}


void Debug::FloodWithOneShot(Handle<SharedFunctionInfo> shared) {
  // Make sure the function has setup the debug info.
  if (!EnsureDebugInfo(shared)) {
    // Return if we failed to retrieve the debug info.
    return;
  }

  // Flood the function with break points.
  BreakLocationIterator it(GetDebugInfo(shared), ALL_BREAK_LOCATIONS);
  while (!it.Done()) {
    it.SetOneShot();
    it.Next();
  }
}


void Debug::FloodHandlerWithOneShot() {
  StackFrame::Id id = Top::break_frame_id();
  for (JavaScriptFrameIterator it(id); !it.done(); it.Advance()) {
    JavaScriptFrame* frame = it.frame();
    if (frame->HasHandler()) {
      Handle<SharedFunctionInfo> shared =
          Handle<SharedFunctionInfo>(
              JSFunction::cast(frame->function())->shared());
      // Flood the function with the catch block with break points
      FloodWithOneShot(shared);
      return;
    }
  }
}


void Debug::ChangeBreakOnException(ExceptionBreakType type, bool enable) {
  if (type == BreakUncaughtException) {
    break_on_uncaught_exception_ = enable;
  } else {
    break_on_exception_ = enable;
  }
}


void Debug::PrepareStep(StepAction step_action, int step_count) {
  HandleScope scope;
  ASSERT(Debug::InDebugger());

  // Remember this step action and count.
  thread_local_.last_step_action_ = step_action;
  thread_local_.step_count_ = step_count;

  // Get the frame where the execution has stopped and skip the debug frame if
  // any. The debug frame will only be present if execution was stopped due to
  // hitting a break point. In other situations (e.g. unhandled exception) the
  // debug frame is not present.
  StackFrame::Id id = Top::break_frame_id();
  JavaScriptFrameIterator frames_it(id);
  JavaScriptFrame* frame = frames_it.frame();

  // First of all ensure there is one-shot break points in the top handler
  // if any.
  FloodHandlerWithOneShot();

  // If the function on the top frame is unresolved perform step out. This will
  // be the case when calling unknown functions and having the debugger stopped
  // in an unhandled exception.
  if (!frame->function()->IsJSFunction()) {
    // Step out: Find the calling JavaScript frame and flood it with
    // breakpoints.
    frames_it.Advance();
    // Fill the function to return to with one-shot break points.
    JSFunction* function = JSFunction::cast(frames_it.frame()->function());
    FloodWithOneShot(Handle<SharedFunctionInfo>(function->shared()));
    return;
  }

  // Get the debug info (create it if it does not exist).
  Handle<SharedFunctionInfo> shared =
      Handle<SharedFunctionInfo>(JSFunction::cast(frame->function())->shared());
  if (!EnsureDebugInfo(shared)) {
    // Return if ensuring debug info failed.
    return;
  }
  Handle<DebugInfo> debug_info = GetDebugInfo(shared);

  // Find the break location where execution has stopped.
  BreakLocationIterator it(debug_info, ALL_BREAK_LOCATIONS);
  it.FindBreakLocationFromAddress(frame->pc());

  // Compute whether or not the target is a call target.
  bool is_call_target = false;
  if (is_code_target(it.rinfo()->rmode())) {
    Address target = it.rinfo()->target_address();
    Code* code = Debug::GetCodeTarget(target);
    if (code->is_call_stub()) is_call_target = true;
  }

  // If this is the last break code target step out is the only posibility.
  if (it.IsExit() || step_action == StepOut) {
    // Step out: If there is a JavaScript caller frame, we need to
    // flood it with breakpoints.
    frames_it.Advance();
    if (!frames_it.done()) {
      // Fill the function to return to with one-shot break points.
      JSFunction* function = JSFunction::cast(frames_it.frame()->function());
      FloodWithOneShot(Handle<SharedFunctionInfo>(function->shared()));
    }
  } else if (!(is_call_target || is_js_construct_call(it.rmode())) ||
             step_action == StepNext || step_action == StepMin) {
    // Step next or step min.

    // Fill the current function with one-shot break points.
    FloodWithOneShot(shared);

    // Remember source position and frame to handle step next.
    thread_local_.last_statement_position_ =
        debug_info->code()->SourceStatementPosition(frame->pc());
    thread_local_.last_fp_ = frame->fp();
  } else {
    // Fill the current function with one-shot break points even for step in on
    // a call target as the function called might be a native function for
    // which step in will not stop.
    FloodWithOneShot(shared);

    // Step in or Step in min
    it.PrepareStepIn();
    ActivateStepIn(frame);
  }
}


// Check whether the current debug break should be reported to the debugger. It
// is used to have step next and step in only report break back to the debugger
// if on a different frame or in a different statement. In some situations
// there will be several break points in the same statement when the code is
// flodded with one-shot break points. This function helps to perform several
// steps before reporting break back to the debugger.
bool Debug::StepNextContinue(BreakLocationIterator* break_location_iterator,
                             JavaScriptFrame* frame) {
  // If the step last action was step next or step in make sure that a new
  // statement is hit.
  if (thread_local_.last_step_action_ == StepNext ||
      thread_local_.last_step_action_ == StepIn) {
    // Never continue if returning from function.
    if (break_location_iterator->IsExit()) return false;

    // Continue if we are still on the same frame and in the same statement.
    int current_statement_position =
        break_location_iterator->code()->SourceStatementPosition(frame->pc());
    return thread_local_.last_fp_ == frame->fp() &&
           thread_local_.last_statement_position_ == current_statement_position;
  }

  // No step next action - don't continue.
  return false;
}


// Check whether the code object at the specified address is a debug break code
// object.
bool Debug::IsDebugBreak(Address addr) {
  Code* code = GetCodeTarget(addr);
  return code->state() == DEBUG_BREAK;
}


// Check whether a code stub with the specified major key is a possible break
// point location when looking for source break locations.
bool Debug::IsSourceBreakStub(Code* code) {
  CodeStub::Major major_key = code->major_key();
  return major_key == CodeStub::CallFunction;
}


// Check whether a code stub with the specified major key is a possible break
// location.
bool Debug::IsBreakStub(Code* code) {
  CodeStub::Major major_key = code->major_key();
  return major_key == CodeStub::CallFunction ||
         major_key == CodeStub::StackCheck;
}


// Find the builtin to use for invoking the debug break
Handle<Code> Debug::FindDebugBreak(RelocInfo* rinfo) {
  // Find the builtin debug break function matching the calling convention
  // used by the call site.
  RelocMode mode = rinfo->rmode();

  if (is_code_target(mode)) {
    Address target = rinfo->target_address();
    Code* code = Debug::GetCodeTarget(target);
    if (code->is_inline_cache_stub()) {
      if (code->is_call_stub()) {
        return ComputeCallDebugBreak(code->arguments_count());
      }
      if (code->is_load_stub()) {
        return Handle<Code>(Builtins::builtin(Builtins::LoadIC_DebugBreak));
      }
      if (code->is_store_stub()) {
        return Handle<Code>(Builtins::builtin(Builtins::StoreIC_DebugBreak));
      }
      if (code->is_keyed_load_stub()) {
        Handle<Code> result =
            Handle<Code>(Builtins::builtin(Builtins::KeyedLoadIC_DebugBreak));
        return result;
      }
      if (code->is_keyed_store_stub()) {
        Handle<Code> result =
            Handle<Code>(Builtins::builtin(Builtins::KeyedStoreIC_DebugBreak));
        return result;
      }
    }
    if (is_js_construct_call(mode)) {
      Handle<Code> result =
          Handle<Code>(Builtins::builtin(Builtins::ConstructCall_DebugBreak));
      return result;
    }
    // Currently is_exit_js_frame is used on ARM.
    if (is_exit_js_frame(mode)) {
      return Handle<Code>(Builtins::builtin(Builtins::Return_DebugBreak));
    }
    if (code->kind() == Code::STUB) {
      ASSERT(code->major_key() == CodeStub::CallFunction ||
             code->major_key() == CodeStub::StackCheck);
      Handle<Code> result =
          Handle<Code>(Builtins::builtin(Builtins::StubNoRegisters_DebugBreak));
      return result;
    }
  }

  UNREACHABLE();
  return Handle<Code>::null();
}


// Simple function for returning the source positions for active break points.
Handle<Object> Debug::GetSourceBreakLocations(
    Handle<SharedFunctionInfo> shared) {
  if (!HasDebugInfo(shared)) return Handle<Object>(Heap::undefined_value());
  Handle<DebugInfo> debug_info = GetDebugInfo(shared);
  if (debug_info->GetBreakPointCount() == 0) {
    return Handle<Object>(Heap::undefined_value());
  }
  Handle<FixedArray> locations =
      Factory::NewFixedArray(debug_info->GetBreakPointCount());
  int count = 0;
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (!debug_info->break_points()->get(i)->IsUndefined()) {
      BreakPointInfo* break_point_info =
          BreakPointInfo::cast(debug_info->break_points()->get(i));
      if (break_point_info->GetBreakPointCount() > 0) {
        locations->set(count++, break_point_info->statement_position());
      }
    }
  }
  return locations;
}


void Debug::ClearStepping() {
  // Clear the various stepping setup.
  ClearOneShot();
  ClearStepIn();
  ClearStepNext();

  // Clear multiple step counter.
  thread_local_.step_count_ = 0;
}

// Clears all the one-shot break points that are currently set. Normally this
// function is called each time a break point is hit as one shot break points
// are used to support stepping.
void Debug::ClearOneShot() {
  // The current implementation just runs through all the breakpoints. When the
  // last break point for a function is removed that function is automaticaly
  // removed from the list.

  DebugInfoListNode* node = debug_info_list_;
  while (node != NULL) {
    BreakLocationIterator it(node->debug_info(), ALL_BREAK_LOCATIONS);
    while (!it.Done()) {
      it.ClearOneShot();
      it.Next();
    }
    node = node->next();
  }
}


void Debug::ActivateStepIn(StackFrame* frame) {
  thread_local_.step_into_fp_ = frame->fp();
}


void Debug::ClearStepIn() {
  thread_local_.step_into_fp_ = 0;
}


void Debug::ClearStepNext() {
  thread_local_.last_step_action_ = StepNone;
  thread_local_.last_statement_position_ = kNoPosition;
  thread_local_.last_fp_ = 0;
}


bool Debug::EnsureCompiled(Handle<SharedFunctionInfo> shared) {
  if (shared->is_compiled()) return true;
  return CompileLazyShared(shared, CLEAR_EXCEPTION);
}


// Ensures the debug information is present for shared.
bool Debug::EnsureDebugInfo(Handle<SharedFunctionInfo> shared) {
  // Return if we already have the debug info for shared.
  if (HasDebugInfo(shared)) return true;

  // Ensure shared in compiled. Return false if this failed.
  if (!EnsureCompiled(shared)) return false;

  // Create the debug info object.
  Handle<DebugInfo> debug_info =
      Handle<DebugInfo>::cast(Factory::NewStruct(DEBUG_INFO_TYPE));

  // Get the function original code.
  Handle<Code> code(shared->code());

  // Debug info contains function, a copy of the original code and the executing
  // code.
  debug_info->set_shared(*shared);
  debug_info->set_original_code(*Factory::CopyCode(code));
  debug_info->set_code(*code);

  // Link debug info to function.
  shared->set_debug_info(*debug_info);

  // Initially no active break points.
  debug_info->set_break_points(
      *Factory::NewFixedArray(Debug::kEstimatedNofBreakPointsInFunction));

  // Add debug info to the list.
  DebugInfoListNode* node = new DebugInfoListNode(*debug_info);
  node->set_next(debug_info_list_);
  debug_info_list_ = node;

  // Now there is at least one break point.
  has_break_points_ = true;

  return true;
}


void Debug::RemoveDebugInfo(Handle<DebugInfo> debug_info) {
  ASSERT(debug_info_list_ != NULL);
  // Run through the debug info objects to find this one and remove it.
  DebugInfoListNode* prev = NULL;
  DebugInfoListNode* current = debug_info_list_;
  while (current != NULL) {
    if (*current->debug_info() == *debug_info) {
      // Unlink from list. If prev is NULL we are looking at the first element.
      if (prev == NULL) {
        debug_info_list_ = current->next();
      } else {
        prev->set_next(current->next());
      }
      current->debug_info()->shared()->set_debug_info(Heap::undefined_value());
      delete current;

      // If there are no more debug info objects there are not more break
      // points.
      has_break_points_ = debug_info_list_ != NULL;

      return;
    }
    // Move to next in list.
    prev = current;
    current = current->next();
  }
  UNREACHABLE();
}


void Debug::SetAfterBreakTarget(JavaScriptFrame* frame) {
  // Get the executing function in which the debug break occurred.
  Handle<SharedFunctionInfo> shared =
      Handle<SharedFunctionInfo>(JSFunction::cast(frame->function())->shared());
  if (!EnsureDebugInfo(shared)) {
    // Return if we failed to retrieve the debug info.
    return;
  }
  Handle<DebugInfo> debug_info = GetDebugInfo(shared);
  Handle<Code> code(debug_info->code());
  Handle<Code> original_code(debug_info->original_code());
#ifdef DEBUG
  // Get the code which is actually executing.
  Handle<Code> frame_code(frame->FindCode());
  ASSERT(frame_code.is_identical_to(code));
#endif

  // Find the call address in the running code. This address holds the call to
  // either a DebugBreakXXX or to the debug break return entry code if the
  // break point is still active after processing the break point.
  Address addr = frame->pc() - Assembler::kTargetAddrToReturnAddrDist;

  // Check if the location is at JS exit.
  bool at_js_exit = false;
  RelocIterator it(debug_info->code());
  while (!it.done()) {
    if (is_js_return(it.rinfo()->rmode())) {
      at_js_exit = it.rinfo()->pc() == addr - 1;
    }
    it.next();
  }

  // Handle the jump to continue execution after break point depending on the
  // break location.
  if (at_js_exit) {
    // First check if the call in the code is still the debug break return
    // entry code. If it is the break point is still active. If not the break
    // point was removed during break point processing.
    if (Assembler::target_address_at(addr) ==
        debug_break_return_entry()->entry()) {
      // Break point still active. Jump to the corresponding place in the
      // original code.
      addr +=  original_code->instruction_start() - code->instruction_start();
    }

    // Move one byte back to where the call instruction was placed.
    thread_local_.after_break_target_ = addr - 1;
  } else {
    // Check if there still is a debug break call at the target address. If the
    // break point has been removed it will have disappeared. If it have
    // disappeared don't try to look in the original code as the running code
    // will have the right address. This takes care of the case where the last
    // break point is removed from the function and therefore no "original code"
    // is available. If the debug break call is still there find the address in
    // the original code.
    if (IsDebugBreak(Assembler::target_address_at(addr))) {
      // If the break point is still there find the call address which was
      // overwritten in the original code by the call to DebugBreakXXX.

      // Find the corresponding address in the original code.
      addr += original_code->instruction_start() - code->instruction_start();
    }

    // Install jump to the call address in the original code. This will be the
    // call which was overwritten by the call to DebugBreakXXX.
    thread_local_.after_break_target_ = Assembler::target_address_at(addr);
  }
}


Code* Debug::GetCodeTarget(Address target) {
  // Maybe this can be refactored with the stuff in ic-inl.h?
  Code* result =
      Code::cast(HeapObject::FromAddress(target - Code::kHeaderSize));
  return result;
}


bool Debug::IsDebugGlobal(GlobalObject* global) {
  return IsLoaded() && global == Debug::debug_context()->global();
}


bool Debugger::debugger_active_ = false;
bool Debugger::compiling_natives_ = false;
DebugMessageThread* Debugger::message_thread_ = NULL;
v8::DebugMessageHandler Debugger::debug_message_handler_ = NULL;
void* Debugger::debug_message_handler_data_ = NULL;

Mutex* Debugger::pending_requests_access_ = OS::CreateMutex();
PendingRequest* Debugger::pending_requests_head_ = NULL;
PendingRequest* Debugger::pending_requests_tail_ = NULL;


void Debugger::DebugRequest(const uint16_t* json_request, int length) {
  // Create a pending request.
  PendingRequest* pending_request = new PendingRequest(json_request, length);

  // Add the pending request to list.
  Guard with(pending_requests_access_);
  if (pending_requests_head_ == NULL) {
    ASSERT(pending_requests_tail_ == NULL);
    pending_requests_head_ = pending_request;
    pending_requests_tail_ = pending_request;
  } else {
    ASSERT(pending_requests_tail_ != NULL);
    pending_requests_tail_->set_next(pending_request);
    pending_requests_tail_ = pending_request;
  }

  // Set the pending request flag to force the VM to stop soon.
  v8::Debug::DebugBreak();
}


bool Debugger::ProcessPendingRequests() {
  HandleScope scope;

  // Lock access to pending requests list while processing them. Typically
  // there will be either zero or one pending request.
  Guard with(pending_requests_access_);

  EnterDebuggerContext enter;

  // Get the current execution state.
  bool caught_exception;
  Handle<Object> exec_state = MakeExecutionState(&caught_exception);
  if (caught_exception) {
    return false;
  }

  // Process the list of pending requests.
  bool plain_break = false;
  PendingRequest* pending_request = pending_requests_head_;
  if (pending_request == NULL) {
    // If no pending commands plain break issued some other way (e.g. debugger
    // statement).
    plain_break = true;
  }
  while (pending_request != NULL) {
    Handle<String> response = ProcessRequest(exec_state,
                                             pending_request->request(),
                                             false);
    OnPendingRequestProcessed(response);

    // Check whether one of the commands is a plain break request.
    if (!plain_break) {
      plain_break = IsPlainBreakRequest(pending_request->request());
    }

    // Move to the next item in the list.
    PendingRequest* next = pending_request->next();
    delete pending_request;
    pending_request = next;
  }

  // List processed.
  pending_requests_head_ = NULL;
  pending_requests_tail_ = NULL;

  return plain_break;
}


Handle<Object> Debugger::MakeJSObject(Vector<const char> constructor_name,
                                      int argc, Object*** argv,
                                      bool* caught_exception) {
  ASSERT(Top::context() == *Debug::debug_context());

  // Create the execution state object.
  Handle<String> constructor_str = Factory::LookupSymbol(constructor_name);
  Handle<Object> constructor(Top::global()->GetProperty(*constructor_str));
  ASSERT(constructor->IsJSFunction());
  if (!constructor->IsJSFunction()) {
    *caught_exception = true;
    return Factory::undefined_value();
  }
  Handle<Object> js_object = Execution::TryCall(
      Handle<JSFunction>::cast(constructor),
      Handle<JSObject>(Debug::debug_context()->global()), argc, argv,
      caught_exception);
  return js_object;
}


Handle<Object> Debugger::MakeExecutionState(bool* caught_exception) {
  // Create the execution state object.
  Handle<Object> break_id = Factory::NewNumberFromInt(Top::break_id());
  const int argc = 1;
  Object** argv[argc] = { break_id.location() };
  return MakeJSObject(CStrVector("MakeExecutionState"),
                      argc, argv, caught_exception);
}


Handle<Object> Debugger::MakeBreakEvent(Handle<Object> exec_state,
                                        Handle<Object> break_points_hit,
                                        bool* caught_exception) {
  // Create the new break event object.
  const int argc = 2;
  Object** argv[argc] = { exec_state.location(),
                          break_points_hit.location() };
  return MakeJSObject(CStrVector("MakeBreakEvent"),
                      argc,
                      argv,
                      caught_exception);
}


Handle<Object> Debugger::MakeExceptionEvent(Handle<Object> exec_state,
                                            Handle<Object> exception,
                                            bool uncaught,
                                            bool* caught_exception) {
  // Create the new exception event object.
  const int argc = 3;
  Object** argv[argc] = { exec_state.location(),
                          exception.location(),
                          uncaught ? Factory::true_value().location() :
                                     Factory::false_value().location()};
  return MakeJSObject(CStrVector("MakeExceptionEvent"),
                      argc, argv, caught_exception);
}


Handle<Object> Debugger::MakeNewFunctionEvent(Handle<Object> function,
                                              bool* caught_exception) {
  // Create the new function event object.
  const int argc = 1;
  Object** argv[argc] = { function.location() };
  return MakeJSObject(CStrVector("MakeNewFunctionEvent"),
                      argc, argv, caught_exception);
}


Handle<Object> Debugger::MakeCompileEvent(Handle<Script> script,
                                          Handle<Object> script_function,
                                          bool* caught_exception) {
  // Create the compile event object.
  Handle<Object> exec_state = MakeExecutionState(caught_exception);
  Handle<Object> script_source(script->source());
  Handle<Object> script_name(script->name());
  const int argc = 3;
  Object** argv[argc] = { script_source.location(),
                          script_name.location(),
                          script_function.location() };
  return MakeJSObject(CStrVector("MakeCompileEvent"),
                      argc,
                      argv,
                      caught_exception);
}


Handle<String> Debugger::ProcessRequest(Handle<Object> exec_state,
                                        Handle<Object> request,
                                        bool stopped) {
  // Get the function ProcessDebugRequest (declared in debug.js).
  Handle<JSFunction> process_denbug_request =
    Handle<JSFunction>(JSFunction::cast(
    Debug::debug_context()->global()->GetProperty(
        *Factory::LookupAsciiSymbol("ProcessDebugRequest"))));

  // Call ProcessDebugRequest expect String result. The ProcessDebugRequest
  // will never throw an exception (see debug.js).
  bool caught_exception;
  const int argc = 3;
  Object** argv[argc] = { exec_state.location(),
                          request.location(),
                          stopped ? Factory::true_value().location() :
                                    Factory::false_value().location()};
  Handle<Object> result = Execution::TryCall(process_denbug_request,
                                             Factory::undefined_value(),
                                             argc, argv,
                                             &caught_exception);
  if (caught_exception) {
    return Factory::empty_symbol();
  }

  return Handle<String>::cast(result);
}


bool Debugger::IsPlainBreakRequest(Handle<Object> request) {
  // Get the function IsPlainBreakRequest (defined in debug.js).
  Handle<JSFunction> process_debug_request =
    Handle<JSFunction>(JSFunction::cast(
    Debug::debug_context()->global()->GetProperty(
        *Factory::LookupAsciiSymbol("IsPlainBreakRequest"))));

  // Call ProcessDebugRequest expect String result.
  bool caught_exception;
  const int argc = 1;
  Object** argv[argc] = { request.location() };
  Handle<Object> result = Execution::TryCall(process_debug_request,
                                             Factory::undefined_value(),
                                             argc, argv,
                                             &caught_exception);
  if (caught_exception) {
    return false;
  }

  return *result == Heap::true_value();
}


void Debugger::OnException(Handle<Object> exception, bool uncaught) {
  HandleScope scope;

  // Bail out based on state or if there is no listener for this event
  if (Debug::InDebugger()) return;
  if (!Debugger::EventActive(v8::Exception)) return;

  // Bail out if exception breaks are not active
  if (uncaught) {
    // Uncaught exceptions are reported by either flags.
    if (!(Debug::break_on_uncaught_exception() ||
          Debug::break_on_exception())) return;
  } else {
    // Caught exceptions are reported is activated.
    if (!Debug::break_on_exception()) return;
  }

  // Enter the debugger.  Bail out if the debugger cannot be loaded.
  if (!Debug::Load()) return;
  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // Clear all current stepping setup.
  Debug::ClearStepping();
  // Create the event data object.
  bool caught_exception = false;
  Handle<Object> exec_state = MakeExecutionState(&caught_exception);
  Handle<Object> event_data;
  if (!caught_exception) {
    event_data = MakeExceptionEvent(exec_state, exception, uncaught,
                                    &caught_exception);
  }
  // Bail out and don't call debugger if exception.
  if (caught_exception) {
    return;
  }

  // Process debug event
  ProcessDebugEvent(v8::Exception, event_data);
  // Return to continue execution from where the exception was thrown.
}


void Debugger::OnDebugBreak(Handle<Object> break_points_hit) {
  HandleScope scope;

  // Debugger has already been entered by caller.
  ASSERT(Top::context() == *Debug::debug_context());

  // Bail out if there is no listener for this event
  if (!Debugger::EventActive(v8::Break)) return;

  // Debugger must be entered in advance.
  ASSERT(Top::context() == *Debug::debug_context());

  // Create the event data object.
  bool caught_exception = false;
  Handle<Object> exec_state = MakeExecutionState(&caught_exception);
  Handle<Object> event_data;
  if (!caught_exception) {
    event_data = MakeBreakEvent(exec_state, break_points_hit,
                                &caught_exception);
  }
  // Bail out and don't call debugger if exception.
  if (caught_exception) {
    return;
  }

  // Process debug event
  ProcessDebugEvent(v8::Break, event_data);
}


void Debugger::OnBeforeCompile(Handle<Script> script) {
  HandleScope scope;

  // Bail out based on state or if there is no listener for this event
  if (Debug::InDebugger()) return;
  if (compiling_natives()) return;
  if (!EventActive(v8::BeforeCompile)) return;

  // Enter the debugger.  Bail out if the debugger cannot be loaded.
  if (!Debug::Load()) return;
  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // Create the event data object.
  bool caught_exception = false;
  Handle<Object> event_data = MakeCompileEvent(script,
                                               Factory::undefined_value(),
                                               &caught_exception);
  // Bail out and don't call debugger if exception.
  if (caught_exception) {
    return;
  }

  // Process debug event
  ProcessDebugEvent(v8::BeforeCompile, event_data);
}


// Handle debugger actions when a new script is compiled.
void Debugger::OnAfterCompile(Handle<Script> script, Handle<JSFunction> fun) {
  HandleScope scope;

  // No compile events while compiling natives.
  if (compiling_natives()) return;

  // No more to do if not debugging.
  if (!debugger_active()) return;

  // Enter the debugger.  Bail out if the debugger cannot be loaded.
  if (!Debug::Load()) return;
  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // If debugging there might be script break points registered for this
  // script. Make sure that these break points are set.

  // Get the function UpdateScriptBreakPoints (defined in debug-delay.js).
  Handle<Object> update_script_break_points =
      Handle<Object>(Debug::debug_context()->global()->GetProperty(
          *Factory::LookupAsciiSymbol("UpdateScriptBreakPoints")));
  if (!update_script_break_points->IsJSFunction()) {
    return;
  }
  ASSERT(update_script_break_points->IsJSFunction());

  // Wrap the script object in a proper JS object before passing it
  // to JavaScript.
  Handle<JSValue> wrapper = GetScriptWrapper(script);

  // Call UpdateScriptBreakPoints expect no exceptions.
  bool caught_exception = false;
  const int argc = 1;
  Object** argv[argc] = { reinterpret_cast<Object**>(wrapper.location()) };
  Handle<Object> result = Execution::TryCall(
      Handle<JSFunction>::cast(update_script_break_points),
      Top::builtins(), argc, argv,
      &caught_exception);
  if (caught_exception) {
    return;
  }

  // Bail out based on state or if there is no listener for this event
  if (Debug::InDebugger()) return;
  if (!Debugger::EventActive(v8::AfterCompile)) return;

  // Create the compile state object.
  Handle<Object> event_data = MakeCompileEvent(script,
                                               Factory::undefined_value(),
                                               &caught_exception);
  // Bail out and don't call debugger if exception.
  if (caught_exception) {
    return;
  }

  // Process debug event
  ProcessDebugEvent(v8::AfterCompile, event_data);
}


void Debugger::OnNewFunction(Handle<JSFunction> function) {
  return;
  HandleScope scope;

  // Bail out based on state or if there is no listener for this event
  if (Debug::InDebugger()) return;
  if (compiling_natives()) return;
  if (!Debugger::EventActive(v8::NewFunction)) return;

  // Enter the debugger.  Bail out if the debugger cannot be loaded.
  if (!Debug::Load()) return;
  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // Create the event object.
  bool caught_exception = false;
  Handle<Object> event_data = MakeNewFunctionEvent(function, &caught_exception);
  // Bail out and don't call debugger if exception.
  if (caught_exception) {
    return;
  }

  // Process debug event.
  ProcessDebugEvent(v8::NewFunction, event_data);
}


void Debugger::OnPendingRequestProcessed(Handle<Object> event_data) {
  // Process debug event.
  ProcessDebugEvent(v8::PendingRequestProcessed, event_data);
}


void Debugger::ProcessDebugEvent(v8::DebugEvent event,
                                 Handle<Object> event_data) {
  // Create the execution state.
  bool caught_exception = false;
  Handle<Object> exec_state = MakeExecutionState(&caught_exception);
  if (caught_exception) {
    return;
  }

  // First notify the builtin debugger.
  if (message_thread_ != NULL) {
    message_thread_->DebugEvent(event, exec_state, event_data);
  }

  // Notify registered debug event listeners. The list can contain both C and
  // JavaScript functions.
  v8::NeanderArray listeners(Factory::debug_event_listeners());
  int length = listeners.length();
  for (int i = 0; i < length; i++) {
    if (listeners.get(i)->IsUndefined()) continue;   // Skip deleted ones.
    v8::NeanderObject listener(JSObject::cast(listeners.get(i)));
    Handle<Object> callback_data(listener.get(1));
    if (listener.get(0)->IsProxy()) {
      // C debug event listener.
      Handle<Proxy> callback_obj(Proxy::cast(listener.get(0)));
      v8::DebugEventCallback callback =
            FUNCTION_CAST<v8::DebugEventCallback>(callback_obj->proxy());
      callback(event,
               v8::Utils::ToLocal(Handle<JSObject>::cast(exec_state)),
               v8::Utils::ToLocal(Handle<JSObject>::cast(event_data)),
               v8::Utils::ToLocal(callback_data));
    } else {
      // JavaScript debug event listener.
      ASSERT(listener.get(0)->IsJSFunction());
      Handle<JSFunction> fun(JSFunction::cast(listener.get(0)));

      // Invoke the JavaScript debug event listener.
      const int argc = 4;
      Object** argv[argc] = { Handle<Object>(Smi::FromInt(event)).location(),
                              exec_state.location(),
                              event_data.location(),
                              callback_data.location() };
      Handle<Object> result = Execution::TryCall(fun, Top::global(),
                                                 argc, argv, &caught_exception);
      if (caught_exception) {
        // Silently ignore exceptions from debug event listeners.
      }
    }
  }
}


void Debugger::SetMessageHandler(v8::DebugMessageHandler handler, void* data) {
  debug_message_handler_ = handler;
  debug_message_handler_data_ = data;
  if (!message_thread_) {
    message_thread_  = new DebugMessageThread();
    message_thread_->Start();
  }
  UpdateActiveDebugger();
}


void Debugger::SendMessage(Vector< uint16_t> message) {
  if (debug_message_handler_ != NULL) {
    debug_message_handler_(message.start(), message.length(),
                           debug_message_handler_data_);
  }
}


void Debugger::ProcessCommand(Vector<const uint16_t> command) {
  if (message_thread_ != NULL) {
    message_thread_->ProcessCommand(
        Vector<uint16_t>(const_cast<uint16_t *>(command.start()),
                         command.length()));
  }
}


void Debugger::UpdateActiveDebugger() {
  v8::NeanderArray listeners(Factory::debug_event_listeners());
  int length = listeners.length();
  bool active_listener = false;
  for (int i = 0; i < length && !active_listener; i++) {
    active_listener = !listeners.get(i)->IsUndefined();
  }

  set_debugger_active((Debugger::message_thread_ != NULL &&
                       Debugger::debug_message_handler_ != NULL) ||
                       active_listener);
  if (!debugger_active() && message_thread_)
    message_thread_->OnDebuggerInactive();
}


DebugMessageThread::DebugMessageThread()
    : host_running_(true),
      event_json_(Vector<uint16_t>::empty()),
      command_(Vector<uint16_t>::empty()),
      result_(Vector<uint16_t>::empty()) {
  command_received_ = OS::CreateSemaphore(0);
  debug_event_ = OS::CreateSemaphore(0);
  debug_command_ = OS::CreateSemaphore(0);
  debug_result_ = OS::CreateSemaphore(0);
}


DebugMessageThread::~DebugMessageThread() {
}


void DebugMessageThread::SetEventJSON(Vector<uint16_t> event_json) {
  SetVector(&event_json_, event_json);
}


void DebugMessageThread::SetEventJSONFromEvent(Handle<Object> event_data) {
  v8::HandleScope scope;
  // Call toJSONProtocol on the debug event object.
  v8::Local<v8::Object> api_event_data =
      v8::Utils::ToLocal(Handle<JSObject>::cast(event_data));
  v8::Local<v8::String> fun_name = v8::String::New("toJSONProtocol");
  v8::Local<v8::Function> fun =
      v8::Function::Cast(*api_event_data->Get(fun_name));
  v8::TryCatch try_catch;
  v8::Local<v8::Value> json_result = *fun->Call(api_event_data, 0, NULL);
  v8::Local<v8::String> json_result_string;
  if (!try_catch.HasCaught()) {
    if (!json_result->IsUndefined()) {
      json_result_string = json_result->ToString();
      if (FLAG_trace_debug_json) {
        PrintLn(json_result_string);
      }
      v8::String::Value val(json_result_string);
      Vector<uint16_t> str(reinterpret_cast<uint16_t*>(*val),
                          json_result_string->Length());
      SetEventJSON(str);
    } else {
      SetEventJSON(Vector<uint16_t>::empty());
    }
  } else {
    PrintLn(try_catch.Exception());
    SetEventJSON(Vector<uint16_t>::empty());
  }
}


void DebugMessageThread::SetCommand(Vector<uint16_t> command) {
  SetVector(&command_, command);
}


void DebugMessageThread::SetResult(const char* result) {
  int len = strlen(result);
  uint16_t* tmp = NewArray<uint16_t>(len);
  for (int i = 0; i < len; i++) {
    tmp[i] = result[i];
  }
  SetResult(Vector<uint16_t>(tmp, len));
  DeleteArray(tmp);
}


void DebugMessageThread::SetResult(Vector<uint16_t> result) {
  SetVector(&result_, result);
}


void DebugMessageThread::SetVector(Vector<uint16_t>* vector,
                                   Vector<uint16_t> value) {
  // Deallocate current result.
  if (!vector->is_empty()) {
    vector->Dispose();
    *vector = Vector<uint16_t>::empty();
  }

  // Allocate a copy of the new result.
  if (!value.is_empty()) {
    *vector = value.Clone();
  }
}


// Compare a two byte string to an null terminated ASCII string.
bool DebugMessageThread::TwoByteEqualsAscii(Vector<uint16_t> two_byte,
                                            const char* ascii) {
  for (int i = 0; i < two_byte.length(); i++) {
    if (ascii[i] == '\0') {
      return false;
    }
    if (two_byte[i] != static_cast<uint16_t>(ascii[i])) {
      return false;
    }
  }
  return ascii[two_byte.length()] == '\0';
}


void DebugMessageThread::CommandResult(Vector<uint16_t> result) {
  SetResult(result);
  debug_result_->Signal();
}


void DebugMessageThread::Run() {
  // Process commands and debug events.
  while (true) {
    // Set the current command prompt
    Semaphore* sems[2];
    sems[0] = command_received_;
    sems[1] = debug_event_;
    int signal = Select(2, sems).WaitSingle();
    if (signal == 0) {
      if (command_.length() > 0) {
        HandleCommand();
        if (result_.length() > 0) {
          Debugger::SendMessage(result_);
          SetResult(Vector<uint16_t>::empty());
        }
      }
    } else {
      // Send the the current event as JSON to the debugger.
      Debugger::SendMessage(event_json_);
    }
  }
}


// This method is called by the V8 thread whenever a debug event occurs in
// the VM.
void DebugMessageThread::DebugEvent(v8::DebugEvent event,
                                    Handle<Object> exec_state,
                                    Handle<Object> event_data) {
  if (!Debug::Load()) return;

  // Process the individual events.
  bool interactive = false;
  switch (event) {
    case v8::Break:
      interactive = true;  // Break event is always interavtive
      break;
    case v8::Exception:
      interactive = true;  // Exception event is always interavtive
      break;
    case v8::BeforeCompile:
      break;
    case v8::AfterCompile:
      break;
    case v8::NewFunction:
      break;
    case v8::PendingRequestProcessed: {
      // For a processed pending request the event_data is the JSON response
      // string.
      v8::Handle<v8::String> str =
          v8::Handle<v8::String>(
              Utils::ToLocal(Handle<String>::cast(event_data)));
      v8::String::Value val(str);
      SetEventJSON(Vector<uint16_t>(reinterpret_cast<uint16_t*>(*val),
                   str->Length()));
      debug_event_->Signal();
      break;
    }
    default:
      UNREACHABLE();
  }

  // Done if not interactive.
  if (!interactive) return;

  // Get the DebugCommandProcessor.
  v8::Local<v8::Object> api_exec_state =
      v8::Utils::ToLocal(Handle<JSObject>::cast(exec_state));
  v8::Local<v8::String> fun_name =
      v8::String::New("debugCommandProcessor");
  v8::Local<v8::Function> fun =
      v8::Function::Cast(*api_exec_state->Get(fun_name));
  v8::TryCatch try_catch;
  v8::Local<v8::Object> cmd_processor =
      v8::Object::Cast(*fun->Call(api_exec_state, 0, NULL));
  if (try_catch.HasCaught()) {
    PrintLn(try_catch.Exception());
    return;
  }

  // Notify the debug session thread that a debug event has occoured.
  host_running_ = false;
  event_ = event;
  SetEventJSONFromEvent(event_data);
  debug_event_->Signal();

  // Wait for commands from the debug session.
  while (true) {
    debug_command_->Wait();
    ASSERT(!host_running_);
    if (!Debugger::debugger_active()) {
      host_running_ = true;
      return;
    }

    // Invoke the JavaScript to convert the debug command line to a JSON
    // request, invoke the JSON request and convert the JSON respose to a text
    // representation.
    v8::Local<v8::String> fun_name;
    v8::Local<v8::Function> fun;
    v8::Local<v8::Value> args[1];
    v8::TryCatch try_catch;
    fun_name = v8::String::New("processDebugCommand");
    fun = v8::Function::Cast(*cmd_processor->Get(fun_name));
    args[0] = v8::String::New(reinterpret_cast<uint16_t*>(command_.start()),
                              command_.length());
    v8::Local<v8::Value> result_val = fun->Call(cmd_processor, 1, args);

    // Get the result of the command.
    v8::Local<v8::String> result_string;
    bool running = false;
    if (!try_catch.HasCaught()) {
      // Get the result as an object.
      v8::Local<v8::Object> result = v8::Object::Cast(*result_val);

      // Log the JSON request/response.
      if (FLAG_trace_debug_json) {
        PrintLn(result->Get(v8::String::New("request")));
        PrintLn(result->Get(v8::String::New("response")));
      }

      // Get the running state.
      running = result->Get(v8::String::New("running"))->ToBoolean()->Value();

      // Get result text.
      v8::Local<v8::Value> text_result =
          result->Get(v8::String::New("response"));
      if (!text_result->IsUndefined()) {
        result_string = text_result->ToString();
      } else {
        result_string = v8::String::New("");
      }
    } else {
      // In case of failure the result text is the exception text.
      result_string = try_catch.Exception()->ToString();
    }

    // Convert text result to C string.
    v8::String::Value val(result_string);
    Vector<uint16_t> str(reinterpret_cast<uint16_t*>(*val),
                        result_string->Length());

    // Change the prompt if VM is running after this command.
    if (running) {
      host_running_ = true;
    }

    // Return the result.
    CommandResult(str);

    // Return from debug event processing is VM should be running.
    if (running) {
      return;
    }
  }
}


void DebugMessageThread::HandleCommand() {
  // Handle the command.
  if (TwoByteEqualsAscii(command_, "b") ||
      TwoByteEqualsAscii(command_, "break")) {
    v8::Debug::DebugBreak();
    SetResult("request queued");
  } else if (host_running_) {
    // Send the JSON command to the running VM.
    Debugger::DebugRequest(command_.start(), command_.length());
    SetResult("request queued");
  } else {
    debug_command_->Signal();
    debug_result_->Wait();
  }
}


void DebugMessageThread::ProcessCommand(Vector<uint16_t> command) {
  SetCommand(command);
  command_received_->Signal();
}


void DebugMessageThread::OnDebuggerInactive() {
  if (!host_running_) {
    debug_command_->Signal();
    SetResult("");
  }
}

} }  // namespace v8::internal
