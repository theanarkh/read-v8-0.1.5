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

#ifndef V8_TOP_H_
#define V8_TOP_H_

#include "frames-inl.h"

namespace v8 { namespace internal {


#define RETURN_IF_SCHEDULED_EXCEPTION() \
  if (Top::has_scheduled_exception()) return Top::PromoteScheduledException()

// Top has static variables used for JavaScript execution.

class SaveContext;  // Forward decleration.

class ThreadLocalTop BASE_EMBEDDED {
 public:
  Context* security_context_;
  // The context where the current execution method is created and for variable
  // lookups.
  Context* context_;
  Object* pending_exception_;
  // Use a separate value for scheduled exceptions to preserve the
  // invariants that hold about pending_exception.  We may want to
  // unify them later.
  Object* scheduled_exception_;
  bool external_caught_exception_;
  v8::TryCatch* try_catch_handler_;
  SaveContext* save_context_;

  // Stack.
  Address c_entry_fp_;  // the frame pointer of the top c entry frame
  Address handler_;   // try-blocks are chained through the stack
  bool stack_is_cooked_;
  inline bool stack_is_cooked() { return stack_is_cooked_; }
  inline void set_stack_is_cooked(bool value) { stack_is_cooked_ = value; }

  // Generated code scratch locations.
  int32_t formal_count_;

  // Call back function to report unsafe JS accesses.
  v8::FailedAccessCheckCallback failed_access_check_callback_;
};

#define TOP_ADDRESS_LIST(C) \
  C(handler_address)                    \
  C(c_entry_fp_address)                \
  C(context_address)                   \
  C(pending_exception_address)         \
  C(external_caught_exception_address) \
  C(security_context_address)

class Top {
 public:
  enum AddressId {
#define C(name) k_##name,
    TOP_ADDRESS_LIST(C)
#undef C
    k_top_address_count
  };

  static Address get_address_from_id(AddressId id);

  // Access to the security context from which JS execution started.
  // In a browser world, it is the JS context of the frame which initiated
  // JavaScript execution.
  static Context* security_context() { return thread_local_.security_context_; }
  static void set_security_context(Context* context) {
    ASSERT(context == NULL || context->IsGlobalContext());
    thread_local_.security_context_ = context;
  }
  static Context** security_context_address() {
    return &thread_local_.security_context_;
  }

  // Access to top context (where the current function object was created).
  static Context* context() { return thread_local_.context_; }
  static void set_context(Context* context) {
    thread_local_.context_ = context;
  }
  static Context** context_address() { return &thread_local_.context_; }

  static SaveContext* save_context() {return thread_local_.save_context_; }
  static void set_save_context(SaveContext* save) {
    thread_local_.save_context_ = save;
  }

  // Interface to pending exception.
  static Object* pending_exception() {
    ASSERT(has_pending_exception());
    return thread_local_.pending_exception_;
  }
  static bool external_caught_exception() {
    return thread_local_.external_caught_exception_;
  }
  static void set_pending_exception(Object* exception) {
    thread_local_.pending_exception_ = exception;
  }
  static void clear_pending_exception() {
    thread_local_.pending_exception_ = Heap::the_hole_value();
  }

  static Object** pending_exception_address() {
    return &thread_local_.pending_exception_;
  }
  static bool has_pending_exception() {
    return !thread_local_.pending_exception_->IsTheHole();
  }
  static v8::TryCatch* try_catch_handler() {
    return thread_local_.try_catch_handler_;
  }
  // This method is called by the api after operations that may throw
  // exceptions.  If an exception was thrown and not handled by an external
  // handler the exception is scheduled to be rethrown when we return to running
  // JavaScript code.  If an exception is scheduled true is returned.
  static bool optional_reschedule_exception(bool is_bottom_call);

  static bool* external_caught_exception_address() {
    return &thread_local_.external_caught_exception_;
  }

  static Object* scheduled_exception() {
    ASSERT(has_scheduled_exception());
    return thread_local_.scheduled_exception_;
  }
  static bool has_scheduled_exception() {
    return !thread_local_.scheduled_exception_->IsTheHole();
  }
  static void clear_scheduled_exception() {
    thread_local_.scheduled_exception_ = Heap::the_hole_value();
  }

  // Tells whether the current context has experienced an out of memory
  // exception.
  static bool is_out_of_memory();

  // JS execution stack (see frames.h).
  static Address c_entry_fp(ThreadLocalTop* thread) {
    return thread->c_entry_fp_;
  }
  static Address handler(ThreadLocalTop* thread) { return thread->handler_; }

  static inline Address* c_entry_fp_address() {
    return &thread_local_.c_entry_fp_;
  }
  static inline Address* handler_address() { return &thread_local_.handler_; }

  // Generated code scratch locations.
  static void* formal_count_address() { return &thread_local_.formal_count_; }

  static void new_break(StackFrame::Id break_frame_id);
  static void set_break(StackFrame::Id break_frame_id, int break_id);
  static bool check_break(int break_id);
  static bool is_break();
  static bool is_break_no_lock();
  static StackFrame::Id break_frame_id();
  static int break_id();

  static void MarkCompactPrologue();
  static void MarkCompactEpilogue();
  static void MarkCompactPrologue(char* archived_thread_data);
  static void MarkCompactEpilogue(char* archived_thread_data);
  static void PrintCurrentStackTrace(FILE* out);
  static void PrintStackTrace(FILE* out, char* thread_data);
  static void PrintStack(StringStream* accumulator);
  static void PrintStack();
  static Handle<String> StackTrace();

  // Returns if the top context may access the given global object. If
  // the result is false, the pending exception is guaranteed to be
  // set.
  static bool MayNamedAccess(JSObject* receiver,
                             Object* key,
                             v8::AccessType type);
  static bool MayIndexedAccess(JSObject* receiver,
                               uint32_t index,
                               v8::AccessType type);

  static void SetFailedAccessCheckCallback(
      v8::FailedAccessCheckCallback callback);
  static void ReportFailedAccessCheck(JSObject* receiver, v8::AccessType type);

  // Exception throwing support. The caller should use the result
  // of Throw() as its return value.
  static Failure* Throw(Object* exception, MessageLocation* location = NULL);
  // Re-throw an exception.  This involves no error reporting since
  // error reporting was handled when the exception was thrown
  // originally.
  static Failure* ReThrow(Object* exception, MessageLocation* location = NULL);
  static void ScheduleThrow(Object* exception);

  // Promote a scheduled exception to pending. Asserts has_scheduled_exception.
  static Object* PromoteScheduledException();
  static void DoThrow(Object* exception,
                      MessageLocation* location,
                      const char* message,
                      bool is_rethrow);
  static bool ShouldReportException(bool* is_caught_externally);
  static void ReportUncaughtException(Handle<Object> exception,
                                      MessageLocation* location,
                                      Handle<String> stack_trace);

  // Override command line flag.
  static void TraceException(bool flag);

  // Out of resource exception helpers.
  static Failure* StackOverflow();

  // Administration
  static void Initialize();
  static void TearDown();
  static void Iterate(ObjectVisitor* v);
  static void Iterate(ObjectVisitor* v, ThreadLocalTop* t);
  static char* Iterate(ObjectVisitor* v, char* t);

  static Handle<JSObject> global() {
    return Handle<JSObject>(context()->global());
  }
  static Handle<Context> global_context();

  static Handle<JSBuiltinsObject> builtins() {
    return Handle<JSBuiltinsObject>(thread_local_.context_->builtins());
  }
  static Handle<JSBuiltinsObject> security_context_builtins() {
    return Handle<JSBuiltinsObject>(
        thread_local_.security_context_->builtins());
  }

  static Object* LookupSpecialFunction(JSObject* receiver,
                                       JSObject* prototype,
                                       JSFunction* value);

  static void RegisterTryCatchHandler(v8::TryCatch* that);
  static void UnregisterTryCatchHandler(v8::TryCatch* that);

#define TOP_GLOBAL_CONTEXT_FIELD_ACCESSOR(index, type, name)  \
  static Handle<type> name() {                                \
    return Handle<type>(context()->global_context()->name()); \
  }
  GLOBAL_CONTEXT_FIELDS(TOP_GLOBAL_CONTEXT_FIELD_ACCESSOR)
#undef TOP_GLOBAL_CONTEXT_FIELD_ACCESSOR

  static inline ThreadLocalTop* GetCurrentThread() { return &thread_local_; }
  static int ArchiveSpacePerThread() { return sizeof(ThreadLocalTop); }
  static char* ArchiveThread(char* to);
  static char* RestoreThread(char* from);

 private:
  // The context that initiated this JS execution.
  static ThreadLocalTop thread_local_;
  static void InitializeThreadLocal();
  static void PrintStackTrace(FILE* out, ThreadLocalTop* thread);
  static void MarkCompactPrologue(ThreadLocalTop* archived_thread_data);
  static void MarkCompactEpilogue(ThreadLocalTop* archived_thread_data);

  // Debug.
  // Mutex for serializing access to break control structures.
  static Mutex* break_access_;

  // ID of the frame where execution is stopped by debugger.
  static StackFrame::Id break_frame_id_;

  // Counter to create unique id for each debug break.
  static int break_count_;

  // Current debug break, 0 if running.
  static int break_id_;

  friend class SaveContext;
  friend class AssertNoContextChange;
  friend class ExecutionAccess;

  static void FillCache();
};


class SaveContext BASE_EMBEDDED {
 public:
  SaveContext() :
      context_(Top::context()),
      security_context_(Top::security_context()),
      prev_(Top::save_context()) {
    Top::set_save_context(this);
  }

  ~SaveContext() {
    Top::set_context(*context_);
    Top::set_security_context(*security_context_);
    Top::set_save_context(prev_);
  }

  Handle<Context> context() { return context_; }
  Handle<Context> security_context() { return security_context_; }
  SaveContext* prev() { return prev_; }

 private:
  Handle<Context> context_;
  Handle<Context> security_context_;
  SaveContext* prev_;
};


class AssertNoContextChange BASE_EMBEDDED {
#ifdef DEBUG
 public:
  AssertNoContextChange() :
      context_(Top::context()),
      security_context_(Top::security_context()) {
  }

  ~AssertNoContextChange() {
    ASSERT(Top::context() == *context_);
    ASSERT(Top::security_context() == *security_context_);
  }

 private:
  HandleScope scope_;
  Handle<Context> context_;
  Handle<Context> security_context_;
#else
 public:
  AssertNoContextChange() { }
#endif
};


class ExecutionAccess BASE_EMBEDDED {
 public:
  ExecutionAccess();
  ~ExecutionAccess();
};

} }  // namespace v8::internal

#endif  // V8_TOP_H_
