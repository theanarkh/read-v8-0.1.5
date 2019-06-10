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

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "debug.h"
#include "prettyprinter.h"
#include "scopeinfo.h"
#include "scopes.h"
#include "runtime.h"

namespace v8 { namespace internal {

DEFINE_bool(trace, false, "trace function calls");
DEFINE_bool(defer_negation, true, "defer negation operation");
DECLARE_bool(debug_info);
DECLARE_bool(debug_code);

#ifdef DEBUG
DECLARE_bool(gc_greedy);
DEFINE_bool(trace_codegen, false,
            "print name of functions for which code is generated");
DEFINE_bool(print_code, false, "print generated code");
DEFINE_bool(print_builtin_code, false, "print generated code for builtins");
DEFINE_bool(print_source, false, "pretty print source code");
DEFINE_bool(print_builtin_source, false,
            "pretty print source code for builtins");
DEFINE_bool(print_ast, false, "print source AST");
DEFINE_bool(print_builtin_ast, false, "print source AST for builtins");
DEFINE_bool(trace_calls, false, "trace calls");
DEFINE_bool(trace_builtin_calls, false, "trace builtins calls");
DEFINE_string(stop_at, "", "function name where to insert a breakpoint");
#endif  // DEBUG


DEFINE_bool(check_stack, true,
            "check stack for overflow, interrupt, breakpoint");

#define TOS (Operand(esp, 0))


class Ia32CodeGenerator;

// Mode to overwrite BinaryExpression values.
enum OverwriteMode { NO_OVERWRITE, OVERWRITE_LEFT, OVERWRITE_RIGHT };


// -----------------------------------------------------------------------------
// Reference support

// A reference is a C++ stack-allocated object that keeps an ECMA
// reference on the execution stack while in scope. For variables
// the reference is empty, indicating that it isn't necessary to
// store state on the stack for keeping track of references to those.
// For properties, we keep either one (named) or two (indexed) values
// on the execution stack to represent the reference.

class Reference BASE_EMBEDDED {
 public:
  enum Type { ILLEGAL = -1, EMPTY = 0, NAMED = 1, KEYED = 2 };
  Reference(Ia32CodeGenerator* cgen, Expression* expression);
  ~Reference();

  Expression* expression() const  { return expression_; }
  Type type() const  { return type_; }
  void set_type(Type value)  {
    ASSERT(type_ == ILLEGAL);
    type_ = value;
  }
  int size() const  { return type_; }

  bool is_illegal() const  { return type_ == ILLEGAL; }

 private:
  Ia32CodeGenerator* cgen_;
  Expression* expression_;
  Type type_;
};


// -----------------------------------------------------------------------------
// Code generation state

class CodeGenState BASE_EMBEDDED {
 public:
  enum AccessType {
    UNDEFINED,
    LOAD,
    LOAD_TYPEOF_EXPR,
    STORE,
    INIT_CONST
  };

  CodeGenState()
      : access_(UNDEFINED),
        ref_(NULL),
        true_target_(NULL),
        false_target_(NULL) {
  }

  CodeGenState(AccessType access,
               Reference* ref,
               Label* true_target,
               Label* false_target)
      : access_(access),
        ref_(ref),
        true_target_(true_target),
        false_target_(false_target) {
  }

  AccessType access() const { return access_; }
  Reference* ref() const { return ref_; }
  Label* true_target() const { return true_target_; }
  Label* false_target() const { return false_target_; }

 private:
  AccessType access_;
  Reference* ref_;
  Label* true_target_;
  Label* false_target_;
};


// -----------------------------------------------------------------------------
// Ia32CodeGenerator

class Ia32CodeGenerator: public CodeGenerator {
 public:
  static Handle<Code> MakeCode(FunctionLiteral* fun,
                               Handle<Script> script,
                               bool is_eval);

  MacroAssembler* masm()  { return masm_; }

 private:
  // Assembler
  MacroAssembler* masm_;  // to generate code

  // Code generation state
  Scope* scope_;
  Condition cc_reg_;
  CodeGenState* state_;
  bool is_inside_try_;
  int break_stack_height_;

  // Labels
  Label function_return_;

  // Construction/destruction
  Ia32CodeGenerator(int buffer_size,
                    Handle<Script> script,
                    bool is_eval);
  virtual ~Ia32CodeGenerator()  { delete masm_; }

  // Main code generation function
  void GenCode(FunctionLiteral* fun);

  // The following are used by class Reference.
  void LoadReference(Reference* ref);
  void UnloadReference(Reference* ref);
  friend class Reference;

  bool TryDeferNegate(Expression* x);

  // State
  bool has_cc() const  { return cc_reg_ >= 0; }
  CodeGenState::AccessType access() const  { return state_->access(); }
  Reference* ref() const  { return state_->ref(); }
  bool is_referenced() const { return state_->ref() != NULL; }
  Label* true_target() const  { return state_->true_target(); }
  Label* false_target() const  { return state_->false_target(); }

  // Expressions
  Operand GlobalObject() const  {
    return ContextOperand(esi, Context::GLOBAL_INDEX);
  }

  // Support functions for accessing parameters.
  Operand ParameterOperand(int index) const {
    ASSERT(-2 <= index && index < scope_->num_parameters());
    return Operand(ebp, (1 + scope_->num_parameters() - index) * kPointerSize);
  }

  Operand ReceiverOperand() const { return ParameterOperand(-1); }
  Operand FunctionOperand() const {
    return Operand(ebp, JavaScriptFrameConstants::kFunctionOffset);
  }

  Operand ContextOperand(Register context, int index) const {
    return Operand(context, Context::SlotOffset(index));
  }

  Operand SlotOperand(Slot* slot, Register tmp);

  void LoadCondition(Expression* x,
                     CodeGenState::AccessType access,
                     Label* true_target,
                     Label* false_target,
                     bool force_cc);
  void Load(Expression* x,
            CodeGenState::AccessType access = CodeGenState::LOAD);
  void LoadGlobal();

  // Special code for typeof expressions: Unfortunately, we must
  // be careful when loading the expression in 'typeof'
  // expressions. We are not allowed to throw reference errors for
  // non-existing properties of the global object, so we must make it
  // look like an explicit property access, instead of an access
  // through the context chain.
  void LoadTypeofExpression(Expression* x);

  // References
  void AccessReference(Reference* ref, CodeGenState::AccessType access);

  void GetValue(Reference* ref) { AccessReference(ref, CodeGenState::LOAD); }
  void SetValue(Reference* ref) { AccessReference(ref, CodeGenState::STORE); }
  void InitConst(Reference* ref) {
    AccessReference(ref, CodeGenState::INIT_CONST);
  }

  void ToBoolean(Label* true_target, Label* false_target);


  // Access property from the reference (must be at the TOS).
  void AccessReferenceProperty(Expression* key,
                               CodeGenState::AccessType access);

  void GenericOperation(Token::Value op,
                        OverwriteMode overwrite_mode = NO_OVERWRITE);

  bool InlinedGenericOperation(
      Token::Value op,
      const OverwriteMode overwrite_mode = NO_OVERWRITE,
      bool negate_result = false);
  void Comparison(Condition cc, bool strict = false);

  void SmiComparison(Condition cc,  Handle<Object> value, bool strict = false);

  void SmiOperation(Token::Value op,
                    Handle<Object> value,
                    bool reversed,
                    OverwriteMode overwrite_mode);

  void CallWithArguments(ZoneList<Expression*>* arguments, int position);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  virtual void DeclareGlobals(Handle<FixedArray> pairs);

  // Instantiate the function boilerplate.
  void InstantiateBoilerplate(Handle<JSFunction> boilerplate);

  // Control flow
  void Branch(bool if_true, Label* L);
  void CheckStack();
  void CleanStack(int num_bytes);

  // Node visitors
#define DEF_VISIT(type)                         \
  virtual void Visit##type(type* node);
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  void RecordStatementPosition(Node* node);

  // Activation frames.
  void EnterJSFrame();
  void ExitJSFrame();

  virtual void GenerateShiftDownAndTailCall(ZoneList<Expression*>* args);
  virtual void GenerateSetThisFunction(ZoneList<Expression*>* args);
  virtual void GenerateGetThisFunction(ZoneList<Expression*>* args);
  virtual void GenerateSetThis(ZoneList<Expression*>* args);
  virtual void GenerateGetArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateSetArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateTailCallWithArguments(ZoneList<Expression*>* args);
  virtual void GenerateSetArgument(ZoneList<Expression*>* args);
  virtual void GenerateSquashFrame(ZoneList<Expression*>* args);
  virtual void GenerateExpandFrame(ZoneList<Expression*>* args);
  virtual void GenerateIsSmi(ZoneList<Expression*>* args);
  virtual void GenerateIsArray(ZoneList<Expression*>* args);

  virtual void GenerateArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateArgumentsAccess(ZoneList<Expression*>* args);

  virtual void GenerateValueOf(ZoneList<Expression*>* args);
  virtual void GenerateSetValueOf(ZoneList<Expression*>* args);
};


// -----------------------------------------------------------------------------
// Ia32CodeGenerator implementation

#define __  masm_->


Handle<Code> Ia32CodeGenerator::MakeCode(FunctionLiteral* flit,
                                         Handle<Script> script,
                                         bool is_eval) {
#ifdef DEBUG
  bool print_source = false;
  bool print_ast = false;
  bool print_code = false;
  const char* ftype;

  if (Bootstrapper::IsActive()) {
    print_source = FLAG_print_builtin_source;
    print_ast = FLAG_print_builtin_ast;
    print_code = FLAG_print_builtin_code;
    ftype = "builtin";
  } else {
    print_source = FLAG_print_source;
    print_ast = FLAG_print_ast;
    print_code = FLAG_print_code;
    ftype = "user-defined";
  }

  if (FLAG_trace_codegen || print_source || print_ast) {
    PrintF("*** Generate code for %s function: ", ftype);
    flit->name()->ShortPrint();
    PrintF(" ***\n");
  }

  if (print_source) {
    PrintF("--- Source from AST ---\n%s\n", PrettyPrinter().PrintProgram(flit));
  }

  if (print_ast) {
    PrintF("--- AST ---\n%s\n", AstPrinter().PrintProgram(flit));
  }
#endif  // DEBUG

  // Generate code.
  const int initial_buffer_size = 4 * KB;
  Ia32CodeGenerator cgen(initial_buffer_size, script, is_eval);
  cgen.GenCode(flit);
  if (cgen.HasStackOverflow()) {
    ASSERT(!Top::has_pending_exception());
    return Handle<Code>::null();
  }

  // Process any deferred code.
  cgen.ProcessDeferred();

  // Allocate and install the code.
  CodeDesc desc;
  cgen.masm()->GetCode(&desc);
  ScopeInfo<> sinfo(flit->scope());
  Code::Flags flags = Code::ComputeFlags(Code::FUNCTION);
  Handle<Code> code = Factory::NewCode(desc, &sinfo, flags);

  // Add unresolved entries in the code to the fixup list.
  Bootstrapper::AddFixup(*code, cgen.masm());

#ifdef DEBUG
  if (print_code) {
    // Print the source code if available.
    if (!script->IsUndefined() && !script->source()->IsUndefined()) {
      PrintF("--- Raw source ---\n");
      StringInputBuffer stream(String::cast(script->source()));
      stream.Seek(flit->start_position());
      // flit->end_position() points to the last character in the stream. We
      // need to compensate by adding one to calculate the length.
      int source_len = flit->end_position() - flit->start_position() + 1;
      for (int i = 0; i < source_len; i++) {
        if (stream.has_more()) PrintF("%c", stream.GetNext());
      }
      PrintF("\n\n");
    }
    PrintF("--- Code ---\n");
    code->Print();
  }
#endif  // DEBUG

  return code;
}


Ia32CodeGenerator::Ia32CodeGenerator(int buffer_size,
                                     Handle<Script> script,
                                     bool is_eval)
    : CodeGenerator(is_eval, script),
      masm_(new MacroAssembler(NULL, buffer_size)),
      scope_(NULL),
      cc_reg_(no_condition),
      state_(NULL),
      is_inside_try_(false),
      break_stack_height_(0) {
}


// Calling conventions:
// ebp: frame pointer
// esp: stack pointer
// edi: caller's parameter pointer
// esi: callee's context


void Ia32CodeGenerator::GenCode(FunctionLiteral* fun) {
  // Record the position for debugging purposes.
  __ RecordPosition(fun->start_position());

  Scope* scope = fun->scope();
  ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  { CodeGenState state;
    state_ = &state;
    scope_ = scope;
    cc_reg_ = no_condition;

    // Entry
    // stack: function, receiver, arguments, return address
    // esp: stack pointer
    // ebp: frame pointer
    // edi: caller's parameter pointer
    // esi: callee's context

    { Comment cmnt(masm_, "[ enter JS frame");
      EnterJSFrame();
    }
    // tos: code slot
#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      __ int3();
    }
#endif

    // This section now only allocates and copies the formals into the
    // arguments object. It saves the address in ecx, which is saved
    // at any point before either garbage collection or ecx is
    // overwritten.  The flag arguments_array_allocated communicates
    // with the store into the arguments variable and guards the lazy
    // pushes of ecx to TOS.  The flag arguments_array_saved notes
    // when the push has happened.
    bool arguments_object_allocated = false;
    bool arguments_object_saved = false;

    // Allocate arguments object.
    // The arguments object pointer needs to be saved in ecx, since we need
    // to store arguments into the context.
    if (scope->arguments() != NULL) {
      ASSERT(scope->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ allocate arguments object");
      __ push(FunctionOperand());
      __ CallRuntime(Runtime::kNewArguments, 1);
      __ mov(ecx, Operand(eax));
      arguments_object_allocated = true;
    }

    // Allocate space for locals and initialize them.
    if (scope->num_stack_slots() > 0) {
      Comment cmnt(masm_, "[ allocate space for locals");
      __ Set(eax, Immediate(Factory::undefined_value()));
      for (int i = scope->num_stack_slots(); i-- > 0; ) __ push(eax);
    }

    if (scope->num_heap_slots() > 0) {
      Comment cmnt(masm_, "[ allocate local context");
      // Save the arguments object pointer, if any.
      if (arguments_object_allocated && !arguments_object_saved) {
        __ push(Operand(ecx));
        arguments_object_saved = true;
      }
      // Allocate local context.
      // Get outer context and create a new context based on it.
      __ push(FunctionOperand());
      __ CallRuntime(Runtime::kNewContext, 2);
      __ push(eax);
      // Update context local.
      __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
      // Restore the arguments array pointer, if any.
    }

    // TODO(1241774): Improve this code:
    // 1) only needed if we have a context
    // 2) no need to recompute context ptr every single time
    // 3) don't copy parameter operand code from SlotOperand!
    {
      Comment cmnt2(masm_, "[ copy context parameters into .context");

      // Note that iteration order is relevant here! If we have the same
      // parameter twice (e.g., function (x, y, x)), and that parameter
      // needs to be copied into the context, it must be the last argument
      // passed to the parameter that needs to be copied. This is a rare
      // case so we don't check for it, instead we rely on the copying
      // order: such a parameter is copied repeatedly into the same
      // context location and thus the last value is what is seen inside
      // the function.
      for (int i = 0; i < scope->num_parameters(); i++) {
        Variable* par = scope->parameter(i);
        Slot* slot = par->slot();
        if (slot != NULL && slot->type() == Slot::CONTEXT) {
          // Save the arguments object pointer, if any.
          if (arguments_object_allocated && !arguments_object_saved) {
            __ push(Operand(ecx));
            arguments_object_saved = true;
          }
          ASSERT(!scope->is_global_scope());  // no parameters in global scope
          __ mov(eax, ParameterOperand(i));
          // Loads ecx with context; used below in RecordWrite.
          __ mov(SlotOperand(slot, ecx), eax);
          int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ RecordWrite(ecx, offset, eax, ebx);
        }
      }
    }

    // This section stores the pointer to the arguments object that
    // was allocated and copied into above. If the address was not
    // saved to TOS, we push ecx onto the stack.

    // Store the arguments object.
    // This must happen after context initialization because
    // the arguments object may be stored in the context
    if (arguments_object_allocated) {
      ASSERT(scope->arguments() != NULL);
      ASSERT(scope->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ store arguments object");
      {
        Reference target(this, scope->arguments());
        if (!arguments_object_saved) {
          __ push(Operand(ecx));
        }
        SetValue(&target);
      }
      // The value of arguments must also be stored in .arguments.
      // TODO(1241813): This code can probably be improved by fusing it with
      // the code that stores the arguments object above.
      {
        Reference target(this, scope->arguments_shadow());
        Load(scope->arguments());
        SetValue(&target);
      }
    }

    // Generate code to 'execute' declarations and initialize
    // functions (source elements). In case of an illegal
    // redeclaration we need to handle that instead of processing the
    // declarations.
    if (scope->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ illegal redeclarations");
      scope->VisitIllegalRedeclaration(this);
    } else {
      Comment cmnt(masm_, "[ declarations");
      ProcessDeclarations(scope->declarations());
      // Bail out if a stack-overflow exception occured when
      // processing declarations.
      if (HasStackOverflow()) return;
    }

    if (FLAG_trace) {
      __ CallRuntime(Runtime::kTraceEnter, 1);
      __ push(eax);
    }
    CheckStack();

    // Compile the body of the function in a vanilla state. Don't
    // bother compiling all the code if the scope has an illegal
    // redeclaration.
    if (!scope->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ function body");
#ifdef DEBUG
      bool is_builtin = Bootstrapper::IsActive();
      bool should_trace =
          is_builtin ? FLAG_trace_builtin_calls : FLAG_trace_calls;
      if (should_trace) {
        __ CallRuntime(Runtime::kDebugTrace, 1);
        __ push(eax);
      }
#endif
      VisitStatements(body);

      // Generate a return statement if necessary.
      if (body->is_empty() || body->last()->AsReturnStatement() == NULL) {
        Literal undefined(Factory::undefined_value());
        ReturnStatement statement(&undefined);
        statement.set_statement_pos(fun->end_position());
        VisitReturnStatement(&statement);
      }
    }

    state_ = NULL;
  }

  // Code generation state must be reset.
  scope_ = NULL;
  ASSERT(!has_cc());
  ASSERT(state_ == NULL);
}


Operand Ia32CodeGenerator::SlotOperand(Slot* slot, Register tmp) {
  // Currently, this assertion will fail if we try to assign to
  // a constant variable that is constant because it is read-only
  // (such as the variable referring to a named function expression).
  // We need to implement assignments to read-only variables.
  // Ideally, we should do this during AST generation (by converting
  // such assignments into expression statements); however, in general
  // we may not be able to make the decision until past AST generation,
  // that is when the entire program is known.
  ASSERT(slot != NULL);
  int index = slot->index();
  switch (slot->type()) {
    case Slot::PARAMETER: return ParameterOperand(index);

    case Slot::LOCAL: {
      ASSERT(0 <= index && index < scope_->num_stack_slots());
      const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
      return Operand(ebp, kLocal0Offset - index * kPointerSize);
    }

    case Slot::CONTEXT: {
      // Follow the context chain if necessary.
      ASSERT(!tmp.is(esi));  // do not overwrite context register
      Register context = esi;
      int chain_length = scope_->ContextChainLength(slot->var()->scope());
      for (int i = chain_length; i-- > 0;) {
        // Load the closure.
        // (All contexts, even 'with' contexts, have a closure,
        // and it is the same for all contexts inside a function.
        // There is no need to go to the function context first.)
        __ mov(tmp, ContextOperand(context, Context::CLOSURE_INDEX));
        // Load the function context (which is the incoming, outer context).
        __ mov(tmp, FieldOperand(tmp, JSFunction::kContextOffset));
        context = tmp;
      }
      // We may have a 'with' context now. Get the function context.
      // (In fact this mov may never be the needed, since the scope analysis
      // may not permit a direct context access in this case and thus we are
      // always at a function context. However it is safe to dereference be-
      // cause the function context of a function context is itself. Before
      // deleting this mov we should try to create a counter-example first,
      // though...)
      __ mov(tmp, ContextOperand(context, Context::FCONTEXT_INDEX));
      return ContextOperand(tmp, index);
    }

    default:
      UNREACHABLE();
      return Operand(eax);
  }
}


// Loads a value on TOS. If it is a boolean value, the result may have been
// (partially) translated into branches, or it may have set the condition code
// register. If force_cc is set, the value is forced to set the condition code
// register and no value is pushed. If the condition code register was set,
// has_cc() is true and cc_reg_ contains the condition to test for 'true'.
void Ia32CodeGenerator::LoadCondition(Expression* x,
                                      CodeGenState::AccessType access,
                                      Label* true_target,
                                      Label* false_target,
                                      bool force_cc) {
  ASSERT(access == CodeGenState::LOAD ||
         access == CodeGenState::LOAD_TYPEOF_EXPR);
  ASSERT(!has_cc() && !is_referenced());

  CodeGenState* old_state = state_;
  CodeGenState new_state(access, NULL, true_target, false_target);
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
  if (force_cc && !has_cc()) {
    ToBoolean(true_target, false_target);
  }
  ASSERT(has_cc() || !force_cc);
}


void Ia32CodeGenerator::Load(Expression* x, CodeGenState::AccessType access) {
  ASSERT(access == CodeGenState::LOAD ||
         access == CodeGenState::LOAD_TYPEOF_EXPR);

  Label true_target;
  Label false_target;
  LoadCondition(x, access, &true_target, &false_target, false);

  if (has_cc()) {
    // convert cc_reg_ into a bool

    Label loaded, materialize_true;
    __ j(cc_reg_, &materialize_true);
    __ push(Immediate(Factory::false_value()));
    __ jmp(&loaded);
    __ bind(&materialize_true);
    __ push(Immediate(Factory::true_value()));
    __ bind(&loaded);
    cc_reg_ = no_condition;
  }

  if (true_target.is_linked() || false_target.is_linked()) {
    // we have at least one condition value
    // that has been "translated" into a branch,
    // thus it needs to be loaded explicitly again
    Label loaded;
    __ jmp(&loaded);  // don't lose current TOS
    bool both = true_target.is_linked() && false_target.is_linked();
    // reincarnate "true", if necessary
    if (true_target.is_linked()) {
      __ bind(&true_target);
      __ push(Immediate(Factory::true_value()));
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      __ jmp(&loaded);
    // reincarnate "false", if necessary
    if (false_target.is_linked()) {
      __ bind(&false_target);
      __ push(Immediate(Factory::false_value()));
    }
    // everything is loaded at this point
    __ bind(&loaded);
  }
  ASSERT(!has_cc());
}


void Ia32CodeGenerator::LoadGlobal() {
  __ push(GlobalObject());
}


// TODO(1241834): Get rid of this function in favor of just using Load, now
// that we have the LOAD_TYPEOF_EXPR access type. => Need to handle
// global variables w/o reference errors elsewhere.
void Ia32CodeGenerator::LoadTypeofExpression(Expression* x) {
  Variable* variable = x->AsVariableProxy()->AsVariable();
  if (variable != NULL && !variable->is_this() && variable->is_global()) {
    // NOTE: This is somewhat nasty. We force the compiler to load
    // the variable as if through '<global>.<variable>' to make sure we
    // do not get reference errors.
    Slot global(variable, Slot::CONTEXT, Context::GLOBAL_INDEX);
    Literal key(variable->name());
    // TODO(1241834): Fetch the position from the variable instead of using
    // no position.
    Property property(&global, &key, kNoPosition);
    Load(&property);
  } else {
    Load(x, CodeGenState::LOAD_TYPEOF_EXPR);
  }
}


Reference::Reference(Ia32CodeGenerator* cgen, Expression* expression)
    : cgen_(cgen), expression_(expression), type_(ILLEGAL) {
  cgen->LoadReference(this);
}


Reference::~Reference() {
  cgen_->UnloadReference(this);
}


void Ia32CodeGenerator::LoadReference(Reference* ref) {
  Expression* e = ref->expression();
  Property* property = e->AsProperty();
  Variable* var = e->AsVariableProxy()->AsVariable();

  if (property != NULL) {
    Load(property->obj());
    // Used a named reference if the key is a literal symbol.
    // We don't use a named reference if they key is a string that can be
    // legally parsed as an integer.  This is because, otherwise we don't
    // get into the slow case code that handles [] on String objects.
    Literal* literal = property->key()->AsLiteral();
    uint32_t dummy;
    if (literal != NULL && literal->handle()->IsSymbol() &&
      !String::cast(*(literal->handle()))->AsArrayIndex(&dummy)) {
      ref->set_type(Reference::NAMED);
    } else {
      Load(property->key());
      ref->set_type(Reference::KEYED);
    }
  } else if (var != NULL) {
    if (var->is_global()) {
      // global variable
      LoadGlobal();
      ref->set_type(Reference::NAMED);
    } else {
      // local variable
      ref->set_type(Reference::EMPTY);
    }
  } else {
    Load(e);
    __ CallRuntime(Runtime::kThrowReferenceError, 1);
    __ push(eax);
  }
}


void Ia32CodeGenerator::UnloadReference(Reference* ref) {
  // Pop n references on the stack while preserving TOS
  Comment cmnt(masm_, "[ UnloadReference");
  int size = ref->size();
  if (size <= 0) {
    // Do nothing. No popping is necessary.
  } else if (size == 1) {
    __ pop(eax);
    __ mov(TOS, eax);
  } else {
    __ pop(eax);
    __ add(Operand(esp), Immediate(size * kPointerSize));
    __ push(eax);
  }
}


void Ia32CodeGenerator::AccessReference(Reference* ref,
                                        CodeGenState::AccessType access) {
  ASSERT(!has_cc());
  ASSERT(ref->type() != Reference::ILLEGAL);
  CodeGenState* old_state = state_;
  CodeGenState new_state(access, ref, true_target(), false_target());
  state_ = &new_state;
  Visit(ref->expression());
  state_ = old_state;
}


// ECMA-262, section 9.2, page 30: ToBoolean(). Convert the given
// register to a boolean in the condition code register. The code
// may jump to 'false_target' in case the register converts to 'false'.
void Ia32CodeGenerator::ToBoolean(Label* true_target, Label* false_target) {
  // Note: The generated code snippet cannot change 'reg'.
  //       Only the condition code should be set.

  Comment cmnt(masm_, "[ ToBoolean");

  // the value to convert should be popped from the stack
  __ pop(eax);

  // Fast case checks

  // Check if value is 'false'.
  __ cmp(eax, Factory::false_value());
  __ j(equal, false_target);

  // Check if value is 'true'.
  __ cmp(eax, Factory::true_value());
  __ j(equal, true_target);

  // Check if reg is 'undefined'.
  __ cmp(eax, Factory::undefined_value());
  __ j(equal, false_target);

  // Check if reg is 'null'.
  __ cmp(eax, Factory::null_value());
  __ j(equal, false_target);

  // Check if value is a Smi.
  __ cmp(eax, reinterpret_cast<intptr_t>(Smi::FromInt(0)));
  __ j(equal, false_target);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, true_target, taken);

  // Slow case: call the runtime.
  __ push(eax);  // undo the pop(eax) from above
  __ CallRuntime(Runtime::kToBool, 1);
  // Convert result (eax) to condition code
  __ cmp(eax, Factory::false_value());

  ASSERT(not_equal == not_zero);
  cc_reg_ = not_equal;
}


void Ia32CodeGenerator::AccessReferenceProperty(
    Expression* key,
    CodeGenState::AccessType access) {
  Reference::Type type = ref()->type();
  ASSERT(type != Reference::ILLEGAL);

  // TODO(1241834): Make sure that this is sufficient. If there is a chance
  // that reference errors can be thrown below, we must distinguish
  // between the 2 kinds of loads (typeof expression loads must not
  // throw a reference errror).
  bool is_load = (access == CodeGenState::LOAD ||
                  access == CodeGenState::LOAD_TYPEOF_EXPR);

  if (type == Reference::NAMED) {
    // Compute the name of the property.
    Literal* literal = key->AsLiteral();
    Handle<String> name(String::cast(*literal->handle()));

    // Call the appropriate IC code.
    if (is_load) {
      Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
      Variable* var = ref()->expression()->AsVariableProxy()->AsVariable();
      // Setup the name register.
      __ Set(ecx, Immediate(name));
      if (var != NULL) {
        ASSERT(var->is_global());
        __ call(ic, code_target_context);
      } else {
        __ call(ic, code_target);
      }
    } else {
      Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      __ pop(eax);
      // Setup the name register.
      __ Set(ecx, Immediate(name));
      __ call(ic, code_target);
    }
  } else {
    // Access keyed property.
    ASSERT(type == Reference::KEYED);

    if (is_load) {
      // Call IC code.
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
      Variable* var = ref()->expression()->AsVariableProxy()->AsVariable();
      if (var != NULL) {
        ASSERT(var->is_global());
        __ call(ic, code_target_context);
      } else {
        __ call(ic, code_target);
      }
    } else {
      // Call IC code.
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      __ pop(eax);
      __ call(ic, code_target);
    }
  }
  __ push(eax);  // IC call leaves result in eax, push it out
}


#undef __
#define __  masm->


class FloatingPointHelper : public AllStatic {
 public:
  // Code pattern for loading floating point values. Input values must
  // be either Smi or heap number objects (fp values). Requirements:
  // operand_1 on TOS+1 , operand_2 on TOS+2; Returns operands as
  // floating point numbers on FPU stack.
  static void LoadFloatOperands(MacroAssembler* masm, Register scratch);
  // Test if operands are Smi or number objects (fp). Requirements:
  // operand_1 in eax, operand_2 in edx; falls through on float
  // operands, jumps to the non_float label otherwise.
  static void CheckFloatOperands(MacroAssembler* masm,
                                 Label* non_float,
                                 Register scratch);
  // Allocate a heap number in new space with undefined value.
  // Returns tagged pointer in eax, or jumps to need_gc if new space is full.
  static void AllocateHeapNumber(MacroAssembler* masm,
                                 Label* need_gc,
                                 Register scratch1,
                                 Register scratch2);
};


class InlinedGenericOpStub: public CodeStub {
 public:
  InlinedGenericOpStub(Token::Value op, OverwriteMode mode, bool negate_result)
      : op_(op), mode_(mode), negate_result_(negate_result) { }

 private:
  Token::Value op_;
  OverwriteMode mode_;
  bool negate_result_;

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("InlinedGenericOpStub (op %s), (mode %d), (negate_result %s)\n",
           Token::String(op_),
           static_cast<int>(mode_),
           negate_result_ ? "true" : "false");
  }
#endif

  // Minor key encoding in 16 bits OOOOOOOOOOOOOMMN.
  class NegateBits: public BitField<bool, 0, 1> {};
  class ModeBits: public BitField<OverwriteMode, 1, 2> {};
  class OpBits: public BitField<Token::Value, 3, 13> {};

  Major MajorKey() { return InlinedGenericOp; }
  int MinorKey() {
    // Encode the three parameters in a unique 16 bit value.
    return NegateBits::encode(negate_result_) |
           OpBits::encode(op_) |
           ModeBits::encode(mode_);
  }
  void Generate(MacroAssembler* masm);
};


const char* InlinedGenericOpStub::GetName() {
  switch (op_) {
  case Token::ADD: return "InlinedGenericOpStub_ADD";
  case Token::SUB: return "InlinedGenericOpStub_SUB";
  case Token::MUL: return "InlinedGenericOpStub_MUL";
  case Token::DIV: return "InlinedGenericOpStub_DIV";
  case Token::BIT_OR: return "InlinedGenericOpStub_BIT_OR";
  case Token::BIT_AND: return "InlinedGenericOpStub_BIT_AND";
  case Token::BIT_XOR: return "InlinedGenericOpStub_BIT_XOR";
  case Token::SAR: return "InlinedGenericOpStub_SAR";
  case Token::SHL: return "InlinedGenericOpStub_SHL";
  case Token::SHR: return "InlinedGenericOpStub_SHR";
  default:         return "InlinedGenericOpStub";
  }
}


void InlinedGenericOpStub::Generate(MacroAssembler* masm) {
  Label call_runtime;

  __ mov(eax, Operand(esp, 1 * kPointerSize));  // Get y.
  __ mov(edx, Operand(esp, 2 * kPointerSize));  // Get x.
  switch (op_) {
    case Token::ADD: {
      // eax: y.
      // edx: x.
      if (negate_result_) UNIMPLEMENTED();
      Label revert;
      __ mov(ecx, Operand(eax));
      __ or_(ecx, Operand(edx));  // ecx = x | y.
      __ add(eax, Operand(edx));  // Add y optimistically.
      // Go slow-path in case of overflow.
      __ j(overflow, &revert, not_taken);
      // Go slow-path in case of non-Smi operands.
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &revert, not_taken);
      __ ret(2 * kPointerSize);  // Remove all operands.

      // Revert optimistic add.
      __ bind(&revert);
      __ sub(eax, Operand(edx));
      break;
    }

    case Token::SUB: {
      // eax: y.
      // edx: x.
      if (negate_result_) UNIMPLEMENTED();
      Label revert;
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      __ sub(edx, Operand(eax));  // Subtract y optimistically.
      // Go slow-path in case of overflow.
      __ j(overflow, &revert, not_taken);
      // Go slow-path in case of non-Smi operands.
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &revert, not_taken);
      __ mov(eax, Operand(edx));
      __ ret(2 * kPointerSize);  // Remove all operands.

      // Revert optimistic sub.
      __ bind(&revert);
      __ add(edx, Operand(eax));
      break;
    }

    case Token::MUL: {
      // eax: y
      // edx: x
      // a) both operands SMI and result fits into a SMI -> return.
      // b) at least one of operans non-SMI -> non_smi_operands.
      // c) result does not fit in a SMI -> non_smi_result.
      Label non_smi_operands, non_smi_result;
      // Tag check.
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      ASSERT(kSmiTag == 0);  // Adjust code below.
      __ test(ecx, Immediate(kSmiTagMask));
      // Jump if not both Smi; check if float numbers.
      __ j(not_zero, &non_smi_operands, not_taken);

      // Get copies of operands.
      __ mov(ebx, Operand(eax));
      __ mov(ecx, Operand(edx));
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // adjust code below
      // Remove tag from one of the operands (but keep sign).
      __ sar(ecx, kSmiTagSize);
      // Do multiplication.
      __ imul(eax, Operand(ecx));  // Multiplication of Smis; result in eax.
      // Go slow on overflows.
      __ j(overflow, &non_smi_result, not_taken);
      // ...but operands OK for float arithmetic.

      if (negate_result_) {
        __ xor_(ecx, Operand(ecx));
        __ sub(ecx, Operand(eax));
        // Go slow on overflows.
        __ j(overflow, &non_smi_result, not_taken);
        __ mov(eax, Operand(ecx));
      }
      // If the result is +0 we may need to check if the result should
      // really be -0. Welcome to the -0 fan club.
      __ NegativeZeroTest(eax, ebx, edx, ecx, &non_smi_result);

      __ ret(2 * kPointerSize);

      __ bind(&non_smi_result);
      // TODO(1243132): Do not check float operands here.
      __ bind(&non_smi_operands);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }

    case Token::DIV: {
      // eax: y
      // edx: x
      if (negate_result_) UNIMPLEMENTED();
      Label non_smi_operands, non_smi_result, division_by_zero;
      __ mov(ebx, Operand(eax));  // Get y
      __ mov(eax, Operand(edx));  // Get x

      __ cdq();  // Sign extend eax into edx:eax.
      // Tag check.
      __ mov(ecx, Operand(ebx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      ASSERT(kSmiTag == 0);  // Adjust code below.
      __ test(ecx, Immediate(kSmiTagMask));
      // Jump if not both Smi; check if float numbers.
      __ j(not_zero, &non_smi_operands, not_taken);
      __ test(ebx, Operand(ebx));  // Check for 0 divisor.
      __ j(zero, &division_by_zero, not_taken);

      __ idiv(ebx);
      // Check for the corner case of dividing the most negative smi by -1.
      // (We cannot use the overflow flag, since it is not set by idiv.)
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      __ cmp(eax, 0x40000000);
      __ j(equal, &non_smi_result);
      // If the result is +0 we may need to check if the result should
      // really be -0. Welcome to the -0 fan club.
      __ NegativeZeroTest(eax, ecx, &non_smi_result);  // Use ecx = x | y.
      __ test(edx, Operand(edx));
      // Use floats if there's a remainder.
      __ j(not_zero, &non_smi_result, not_taken);
      __ shl(eax, kSmiTagSize);
      __ ret(2 * kPointerSize);  // Remove all operands.

      __ bind(&division_by_zero);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      __ jmp(&call_runtime);  // Division by zero must go through runtime.

      __ bind(&non_smi_result);
      // TODO(1243132): Do not check float operands here.
      __ bind(&non_smi_operands);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SAR:
    case Token::SHL:
    case Token::SHR: {
      // Smi-case for bitops should already have been inlined.
      break;
    }

    default: {
      UNREACHABLE();
    }
  }

  // eax: y
  // edx: x
  FloatingPointHelper::CheckFloatOperands(masm, &call_runtime, ebx);

  // Fast-case: Both operands are numbers.

  // Allocate a heap number, if needed.
  // Bitops allocate _after_ computation to allow for smi results.
  if (!Token::IsBitOp(op_)) {
    Label skip_allocation;
    switch (mode_) {
      case OVERWRITE_LEFT:
        __ mov(eax, Operand(edx));
        // Fall through!
      case OVERWRITE_RIGHT:
        // If the argument in eax is already an object, we skip the
        // allocation of a heap number.
        __ test(eax, Immediate(kSmiTagMask));
        __ j(not_zero, &skip_allocation, not_taken);
        // Fall through!
      case NO_OVERWRITE:
        FloatingPointHelper::AllocateHeapNumber(masm, &call_runtime, ecx, edx);
        __ bind(&skip_allocation);
        break;
      default: UNREACHABLE();
    }
  }

  FloatingPointHelper::LoadFloatOperands(masm, ecx);

  switch (op_) {
    case Token::ADD: {
      __ faddp(1);
      __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
      __ ret(2 * kPointerSize);
      break;
    }

    case Token::SUB: {
      __ fsubp(1);
      __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
      __ ret(2 * kPointerSize);
      break;
    }

    case Token::MUL: {
      __ fmulp(1);
      if (negate_result_) __ fchs();
      __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
      __ ret(2 * kPointerSize);
      break;
    }

    case Token::DIV: {
      __ fdivp(1);
      __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
      __ ret(2 * kPointerSize);
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SAR:
    case Token::SHL:
    case Token::SHR: {
      Label non_int32_operands, non_smi_result, skip_allocation;
      // Reserve space for converted numbers.
      __ sub(Operand(esp), Immediate(2 * kPointerSize));

      // Check if right operand is int32.
      __ fist_s(Operand(esp, 1 * kPointerSize));
      __ fild_s(Operand(esp, 1 * kPointerSize));
      __ fucompp();
      __ fnstsw_ax();
      __ sahf();
      __ j(not_zero, &non_int32_operands);
      __ j(parity_even, &non_int32_operands);

      // Check if left operand is int32.
      __ fist_s(Operand(esp, 0 * kPointerSize));
      __ fild_s(Operand(esp, 0 * kPointerSize));
      __ fucompp();
      __ fnstsw_ax();
      __ sahf();
      __ j(not_zero, &non_int32_operands);
      __ j(parity_even, &non_int32_operands);

      // Get int32 operands and perform bitop.
      __ pop(eax);
      __ pop(ecx);
      switch (op_) {
        case Token::BIT_OR:  __ or_(eax, Operand(ecx)); break;
        case Token::BIT_AND: __ and_(eax, Operand(ecx)); break;
        case Token::BIT_XOR: __ xor_(eax, Operand(ecx)); break;
        case Token::SAR: __ sar(eax); break;
        case Token::SHL: __ shl(eax); break;
        case Token::SHR: __ shr(eax); break;
        default: UNREACHABLE();
      }

      // Check if result is non-negative and fits in a smi.
      __ test(eax, Immediate(0xc0000000));
      __ j(not_zero, &non_smi_result);

      // Tag smi result and return.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(eax, times_2, kSmiTag));
      __ ret(2 * kPointerSize);

      // All ops except SHR return a signed int32 that we load in a HeapNumber.
      if (op_ != Token::SHR) {
        __ bind(&non_smi_result);
        // Allocate a heap number if needed.
        __ mov(ebx, Operand(eax));  // ebx: result
        switch (mode_) {
          case OVERWRITE_LEFT:
          case OVERWRITE_RIGHT:
            // If the operand was an object, we skip the
            // allocation of a heap number.
            __ mov(eax, Operand(esp, mode_ == OVERWRITE_RIGHT ?
                                1 * kPointerSize : 2 * kPointerSize));
            __ test(eax, Immediate(kSmiTagMask));
            __ j(not_zero, &skip_allocation, not_taken);
            // Fall through!
          case NO_OVERWRITE:
            FloatingPointHelper::AllocateHeapNumber(masm, &call_runtime,
                                                    ecx, edx);
            __ bind(&skip_allocation);
            break;
          default: UNREACHABLE();
        }
        // Store the result in the HeapNumber and return.
        __ mov(Operand(esp, 1 * kPointerSize), ebx);
        __ fild_s(Operand(esp, 1 * kPointerSize));
        __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
        __ ret(2 * kPointerSize);
      }
      __ bind(&non_int32_operands);
      // Restore stacks and operands before calling runtime.
      __ ffree(0);
      __ add(Operand(esp), Immediate(2 * kPointerSize));

      // SHR should return uint32 - go to runtime for non-smi/negative result.
      if (op_ == Token::SHR) __ bind(&non_smi_result);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }

    default: UNREACHABLE(); break;
  }

  // Slow-case: Use the runtime system to get the right result.
  __ bind(&call_runtime);
  if (negate_result_) {
    switch (op_) {
      case Token::MUL:
        __ InvokeBuiltin(Builtins::MULNEG, JUMP_FUNCTION);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    switch (op_) {
      case Token::ADD:
        __ InvokeBuiltin(Builtins::ADD, JUMP_FUNCTION);
        break;
      case Token::SUB:
        __ InvokeBuiltin(Builtins::SUB, JUMP_FUNCTION);
        break;
      case Token::MUL:
        __ InvokeBuiltin(Builtins::MUL, JUMP_FUNCTION);
        break;
      case Token::DIV:
        __ InvokeBuiltin(Builtins::DIV, JUMP_FUNCTION);
        break;
      case Token::BIT_OR:
        __ InvokeBuiltin(Builtins::BIT_OR, JUMP_FUNCTION);
        break;
      case Token::BIT_AND:
        __ InvokeBuiltin(Builtins::BIT_AND, JUMP_FUNCTION);
        break;
      case Token::BIT_XOR:
        __ InvokeBuiltin(Builtins::BIT_XOR, JUMP_FUNCTION);
        break;
      case Token::SAR:
        __ InvokeBuiltin(Builtins::SAR, JUMP_FUNCTION);
        break;
      case Token::SHL:
        __ InvokeBuiltin(Builtins::SHL, JUMP_FUNCTION);
        break;
      case Token::SHR:
        __ InvokeBuiltin(Builtins::SHR, JUMP_FUNCTION);
        break;

      default:
        UNREACHABLE();
    }
  }
}


void FloatingPointHelper::AllocateHeapNumber(MacroAssembler* masm,
                                             Label* need_gc,
                                             Register scratch1,
                                             Register scratch2) {
  ExternalReference allocation_top =
      ExternalReference::new_space_allocation_top_address();
  ExternalReference allocation_limit =
      ExternalReference::new_space_allocation_limit_address();
  __ mov(Operand(scratch1), Immediate(allocation_top));
  __ mov(eax, Operand(scratch1, 0));
  __ lea(scratch2, Operand(eax, HeapNumber::kSize));  // scratch2: new top
  __ cmp(scratch2, Operand::StaticVariable(allocation_limit));
  __ j(above, need_gc, not_taken);

  __ mov(Operand(scratch1, 0), scratch2);  // store new top
  __ mov(Operand(eax, HeapObject::kMapOffset),
         Immediate(Factory::heap_number_map()));
  // Tag old top and use as result.
  __ add(Operand(eax), Immediate(kHeapObjectTag));
}


void FloatingPointHelper::LoadFloatOperands(MacroAssembler* masm,
                                            Register scratch) {
  Label load_smi_1, load_smi_2, done_load_1, done;
  __ mov(scratch, Operand(esp, 2 * kPointerSize));
  __ test(scratch, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_1, not_taken);
  __ fld_d(FieldOperand(scratch, HeapNumber::kValueOffset));
  __ bind(&done_load_1);

  __ mov(scratch, Operand(esp, 1 * kPointerSize));
  __ test(scratch, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_2, not_taken);
  __ fld_d(FieldOperand(scratch, HeapNumber::kValueOffset));
  __ jmp(&done);

  __ bind(&load_smi_1);
  __ sar(scratch, kSmiTagSize);
  __ push(scratch);
  __ fild_s(Operand(esp, 0));
  __ pop(scratch);
  __ jmp(&done_load_1);

  __ bind(&load_smi_2);
  __ sar(scratch, kSmiTagSize);
  __ push(scratch);
  __ fild_s(Operand(esp, 0));
  __ pop(scratch);

  __ bind(&done);
}


void FloatingPointHelper::CheckFloatOperands(MacroAssembler* masm,
                                             Label* non_float,
                                             Register scratch) {
  Label test_other, done;
  // test if both operands are floats or Smi -> scratch=k_is_float;
  // otherwise scratch=k_not_float
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &test_other, not_taken);  // argument in edx is OK
  __ mov(scratch, FieldOperand(edx, HeapObject::kMapOffset));
  __ cmp(scratch, Factory::heap_number_map());
  __ j(not_equal, non_float);  // argument in edx is not a number -> NaN

  __ bind(&test_other);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &done);  // argument in eax is OK
  __ mov(scratch, FieldOperand(eax, HeapObject::kMapOffset));
  __ cmp(scratch, Factory::heap_number_map());
  __ j(not_equal, non_float);  // argument in eax is not a number -> NaN

  // Fall-through: Both operands are numbers.
  __ bind(&done);
}


#undef __
#define __  masm->


void UnarySubStub::Generate(MacroAssembler* masm) {
  Label undo;
  Label slow;
  Label done;

  // Enter runtime system if the value is not a smi.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(not_zero, &slow, not_taken);

  // Enter runtime system if the value of the expression is zero
  // to make sure that we switch between 0 and -0.
  __ test(eax, Operand(eax));
  __ j(zero, &slow, not_taken);

  // The value of the expression is a smi that is not zero.  Try
  // optimistic subtraction '0 - value'.
  __ mov(edx, Operand(eax));
  __ Set(eax, Immediate(0));
  __ sub(eax, Operand(edx));
  __ j(overflow, &undo, not_taken);

  // If result is a smi we are done.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &done, taken);

  // Undo optimistic sub and enter runtime system.
  __ bind(&undo);
  __ mov(eax, Operand(edx));

  // Enter runtime system.
  __ bind(&slow);
  __ pop(ecx);  // pop return address
  __ push(eax);
  __ push(ecx);  // push return address
  __ InvokeBuiltin(Builtins::UNARY_MINUS, JUMP_FUNCTION);

  __ bind(&done);

  masm->StubReturn(1);
}


// TODO(1217800): Implement MOD like ADD/SUB/MUL/DIV
// and get rid of GenericOpStub.
void GenericOpStub::Generate(MacroAssembler* masm) {
  switch (op_) {
    case Token::MOD: {
      Label fast, slow;
      __ mov(ebx, Operand(eax));  // get y
      __ mov(eax, Operand(esp, 1 * kPointerSize));  // get x
      __ cdq();  // sign extend eax into edx:eax
      // tag check
      __ mov(ecx, Operand(ebx));
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, not_taken);
      __ test(ebx, Operand(ebx));  // test for y == 0
      __ j(not_zero, &fast, taken);

      // Slow case: Call native operator implementation.
      __ bind(&slow);
      __ pop(ecx);  // pop return address
      __ push(ebx);
      __ push(ecx);  // push return address
      __ InvokeBuiltin(Builtins::MOD, JUMP_FUNCTION);

      // Fast case: Do integer division and use remainder.
      __ bind(&fast);
      __ idiv(ebx);
      __ NegativeZeroTest(edx, ecx, &slow);  // use ecx = x | y
      __ mov(eax, Operand(edx));
      break;
    }

    default: UNREACHABLE();
  }
  masm->StubReturn(2);
}


class ArgumentsAccessStub: public CodeStub {
 public:
  explicit ArgumentsAccessStub(bool is_length) : is_length_(is_length) { }

 private:
  bool is_length_;

  Major MajorKey() { return ArgumentsAccess; }
  int MinorKey() { return is_length_ ? 1 : 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "ArgumentsAccessStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("ArgumentsAccessStub (is_length %s)\n",
           is_length_ ? "true" : "false");
  }
#endif
};


void ArgumentsAccessStub::Generate(MacroAssembler* masm) {
  // Check that the key is a smi for non-length access.
  Label slow;
  if (!is_length_) {
    __ mov(ebx, Operand(esp, 1 * kPointerSize));  // skip return address
    __ test(ebx, Immediate(kSmiTagMask));
    __ j(not_zero, &slow, not_taken);
  }

  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ mov(edx, Operand(ebp, StandardFrameConstants::kCallerFPOffset));
  __ mov(ecx, Operand(edx, StandardFrameConstants::kContextOffset));
  __ cmp(ecx, ArgumentsAdaptorFrame::SENTINEL);
  __ j(equal, &adaptor);

  // The displacement is used for skipping the return address on the
  // stack. It is the offset of the last parameter (if any) relative
  // to the frame pointer.
  static const int kDisplacement = 1 * kPointerSize;
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);  // shifting code depends on this

  if (is_length_) {
    // Do nothing. The length is already in register eax.
  } else {
    // Check index against formal parameters count limit passed in
    // through register eax. Use unsigned comparison to get negative
    // check for free.
    __ cmp(ebx, Operand(eax));
    __ j(above_equal, &slow, not_taken);

    // Read the argument from the stack.
    __ lea(edx, Operand(ebp, eax, times_2, 0));
    __ neg(ebx);
    __ mov(eax, Operand(edx, ebx, times_2, kDisplacement));
  }

  // Return the length or the argument.
  __ ret(0);

  // Arguments adaptor case: Find the length or the actual argument in
  // the calling frame.
  __ bind(&adaptor);
  if (is_length_) {
    // Read the arguments length from the adaptor frame.
    __ mov(eax, Operand(edx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  } else {
    // Check index against actual arguments limit found in the
    // arguments adaptor frame. Use unsigned comparison to get
    // negative check for free.
    __ mov(ecx, Operand(edx, ArgumentsAdaptorFrameConstants::kLengthOffset));
    __ cmp(ebx, Operand(ecx));
    __ j(above_equal, &slow, not_taken);

    // Read the argument from the stack.
    __ lea(edx, Operand(edx, ecx, times_2, 0));
    __ neg(ebx);
    __ mov(eax, Operand(edx, ebx, times_2, kDisplacement));
  }

  // Return the length or the argument.
  __ ret(0);

  // Slow-case: Handle non-smi or out-of-bounds access to arguments
  // by calling the runtime system.
  if (!is_length_) {
    __ bind(&slow);
    __ Set(eax, Immediate(0));  // not counting receiver
    __ JumpToBuiltin(ExternalReference(Runtime::kGetArgumentsProperty));
  }
}


#undef __
#define __  masm_->


// Return true if code was generated for operation 'type'.
// NOTE: The code below assumes that the slow cases (calls to runtime)
// never return a constant/immutable object.
// TODO(1217800): MOD is not yet implemented.
bool Ia32CodeGenerator::InlinedGenericOperation(
    Token::Value op,
    const OverwriteMode overwrite_mode,
    bool negate_result) {
  const char* comment = NULL;
  if (negate_result) {
    switch (op) {
      case Token::ADD: comment = "[ GenericOpCode Token::ADDNEG"; break;
      case Token::SUB: comment = "[ GenericOpCode Token::SUBNEG"; break;
      case Token::MUL: comment = "[ GenericOpCode Token::MULNEG"; break;
      case Token::DIV: comment = "[ GenericOpCode Token::DIVNEG"; break;
      default: return false;
    }
  } else {
    switch (op) {
      case Token::ADD: comment = "[ GenericOpCode Token::ADD"; break;
      case Token::SUB: comment = "[ GenericOpCode Token::SUB"; break;
      case Token::MUL: comment = "[ GenericOpCode Token::MUL"; break;
      case Token::DIV: comment = "[ GenericOpCode Token::DIV"; break;
      default: return false;
    }
  }
  Comment cmnt(masm_, comment);
  InlinedGenericOpStub stub(op, overwrite_mode, negate_result);
  __ CallStub(&stub);
  __ push(eax);
  return true;
}


void Ia32CodeGenerator::GenericOperation(Token::Value op,
                                         OverwriteMode overwrite_mode) {
  // Stub is entered with a call: 'return address' is on stack.
  switch (op) {
    case Token::MOD: {
      GenericOpStub stub(op);
      __ pop(eax);
      __ CallStub(&stub);
      __ push(eax);
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR: {
      Label slow, exit;
      __ pop(eax);  // get y
      __ pop(edx);  // get x
      __ mov(ecx, Operand(edx));  // prepare smi check
      // tag check
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, taken);
      switch (op) {
        case Token::BIT_OR:  __ or_(eax, Operand(edx)); break;
        case Token::BIT_AND: __ and_(eax, Operand(edx)); break;
        case Token::BIT_XOR: __ xor_(eax, Operand(edx)); break;
        default: UNREACHABLE();
      }
      __ jmp(&exit);
      __ bind(&slow);
      __ push(edx);  // restore stack slots
      __ push(eax);
      InlinedGenericOpStub stub(op, overwrite_mode, false);
      __ CallStub(&stub);
      __ bind(&exit);
      __ push(eax);  // push the result to the stack
      break;
    }

    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      Label slow, exit;
      __ pop(edx);  // get y
      __ pop(eax);  // get x
      // tag check
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, not_taken);
      // get copies of operands
      __ mov(ebx, Operand(eax));
      __ mov(ecx, Operand(edx));
      // remove tags from operands (but keep sign)
      __ sar(ebx, kSmiTagSize);
      __ sar(ecx, kSmiTagSize);
      // perform operation
      switch (op) {
        case Token::SAR:
          __ sar(ebx);
          // no checks of result necessary
          break;

        case Token::SHR:
          __ shr(ebx);
          // check that the *unsigned* result fits in a smi
          // neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging
          // - 0x40000000: this number would convert to negative when
          // smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi
          __ test(ebx, Immediate(0xc0000000));
          __ j(not_zero, &slow, not_taken);
          break;

        case Token::SHL:
          __ shl(ebx);
          // check that the *signed* result fits in a smi
          __ lea(ecx, Operand(ebx, 0x40000000));
          __ test(ecx, Immediate(0x80000000));
          __ j(not_zero, &slow, not_taken);
          break;

        default: UNREACHABLE();
      }
      // tag result and store it in TOS (eax)
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(ebx, times_2, kSmiTag));
      __ jmp(&exit);
      // slow case
      __ bind(&slow);
      __ push(eax);  // restore stack
      __ push(edx);
        InlinedGenericOpStub stub(op, overwrite_mode, false);
      __ CallStub(&stub);
      __ bind(&exit);
      __ push(eax);
      break;
    }

    case Token::COMMA: {
      // simply discard left value
      __ pop(eax);
      __ add(Operand(esp), Immediate(kPointerSize));
      __ push(eax);
      break;
    }

    default:
      // Other cases should have been handled before this point.
      UNREACHABLE();
      break;
  }
}


class DeferredInlinedSmiOperation: public DeferredCode {
 public:
  DeferredInlinedSmiOperation(CodeGenerator* generator,
                              Token::Value op, int value,
                              OverwriteMode overwrite_mode) :
      DeferredCode(generator), op_(op), value_(value),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperation");
  }
  virtual void Generate() {
    __ push(eax);
    __ push(Immediate(Smi::FromInt(value_)));
    InlinedGenericOpStub igostub(op_, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  Token::Value op_;
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiOperationReversed: public DeferredCode {
 public:
  DeferredInlinedSmiOperationReversed(CodeGenerator* generator,
                                      Token::Value op, int value,
                                      OverwriteMode overwrite_mode) :
      DeferredCode(generator), op_(op), value_(value),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperationReversed");
  }
  virtual void Generate() {
    __ push(Immediate(Smi::FromInt(value_)));
    __ push(eax);
    InlinedGenericOpStub igostub(op_, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  Token::Value op_;
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiAdd: public DeferredCode {
 public:
  DeferredInlinedSmiAdd(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAdd");
  }

  virtual void Generate() {
    // Undo the optimistic add operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ sub(Operand(eax), immediate);
    __ push(eax);
    __ push(immediate);
    InlinedGenericOpStub igostub(Token::ADD, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiAddReversed: public DeferredCode {
 public:
  DeferredInlinedSmiAddReversed(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAddReversed");
  }

  virtual void Generate() {
    // Undo the optimistic add operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ sub(Operand(eax), immediate);
    __ push(immediate);
    __ push(eax);
    InlinedGenericOpStub igostub(Token::ADD, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiSub: public DeferredCode {
 public:
  DeferredInlinedSmiSub(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSub");
  }

  virtual void Generate() {
    // Undo the optimistic sub operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ add(Operand(eax), immediate);
    __ push(eax);
    __ push(immediate);
    InlinedGenericOpStub igostub(Token::SUB, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiSubReversed: public DeferredCode {
 public:
  // tos_reg is used to save the TOS value before reversing the operands
  // eax will contain the immediate value after undoing the optimistic sub.
  DeferredInlinedSmiSubReversed(CodeGenerator* generator, Register tos_reg,
                                OverwriteMode overwrite_mode) :
      DeferredCode(generator), tos_reg_(tos_reg),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSubReversed");
  }

  virtual void Generate() {
    // Undo the optimistic sub operation and call the shared stub.
    __ add(eax, Operand(tos_reg_));
    __ push(eax);
    __ push(Operand(tos_reg_));
    InlinedGenericOpStub igostub(Token::SUB, overwrite_mode_, false);
    __ CallStub(&igostub);
  }

 private:
  Register tos_reg_;
  OverwriteMode overwrite_mode_;
};


void Ia32CodeGenerator::SmiOperation(Token::Value op,
                                     Handle<Object> value,
                                     bool reversed,
                                     OverwriteMode overwrite_mode) {
  // NOTE: This is an attempt to inline (a bit) more of the code for
  // some possible smi operations (like + and -) when (at least) one
  // of the operands is a literal smi. With this optimization, the
  // performance of the system is increased by ~15%, and the generated
  // code size is increased by ~1% (measured on a combination of
  // different benchmarks).

  // TODO(1217802): Optimize some special cases of operations
  // involving a smi literal (multiply by 2, shift by 0, etc.).

  // Get the literal value.
  int int_value = Smi::cast(*value)->value();

  switch (op) {
    case Token::ADD: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred = new DeferredInlinedSmiAdd(this, int_value, overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiAddReversed(this, int_value,
                                                     overwrite_mode);
      }
      __ pop(eax);
      __ add(Operand(eax), Immediate(value));
      __ j(overflow, deferred->enter(), not_taken);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    case Token::SUB: {
      DeferredCode* deferred = NULL;
      __ pop(eax);
      if (!reversed) {
        deferred = new DeferredInlinedSmiSub(this, int_value, overwrite_mode);
        __ sub(Operand(eax), Immediate(value));
      } else {
        deferred = new DeferredInlinedSmiSubReversed(this, edx, overwrite_mode);
        __ mov(edx, Operand(eax));
        __ mov(Operand(eax), Immediate(value));
        __ sub(eax, Operand(edx));
      }
      __ j(overflow, deferred->enter(), not_taken);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    case Token::SAR: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericOperation(op);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
          new DeferredInlinedSmiOperation(this, Token::SAR, shift_value,
                                          overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(eax, shift_value);
        __ and_(eax, ~kSmiTagMask);
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::SHR: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericOperation(op);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHR, shift_value,
                                        overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ mov(ebx, Operand(eax));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(ebx, kSmiTagSize);
        __ shr(ebx, shift_value);
        __ test(ebx, Immediate(0xc0000000));
        __ j(not_zero, deferred->enter(), not_taken);
        // tag result and store it in TOS (eax)
        ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
        __ lea(eax, Operand(ebx, times_2, kSmiTag));
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::SHL: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericOperation(op);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHL, shift_value,
                                        overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ mov(ebx, Operand(eax));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(ebx, kSmiTagSize);
        __ shl(ebx, shift_value);
        __ lea(ecx, Operand(ebx, 0x40000000));
        __ test(ecx, Immediate(0x80000000));
        __ j(not_zero, deferred->enter(), not_taken);
        // tag result and store it in TOS (eax)
        ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
        __ lea(eax, Operand(ebx, times_2, kSmiTag));
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_XOR:
    case Token::BIT_AND: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred =  new DeferredInlinedSmiOperation(this, op, int_value,
                                                    overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiOperationReversed(this, op, int_value,
                                                           overwrite_mode);
      }
      __ pop(eax);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      if (op == Token::BIT_AND) {
        __ and_(Operand(eax), Immediate(value));
      } else if (op == Token::BIT_XOR) {
        __ xor_(Operand(eax), Immediate(value));
      } else {
        ASSERT(op == Token::BIT_OR);
        __ or_(Operand(eax), Immediate(value));
      }
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    default: {
      if (!reversed) {
        __ push(Immediate(value));
      } else {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
      }
      bool done = InlinedGenericOperation(op, overwrite_mode,
                                          false /*negate_result*/);
      if (!done) GenericOperation(op);
      break;
    }
  }
}


#undef __
#define __  masm->

class CompareStub: public CodeStub {
 public:
  CompareStub(Condition cc, bool strict) : cc_(cc), strict_(strict) { }

  void Generate(MacroAssembler* masm);

 private:
  Condition cc_;
  bool strict_;

  Major MajorKey() { return Compare; }

  int MinorKey() {
    // Encode the three parameters in a unique 16 bit value.
    ASSERT(static_cast<int>(cc_) < (1 << 15));
    return (static_cast<int>(cc_) << 1) | (strict_ ? 1 : 0);
  }

  const char* GetName() { return "CompareStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("CompareStub (cc %d), (strict %s)\n",
           static_cast<int>(cc_),
           strict_ ? "true" : "false");
  }
#endif
};


void CompareStub::Generate(MacroAssembler* masm) {
  Label call_builtin, done;
  // Save the return address (and get it off the stack).
  __ pop(ecx);

  // Push arguments.
  __ push(eax);
  __ push(edx);
  __ push(ecx);

  // Inlined floating point compare.
  // Call builtin if operands are not floating point or SMI.
  FloatingPointHelper::CheckFloatOperands(masm, &call_builtin, ebx);
  FloatingPointHelper::LoadFloatOperands(masm, ecx);
  __ FCmp();

  // Jump to builtin for NaN.
  __ j(parity_even, &call_builtin, not_taken);

  // TODO(1243847): Use cmov below once CpuFeatures are properly hooked up.
  Label below_lbl, above_lbl;
  // use edx, eax to convert unsigned to signed comparision
  __ j(below, &below_lbl, not_taken);
  __ j(above, &above_lbl, not_taken);

  __ xor_(eax, Operand(eax));  // equal
  __ ret(2 * kPointerSize);

  __ bind(&below_lbl);
  __ mov(eax, -1);
  __ ret(2 * kPointerSize);

  __ bind(&above_lbl);
  __ mov(eax, 1);
  __ ret(2 * kPointerSize);  // eax, edx were pushed

  __ bind(&call_builtin);
  // must swap argument order
  __ pop(ecx);
  __ pop(edx);
  __ pop(eax);
  __ push(edx);
  __ push(eax);

  // Figure out which native to call and setup the arguments.
  Builtins::JavaScript builtin;
  if (cc_ == equal) {
    builtin = strict_ ? Builtins::STRICT_EQUALS : Builtins::EQUALS;
  } else {
    builtin = Builtins::COMPARE;
    int ncr;  // NaN compare result
    if (cc_ == less || cc_ == less_equal) {
      ncr = GREATER;
    } else {
      ASSERT(cc_ == greater || cc_ == greater_equal);  // remaining cases
      ncr = LESS;
    }
    __ push(Immediate(Smi::FromInt(ncr)));
  }

  // Restore return address on the stack.
  __ push(ecx);

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ InvokeBuiltin(builtin, JUMP_FUNCTION);
}


void StackCheckStub::Generate(MacroAssembler* masm) {
  // Because builtins always remove the receiver from the stack, we
  // have to fake one to avoid underflowing the stack. The receiver
  // must be inserted below the return address on the stack so we
  // temporarily store that in a register.
  __ pop(eax);
  __ push(Immediate(Smi::FromInt(0)));
  __ push(eax);

  // Do tail-call to runtime routine.
  __ Set(eax, Immediate(0));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(Runtime::kStackGuard));
}


#undef __
#define __  masm_->


class ComparisonDeferred: public DeferredCode {
 public:
  ComparisonDeferred(CodeGenerator* generator, Condition cc, bool strict) :
      DeferredCode(generator), cc_(cc), strict_(strict) {
    set_comment("[ ComparisonDeferred");
  }
  virtual void Generate();

 private:
  Condition cc_;
  bool strict_;
};


void ComparisonDeferred::Generate() {
  CompareStub stub(cc_, strict_);
  // "parameters" setup by calling code in edx and eax
  __ CallStub(&stub);
  __ cmp(eax, 0);
  // "result" is returned in the flags
}


void Ia32CodeGenerator::Comparison(Condition cc, bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  ComparisonDeferred* deferred = new ComparisonDeferred(this, cc, strict);
  __ pop(eax);
  __ pop(edx);
  __ mov(ecx, Operand(eax));
  __ or_(ecx, Operand(edx));
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(not_zero, deferred->enter(), not_taken);
  // Test smi equality by pointer comparison.
  __ cmp(edx, Operand(eax));
  __ bind(deferred->exit());
  cc_reg_ = cc;
}


class SmiComparisonDeferred: public DeferredCode {
 public:
  SmiComparisonDeferred(CodeGenerator* generator,
                        Condition cc,
                        bool strict,
                        int value)
      : DeferredCode(generator), cc_(cc), strict_(strict), value_(value) {
    set_comment("[ ComparisonDeferred");
  }
  virtual void Generate();

 private:
  Condition cc_;
  bool strict_;
  int value_;
};


void SmiComparisonDeferred::Generate() {
  CompareStub stub(cc_, strict_);
  // Setup parameters and call stub.
  __ mov(edx, Operand(eax));
  __ mov(Operand(eax), Immediate(Smi::FromInt(value_)));
  __ CallStub(&stub);
  __ cmp(eax, 0);
  // "result" is returned in the flags
}


void Ia32CodeGenerator::SmiComparison(Condition cc,
                                      Handle<Object> value,
                                      bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  SmiComparisonDeferred* deferred =
      new SmiComparisonDeferred(this, cc, strict, Smi::cast(*value)->value());
  __ pop(eax);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(not_zero, deferred->enter(), not_taken);
  // Test smi equality by pointer comparison.
  __ cmp(Operand(eax), Immediate(value));
  __ bind(deferred->exit());
  cc_reg_ = cc;
}


class CallFunctionStub: public CodeStub {
 public:
  explicit CallFunctionStub(int argc) : argc_(argc) { }

  void Generate(MacroAssembler* masm);

 private:
  int argc_;

  const char* GetName() { return "CallFunctionStub"; }

#ifdef DEBUG
  void Print() { PrintF("CallFunctionStub (args %d)\n", argc_); }
#endif

  Major MajorKey() { return CallFunction; }
  int MinorKey() { return argc_; }
};


void CallFunctionStub::Generate(MacroAssembler* masm) {
  Label slow, fast;

  // Get the function to call from the stack.
  // +2 ~ receiver, return address
  masm->mov(edi, Operand(esp, (argc_ + 2) * kPointerSize));

  // Check that the function really is a JavaScript function.
  masm->test(edi, Immediate(kSmiTagMask));
  masm->j(zero, &slow, not_taken);
  // Get the map.
  masm->mov(ecx, FieldOperand(edi, HeapObject::kMapOffset));
  masm->movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  masm->cmp(ecx, JS_FUNCTION_TYPE);
  masm->j(not_equal, &slow, not_taken);

  // Fast-case: Just invoke the function.
  ParameterCount actual(argc_);
  masm->InvokeFunction(edi, actual, JUMP_FUNCTION);

  // Slow-case: Non-function called.
  masm->bind(&slow);
  masm->Set(eax, Immediate(argc_));
  masm->Set(ebx, Immediate(0));
  masm->GetBuiltinEntry(edx, Builtins::CALL_NON_FUNCTION);
  Handle<Code> adaptor(Builtins::builtin(Builtins::ArgumentsAdaptorTrampoline));
  masm->jmp(adaptor, code_target);
}


// Call the function just below TOS on the stack with the given
// arguments. The receiver is the TOS.
void Ia32CodeGenerator::CallWithArguments(ZoneList<Expression*>* args,
                                          int position) {
  // Push the arguments ("left-to-right") on the stack.
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  // Record the position for debugging purposes.
  __ RecordPosition(position);

  // Use the shared code stub to call the function.
  CallFunctionStub call_function(args->length());
  __ CallStub(&call_function);

  // Restore context and pop function from the stack.
  __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  __ mov(TOS, eax);
}


void Ia32CodeGenerator::Branch(bool if_true, Label* L) {
  ASSERT(has_cc());
  Condition cc = if_true ? cc_reg_ : NegateCondition(cc_reg_);
  __ j(cc, L);
  cc_reg_ = no_condition;
}


class StackCheckDeferred: public DeferredCode {
 public:
  explicit StackCheckDeferred(CodeGenerator* generator)
      : DeferredCode(generator) {
    set_comment("[ StackCheckDeferred");
  }
  virtual void Generate();
};


void StackCheckDeferred::Generate() {
  StackCheckStub stub;
  __ CallStub(&stub);
}


void Ia32CodeGenerator::CheckStack() {
  if (FLAG_check_stack) {
    StackCheckDeferred* deferred = new StackCheckDeferred(this);
    ExternalReference stack_guard_limit =
        ExternalReference::address_of_stack_guard_limit();
    __ cmp(esp, Operand::StaticVariable(stack_guard_limit));
    __ j(below, deferred->enter(), not_taken);
    __ bind(deferred->exit());
  }
}


void Ia32CodeGenerator::VisitBlock(Block* node) {
  Comment cmnt(masm_, "[ Block");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  VisitStatements(node->statements());
  __ bind(node->break_target());
}


void Ia32CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  __ push(Immediate(pairs));
  __ push(Operand(esi));
  __ push(Immediate(Smi::FromInt(is_eval() ? 1 : 0)));
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void Ia32CodeGenerator::VisitDeclaration(Declaration* node) {
  Comment cmnt(masm_, "[ Declaration");
  Variable* var = node->proxy()->var();
  ASSERT(var != NULL);  // must have been resolved
  Slot* slot = var->slot();

  // If it was not possible to allocate the variable at compile time,
  // we need to "declare" it at runtime to make sure it actually
  // exists in the local context.
  if (slot != NULL && slot->type() == Slot::LOOKUP) {
    // Variables with a "LOOKUP" slot were introduced as non-locals
    // during variable resolution and must have mode DYNAMIC.
    ASSERT(var->mode() == Variable::DYNAMIC);
    // For now, just do a runtime call.
    __ push(Operand(esi));
    __ push(Immediate(var->name()));
    // Declaration nodes are always introduced in one of two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    __ push(Immediate(Smi::FromInt(attr)));
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      __ push(Immediate(Factory::the_hole_value()));
    } else if (node->fun() != NULL) {
      Load(node->fun());
    } else {
      __ push(Immediate(0));  // no initial value!
    }
    __ CallRuntime(Runtime::kDeclareContextSlot, 5);
    // DeclareContextSlot pops the assigned value by accepting an
    // extra argument and returning the TOS; no need to explicitly
    // pop here.
    __ push(eax);
    return;
  }

  ASSERT(!var->is_global());

  // If we have a function or a constant, we need to initialize the variable.
  Expression* val = NULL;
  if (node->mode() == Variable::CONST) {
    val = new Literal(Factory::the_hole_value());
  } else {
    val = node->fun();  // NULL if we don't have a function
  }

  if (val != NULL) {
    // Set initial value.
    Reference target(this, node->proxy());
    Load(val);
    SetValue(&target);
    // Get rid of the assigned value (declarations are statements).
    __ pop(eax);  // Pop(no_reg);
  }
}


void Ia32CodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  Comment cmnt(masm_, "[ ExpressionStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Expression* expression = node->expression();
  expression->MarkAsStatement();
  Load(expression);
  __ pop(eax);  // remove the lingering expression result from the top of stack
}


void Ia32CodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  Comment cmnt(masm_, "// EmptyStatement");
  // nothing to do
}


void Ia32CodeGenerator::VisitIfStatement(IfStatement* node) {
  Comment cmnt(masm_, "[ IfStatement");
  // Generate different code depending on which
  // parts of the if statement are present or not.
  bool has_then_stm = node->HasThenStatement();
  bool has_else_stm = node->HasElseStatement();

  if (FLAG_debug_info) RecordStatementPosition(node);
  Label exit;
  if (has_then_stm && has_else_stm) {
    Label then;
    Label else_;
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &then, &else_, true);
    Branch(false, &else_);
    // then
    __ bind(&then);
    Visit(node->then_statement());
    __ jmp(&exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else if (has_then_stm) {
    ASSERT(!has_else_stm);
    Label then;
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &then, &exit, true);
    Branch(false, &exit);
    // then
    __ bind(&then);
    Visit(node->then_statement());

  } else if (has_else_stm) {
    ASSERT(!has_then_stm);
    Label else_;
    // if (!cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &exit, &else_, true);
    Branch(true, &exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else {
    ASSERT(!has_then_stm && !has_else_stm);
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &exit, &exit, false);
    if (has_cc()) {
      cc_reg_ = no_condition;
    } else {
      // No cc value set up, that means the boolean was pushed.
      // Pop it again, since it is not going to be used.
      __ pop(eax);
    }
  }

  // end
  __ bind(&exit);
}


void Ia32CodeGenerator::CleanStack(int num_bytes) {
  ASSERT(num_bytes >= 0);
  if (num_bytes > 0) {
    __ add(Operand(esp), Immediate(num_bytes));
  }
}


void Ia32CodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  Comment cmnt(masm_, "[ ContinueStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ jmp(node->target()->continue_target());
}


void Ia32CodeGenerator::VisitBreakStatement(BreakStatement* node) {
  Comment cmnt(masm_, "[ BreakStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ jmp(node->target()->break_target());
}


void Ia32CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  Comment cmnt(masm_, "[ ReturnStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());

  // Move the function result into eax
  __ pop(eax);

  // If we're inside a try statement or the return instruction
  // sequence has been generated, we just jump to that
  // point. Otherwise, we generate the return instruction sequence and
  // bind the function return label.
  if (is_inside_try_ || function_return_.is_bound()) {
    __ jmp(&function_return_);
  } else {
    __ bind(&function_return_);
    if (FLAG_trace) {
      __ push(eax);  // undo the pop(eax) from above
      __ CallRuntime(Runtime::kTraceExit, 1);
    }

    // Add a label for checking the size of the code used for returning.
    Label check_exit_codesize;
    __ bind(&check_exit_codesize);

    // Leave the frame and return popping the arguments and the
    // receiver.
    ExitJSFrame();
    __ ret((scope_->num_parameters() + 1) * kPointerSize);

    // Check that the size of the code used for returning matches what is
    // expected by the debugger.
    ASSERT_EQ(Debug::kIa32JSReturnSequenceLength,
              __ SizeOfCodeGeneratedSince(&check_exit_codesize));
  }
}


void Ia32CodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  Comment cmnt(masm_, "[ WithEnterStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());
  __ CallRuntime(Runtime::kPushContext, 2);
  __ push(eax);
  // Update context local.
  __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
}


void Ia32CodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  Comment cmnt(masm_, "[ WithExitStatement");
  // Pop context.
  __ mov(esi, ContextOperand(esi, Context::PREVIOUS_INDEX));
  // Update context local.
  __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
}


void Ia32CodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  Comment cmnt(masm_, "[ SwitchStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);

  Load(node->tag());

  Label next, fall_through, default_case;
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  for (int i = 0; i < length; i++) {
    CaseClause* clause = cases->at(i);

    Comment cmnt(masm_, "[ case clause");

    if (clause->is_default()) {
      // Bind the default case label, so we can branch to it when we
      // have compared against all other cases.
      ASSERT(default_case.is_unused());  // at most one default clause

      // If the default case is the first (but not only) case, we have
      // to jump past it for now. Once we're done with the remaining
      // clauses, we'll branch back here. If it isn't the first case,
      // we jump past it by avoiding to chain it into the next chain.
      if (length > 1) {
        if (i == 0) __ jmp(&next);
        __ bind(&default_case);
      }

    } else {
      __ bind(&next);
      next.Unuse();
      __ mov(eax, TOS);
      __ push(eax);  // duplicate TOS
      Load(clause->label());
      Comparison(equal, true);
      Branch(false, &next);
      // Entering the case statement -> remove the switch value from the stack
      __ pop(eax);
    }

    // Generate code for the body.
    __ bind(&fall_through);
    fall_through.Unuse();
    VisitStatements(clause->statements());
    __ jmp(&fall_through);
  }

  __ bind(&next);
  // Reached the end of the case statements -> remove the switch value
  // from the stack
  __ pop(eax);  // Pop(no_reg)
  if (default_case.is_bound()) __ jmp(&default_case);

  __ bind(&fall_through);
  __ bind(node->break_target());
}


void Ia32CodeGenerator::VisitLoopStatement(LoopStatement* node) {
  Comment cmnt(masm_, "[ LoopStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);

  // simple condition analysis
  enum { ALWAYS_TRUE, ALWAYS_FALSE, DONT_KNOW } info = DONT_KNOW;
  if (node->cond() == NULL) {
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    info = ALWAYS_TRUE;
  } else {
    Literal* lit = node->cond()->AsLiteral();
    if (lit != NULL) {
      if (lit->IsTrue()) {
        info = ALWAYS_TRUE;
      } else if (lit->IsFalse()) {
        info = ALWAYS_FALSE;
      }
    }
  }

  Label loop, entry;

  // init
  if (node->init() != NULL) {
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    Visit(node->init());
  }
  if (node->type() != LoopStatement::DO_LOOP && info != ALWAYS_TRUE) {
    __ jmp(&entry);
  }

  // body
  __ bind(&loop);
  Visit(node->body());

  // next
  __ bind(node->continue_target());
  if (node->next() != NULL) {
    // Record source position of the statement as this code which is after the
    // code for the body actually belongs to the loop statement and not the
    // body.
    if (FLAG_debug_info) __ RecordPosition(node->statement_pos());
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    Visit(node->next());
  }

  // cond
  __ bind(&entry);
  switch (info) {
    case ALWAYS_TRUE:
      CheckStack();  // TODO(1222600): ignore if body contains calls.
      __ jmp(&loop);
      break;
    case ALWAYS_FALSE:
      break;
    case DONT_KNOW:
      CheckStack();  // TODO(1222600): ignore if body contains calls.
      LoadCondition(node->cond(), CodeGenState::LOAD, &loop,
                    node->break_target(), true);
      Branch(true, &loop);
      break;
  }

  // exit
  __ bind(node->break_target());
}


void Ia32CodeGenerator::VisitForInStatement(ForInStatement* node) {
  Comment cmnt(masm_, "[ ForInStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);

  // We keep stuff on the stack while the body is executing.
  // Record it, so that a break/continue crossing this statement
  // can restore the stack.
  const int kForInStackSize = 5 * kPointerSize;
  break_stack_height_ += kForInStackSize;
  node->set_break_stack_height(break_stack_height_);

  Label loop, next, entry, cleanup, exit, primitive, jsobject;
  Label end_del_check, fixed_array;

  // Get the object to enumerate over (converted to JSObject).
  Load(node->enumerable());

  // Both SpiderMonkey and kjs ignore null and undefined in contrast
  // to the specification.  12.6.4 mandates a call to ToObject.
  __ pop(eax);

  // eax: value to be iterated over
  __ cmp(eax, Factory::undefined_value());
  __ j(equal, &exit);
  __ cmp(eax, Factory::null_value());
  __ j(equal, &exit);

  // Stack layout in body:
  // [iteration counter (Smi)] <- slot 0
  // [length of array]         <- slot 1
  // [FixedArray]              <- slot 2
  // [Map or 0]                <- slot 3
  // [Object]                  <- slot 4

  // Check if enumerable is already a JSObject
  // eax: value to be iterated over
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &primitive);
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  __ cmp(ecx, JS_OBJECT_TYPE);
  __ j(above_equal, &jsobject);

  __ bind(&primitive);
  __ push(eax);
  __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
  // function call returns the value in eax, which is where we want it below


  __ bind(&jsobject);

  // Get the set of properties (as a FixedArray or Map).
  // eax: value to be iterated over
  __ push(eax);  // push the object being iterated over (slot 4)

  __ push(eax);  // push the Object (slot 4) for the runtime call
  __ CallRuntime(Runtime::kGetPropertyNamesFast, 1);

  // If we got a Map, we can do a fast modification check.
  // Otherwise, we got a FixedArray, and we have to do a slow check.
  // eax: map or fixed array (result from call to
  // Runtime::kGetPropertyNamesFast)
  __ mov(edx, Operand(eax));
  __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
  __ cmp(ecx, Factory::meta_map());
  __ j(not_equal, &fixed_array);

  // Get enum cache
  // eax: map (result from call to Runtime::kGetPropertyNamesFast)
  __ mov(ecx, Operand(eax));
  __ mov(ecx, FieldOperand(ecx, Map::kInstanceDescriptorsOffset));
  // Get the bridge array held in the enumeration index field.
  __ mov(ecx, FieldOperand(ecx, DescriptorArray::kEnumerationIndexOffset));
  // Get the cache from the bridge array.
  __ mov(edx, FieldOperand(ecx, DescriptorArray::kEnumCacheBridgeCacheOffset));

  __ push(eax);  // <- slot 3
  __ push(Operand(edx));  // <- slot 2
  __ mov(eax, FieldOperand(edx, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  __ push(eax);  // <- slot 1
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 0
  __ jmp(&entry);


  __ bind(&fixed_array);

  // eax: fixed array (result from call to Runtime::kGetPropertyNamesFast)
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 3
  __ push(eax);  // <- slot 2

  // Push the length of the array and the initial index onto the stack.
  __ mov(eax, FieldOperand(eax, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  __ push(eax);  // <- slot 1
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 0
  __ jmp(&entry);

  // Body.
  __ bind(&loop);
  Visit(node->body());

  // Next.
  __ bind(node->continue_target());
  __ bind(&next);
  __ pop(eax);
  __ add(Operand(eax), Immediate(Smi::FromInt(1)));
  __ push(eax);

  // Condition.
  __ bind(&entry);

  __ mov(eax, Operand(esp, 0 * kPointerSize));  // load the current count
  __ cmp(eax, Operand(esp, kPointerSize));  // compare to the array length
  __ j(above_equal, &cleanup);
  // TODO(1222589): remove redundant load here, which is only needed in
  // PUSH_TOS/POP_TOS mode
  __ mov(eax, Operand(esp, 0 * kPointerSize));  // load the current count

  // Get the i'th entry of the array.
  __ mov(edx, Operand(esp, 2 * kPointerSize));
  __ mov(ebx, Operand(edx, eax, times_2,
                      FixedArray::kHeaderSize - kHeapObjectTag));

  // Get the expected map from the stack or a zero map in the
  // permanent slow case eax: current iteration count ebx: i'th entry
  // of the enum cache
  __ mov(edx, Operand(esp, 3 * kPointerSize));
  // Check if the expected map still matches that of the enumerable.
  // If not, we have to filter the key.
  // eax: current iteration count
  // ebx: i'th entry of the enum cache
  // edx: expected map value
  __ mov(ecx, Operand(esp, 4 * kPointerSize));
  __ mov(ecx, FieldOperand(ecx, HeapObject::kMapOffset));
  __ cmp(ecx, Operand(edx));
  __ j(equal, &end_del_check);

  // Convert the entry to a string (or null if it isn't a property anymore).
  __ push(Operand(esp, 4 * kPointerSize));  // push enumerable
  __ push(Operand(ebx));  // push entry
  __ InvokeBuiltin(Builtins::FILTER_KEY, CALL_FUNCTION);
  __ mov(ebx, Operand(eax));

  // If the property has been removed while iterating, we just skip it.
  __ cmp(ebx, Factory::null_value());
  __ j(equal, &next);


  __ bind(&end_del_check);

  // Store the entry in the 'each' expression and take another spin in the loop.
  // edx: i'th entry of the enum cache (or string there of)
  __ push(Operand(ebx));
  { Reference each(this, node->each());
    if (!each.is_illegal()) {
      if (each.size() > 0) {
        __ push(Operand(esp, kPointerSize * each.size()));
      }
      SetValue(&each);
      if (each.size() > 0) {
        __ pop(eax);
      }
    }
  }
  __ pop(eax);  // pop the i'th entry pushed above
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  __ jmp(&loop);

  // Cleanup.
  __ bind(&cleanup);
  __ bind(node->break_target());
  __ add(Operand(esp), Immediate(5 * kPointerSize));

  // Exit.
  __ bind(&exit);

  break_stack_height_ -= kForInStackSize;
}


void Ia32CodeGenerator::VisitTryCatch(TryCatch* node) {
  Comment cmnt(masm_, "[ TryCatch");

  Label try_block, exit;

  __ call(&try_block);
  // --- Catch block ---
  __ push(eax);

  // Store the caught exception in the catch variable.
  { Reference ref(this, node->catch_var());
    // Load the exception to the top of the stack.
    __ push(Operand(esp, ref.size() * kPointerSize));
    SetValue(&ref);
    __ pop(eax);  // pop the pushed exception
  }

  // Remove the exception from the stack.
  __ pop(edx);

  VisitStatements(node->catch_block()->statements());
  __ jmp(&exit);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_CATCH_HANDLER);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  __ push(eax);  //

  // Introduce shadow labels for all escapes from the try block,
  // including returns. We should probably try to unify the escaping
  // labels and the return label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  bool was_inside_try = is_inside_try_;
  is_inside_try_ = true;
  VisitStatements(node->try_block()->statements());
  is_inside_try_ = was_inside_try;

  // Stop the introduced shadowing and count the number of required unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Unlink from try chain.
  __ pop(eax);
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(Operand::StaticVariable(handler_address), eax);  // TOS == next_sp
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));
  // next_sp popped.
  if (nof_unlinks > 0) __ jmp(&exit);

  // Generate unlink code for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Unlink from try chain; be careful not to destroy the TOS.
      __ bind(shadows[i]);

      // Reload sp from the top handler, because some statements that we
      // break from (eg, for...in) may have left stuff on the stack.
      __ mov(edx, Operand::StaticVariable(handler_address));
      const int kNextOffset = StackHandlerConstants::kNextOffset +
          StackHandlerConstants::kAddressDisplacement;
      __ lea(esp, Operand(edx, kNextOffset));

      __ pop(Operand::StaticVariable(handler_address));
      __ add(Operand(esp),
             Immediate(StackHandlerConstants::kSize - kPointerSize));
      // next_sp popped.
      __ jmp(shadows[i]->shadowed());
    }
  }

  __ bind(&exit);
}


void Ia32CodeGenerator::VisitTryFinally(TryFinally* node) {
  Comment cmnt(masm_, "[ TryFinally");

  // State: Used to keep track of reason for entering the finally
  // block. Should probably be extended to hold information for
  // break/continue from within the try block.
  enum { FALLING, THROWING, JUMPING };

  Label exit, unlink, try_block, finally_block;

  __ call(&try_block);

  __ push(eax);
  // In case of thrown exceptions, this is where we continue.
  __ Set(ecx, Immediate(Smi::FromInt(THROWING)));
  __ jmp(&finally_block);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_FINALLY_HANDLER);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  __ push(eax);  //

  // Introduce shadow labels for all escapes from the try block,
  // including returns. We should probably try to unify the escaping
  // labels and the return label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  bool was_inside_try = is_inside_try_;
  is_inside_try_ = true;
  VisitStatements(node->try_block()->statements());
  is_inside_try_ = was_inside_try;

  // Stop the introduced shadowing and count the number of required
  // unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Set the state on the stack to FALLING.
  __ push(Immediate(Factory::undefined_value()));  // fake TOS
  __ Set(ecx, Immediate(Smi::FromInt(FALLING)));
  if (nof_unlinks > 0) __ jmp(&unlink);

  // Generate code that sets the state for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      __ bind(shadows[i]);
      if (shadows[i]->shadowed() == &function_return_) {
        // Materialize the return value on the stack.
        __ push(eax);
      } else {
        // Fake TOS for break and continue.
        __ push(Immediate(Factory::undefined_value()));
      }
      __ Set(ecx, Immediate(Smi::FromInt(JUMPING + i)));
      __ jmp(&unlink);
    }
  }

  // Unlink from try chain; be careful not to destroy the TOS.
  __ bind(&unlink);
  // Reload sp from the top handler, because some statements that we
  // break from (eg, for...in) may have left stuff on the stack.
  __ pop(eax);  // preserve the TOS in a register across stack manipulation
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));
  const int kNextOffset = StackHandlerConstants::kNextOffset +
      StackHandlerConstants::kAddressDisplacement;
  __ lea(esp, Operand(edx, kNextOffset));

  __ pop(Operand::StaticVariable(handler_address));
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));
  // next_sp popped.
  __ push(eax);  // preserve the TOS in a register across stack manipulation

  // --- Finally block ---
  __ bind(&finally_block);

  // Push the state on the stack. If necessary move the state to a
  // local variable to avoid having extra values on the stack while
  // evaluating the finally block.
  __ push(ecx);
  if (node->finally_var() != NULL) {
    Reference target(this, node->finally_var());
    SetValue(&target);
    ASSERT(target.size() == 0);  // no extra stuff on the stack
    __ pop(edx);  // remove the extra value that was pushed above
  }

  // Generate code for the statements in the finally block.
  VisitStatements(node->finally_block()->statements());

  // Get the state from the stack - or the local variable - and
  // restore the TOS register.
  if (node->finally_var() != NULL) {
    Reference target(this, node->finally_var());
    GetValue(&target);
  }
  __ pop(ecx);

  // Restore return value or faked TOS.
  __ pop(eax);

  // Generate code that jumps to the right destination for all used
  // shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_bound()) {
      __ cmp(Operand(ecx), Immediate(Smi::FromInt(JUMPING + i)));
      __ j(equal, shadows[i]->shadowed());
    }
  }

  // Check if we need to rethrow the exception.
  __ cmp(Operand(ecx), Immediate(Smi::FromInt(THROWING)));
  __ j(not_equal, &exit);

  // Rethrow exception.
  __ push(eax);  // undo pop from above
  __ CallRuntime(Runtime::kReThrow, 1);

  // Done.
  __ bind(&exit);
}


void Ia32CodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  Comment cmnt(masm_, "[ DebuggerStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  __ CallRuntime(Runtime::kDebugBreak, 1);
  __ push(eax);
}


void Ia32CodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  ASSERT(boilerplate->IsBoilerplate());

  // Push the boilerplate on the stack.
  __ push(Immediate(boilerplate));

  // Create a new closure.
  __ push(esi);
  __ CallRuntime(Runtime::kNewClosure, 2);
  __ push(eax);
}


void Ia32CodeGenerator::VisitFunctionLiteral(FunctionLiteral* node) {
  Comment cmnt(masm_, "[ FunctionLiteral");

  // Build the function boilerplate and instantiate it.
  Handle<JSFunction> boilerplate = BuildBoilerplate(node);
  // Check for stack-overflow exception.
  if (HasStackOverflow()) return;
  InstantiateBoilerplate(boilerplate);
}


void Ia32CodeGenerator::VisitFunctionBoilerplateLiteral(
    FunctionBoilerplateLiteral* node) {
  Comment cmnt(masm_, "[ FunctionBoilerplateLiteral");
  InstantiateBoilerplate(node->boilerplate());
}


void Ia32CodeGenerator::VisitConditional(Conditional* node) {
  Comment cmnt(masm_, "[ Conditional");
  Label then, else_, exit;
  LoadCondition(node->condition(), CodeGenState::LOAD, &then, &else_, true);
  Branch(false, &else_);
  __ bind(&then);
  Load(node->then_expression(), access());
  __ jmp(&exit);
  __ bind(&else_);
  Load(node->else_expression(), access());
  __ bind(&exit);
}


void Ia32CodeGenerator::VisitSlot(Slot* node) {
  Comment cmnt(masm_, "[ Slot");

  if (node->type() == Slot::LOOKUP) {
    ASSERT(node->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    __ push(Operand(esi));
    __ push(Immediate(node->var()->name()));

    switch (access()) {
      case CodeGenState::UNDEFINED:
        UNREACHABLE();
        break;

      case CodeGenState::LOAD:
        __ CallRuntime(Runtime::kLoadContextSlot, 2);
        __ push(eax);
        // result (TOS) is the value that was loaded
        break;

      case CodeGenState::LOAD_TYPEOF_EXPR:
        __ CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
        __ push(eax);
        // result (TOS) is the value that was loaded
        break;

      case CodeGenState::STORE:
        // Storing a variable must keep the (new) value on the
        // stack. This is necessary for compiling assignment
        // expressions.
        __ CallRuntime(Runtime::kStoreContextSlot, 3);
        __ push(eax);
        // result (TOS) is the value that was stored
        break;

      case CodeGenState::INIT_CONST:
        // Same as STORE but ignores attribute (e.g. READ_ONLY) of
        // context slot so that we can initialize const properties
        // (introduced via eval("const foo = (some expr);")). Also,
        // uses the current function context instead of the top
        // context.
        //
        // Note that we must declare the foo upon entry of eval(),
        // via a context slot declaration, but we cannot initialize
        // it at the same time, because the const declaration may
        // be at the end of the eval code (sigh...) and the const
        // variable may have been used before (where its value is
        // 'undefined'). Thus, we can only do the initialization
        // when we actually encounter the expression and when the
        // expression operands are defined and valid, and thus we
        // need the split into 2 operations: declaration of the
        // context slot followed by initialization.
        //
        __ CallRuntime(Runtime::kInitializeConstContextSlot, 3);
        __ push(eax);
        break;
    }

  } else {
    // Note: We would like to keep the assert below, but it fires because
    // of some nasty code in LoadTypeofExpression() which should be removed...
    // ASSERT(node->var()->mode() != Variable::DYNAMIC);

    switch (access()) {
      case CodeGenState::UNDEFINED:
        UNREACHABLE();
        break;

      case CodeGenState::LOAD:  // fall through
      case CodeGenState::LOAD_TYPEOF_EXPR:
        if (node->var()->mode() == Variable::CONST) {
          // Const slots may contain 'the hole' value (the constant hasn't
          // been initialized yet) which needs to be converted into the
          // 'undefined' value.
          Comment cmnt(masm_, "[ Load const");
          Label L;
          __ mov(eax, SlotOperand(node, ecx));
          __ cmp(eax, Factory::the_hole_value());
          __ j(not_equal, &L);
          __ mov(eax, Factory::undefined_value());
          __ bind(&L);
          __ push(eax);
        } else {
          __ push(SlotOperand(node, ecx));
        }
        break;

      case CodeGenState::INIT_CONST:
        ASSERT(node->var()->mode() == Variable::CONST);
        // Only the first const initialization must be executed (the slot
        // still contains 'the hole' value). When the assignment is executed,
        // the code is identical to a normal store (see below).
        { Comment cmnt(masm_, "[ Init const");
          Label L;
          __ mov(eax, SlotOperand(node, ecx));
          __ cmp(eax, Factory::the_hole_value());
          __ j(not_equal, &L);
          // We must execute the store.
          __ mov(eax, TOS);
          __ mov(SlotOperand(node, ecx), eax);
          if (node->type() == Slot::CONTEXT) {
            // ecx is loaded with context when calling SlotOperand above.
            int offset = FixedArray::kHeaderSize + node->index() * kPointerSize;
            __ RecordWrite(ecx, offset, eax, ebx);
          }
          __ bind(&L);
        }
        break;

      case CodeGenState::STORE:
        // Storing a variable must keep the (new) value on the stack. This
        // is necessary for compiling assignment expressions.
        // ecx may be loaded with context; used below in RecordWrite.
        //
        // Note: We will reach here even with node->var()->mode() ==
        // Variable::CONST because of const declarations which will
        // initialize consts to 'the hole' value and by doing so, end
        // up calling this code.
        __ mov(eax, TOS);
        __ mov(SlotOperand(node, ecx), eax);
        if (node->type() == Slot::CONTEXT) {
          // ecx is loaded with context when calling SlotOperand above.
          int offset = FixedArray::kHeaderSize + node->index() * kPointerSize;
          __ RecordWrite(ecx, offset, eax, ebx);
        }
        break;
    }
  }
}


void Ia32CodeGenerator::VisitVariableProxy(VariableProxy* proxy_node) {
  Comment cmnt(masm_, "[ VariableProxy");
  Variable* node = proxy_node->var();

  Expression* x = node->rewrite();
  if (x != NULL) {
    Visit(x);
    return;
  }

  ASSERT(node->is_global());
  if (is_referenced()) {
    if (node->AsProperty() != NULL) {
      __ RecordPosition(node->AsProperty()->position());
    }
    AccessReferenceProperty(new Literal(node->name()), access());

  } else {
    // All stores are through references.
    ASSERT(access() != CodeGenState::STORE);
    Reference property(this, proxy_node);
    GetValue(&property);
  }
}


void Ia32CodeGenerator::VisitLiteral(Literal* node) {
  Comment cmnt(masm_, "[ Literal");
  __ push(Immediate(node->handle()));
}


class RegExpDeferred: public DeferredCode {
 public:
  RegExpDeferred(CodeGenerator* generator, RegExpLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ RegExpDeferred");
  }
  virtual void Generate();
 private:
  RegExpLiteral* node_;
};


void RegExpDeferred::Generate() {
  // If the entry is undefined we call the runtime system to computed
  // the literal.

  // Literal array (0).
  __ push(ecx);
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // RegExp pattern (2).
  __ push(Immediate(node_->pattern()));
  // RegExp flags (3).
  __ push(Immediate(node_->flags()));
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ mov(ebx, Operand(eax));  // "caller" expects result in ebx
}


void Ia32CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  Comment cmnt(masm_, "[ RegExp Literal");
  RegExpDeferred* deferred = new RegExpDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, FunctionOperand());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the RegExp object.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  __ j(equal, deferred->enter(), not_taken);
  __ bind(deferred->exit());

  // Push the literal.
  __ push(ebx);
}


// This deferred code stub will be used for creating the boilerplate
// by calling Runtime_CreateObjectLiteral.
// Each created boilerplate is stored in the JSFunction and they are
// therefore context dependent.
class ObjectLiteralDeferred: public DeferredCode {
 public:
  ObjectLiteralDeferred(CodeGenerator* generator, ObjectLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ ObjectLiteralDeferred");
  }
  virtual void Generate();
 private:
  ObjectLiteral* node_;
};


void ObjectLiteralDeferred::Generate() {
  // If the entry is undefined we call the runtime system to computed
  // the literal.

  // Literal array (0).
  __ push(Operand(ecx));
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  __ push(Immediate(node_->constant_properties()));
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ mov(ebx, Operand(eax));
}


void Ia32CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  Comment cmnt(masm_, "[ ObjectLiteral");

  ObjectLiteralDeferred* deferred = new ObjectLiteralDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, FunctionOperand());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  __ j(equal, deferred->enter(), not_taken);
  __ bind(deferred->exit());

  // Push the literal.
  __ push(ebx);
  // Clone the boilerplate object.
  __ CallRuntime(Runtime::kCloneObjectLiteralBoilerplate, 1);
  // Push the new cloned literal object as the result.
  __ push(eax);

  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property  = node->properties()->at(i);
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT: break;
      case ObjectLiteral::Property::COMPUTED: {
        Handle<Object> key(property->key()->handle());
        Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
        if (key->IsSymbol()) {
          __ mov(eax, TOS);
          __ push(eax);
          Load(property->value());
          __ pop(eax);
          __ Set(ecx, Immediate(key));
          __ call(ic, code_target);
          __ add(Operand(esp), Immediate(kPointerSize));
          // Ignore result.
          break;
        }
        // Fall through
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        Load(property->value());
        __ CallRuntime(Runtime::kSetProperty, 3);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        __ push(Immediate(Smi::FromInt(1)));
        Load(property->value());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        __ push(Immediate(Smi::FromInt(0)));
        Load(property->value());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      default: UNREACHABLE();
    }
  }
}


void Ia32CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  Comment cmnt(masm_, "[ ArrayLiteral");
  // Load the resulting object.
  Load(node->result());
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is literal the property value is already
    // set in the boilerplate object.
    if (value->AsLiteral() == NULL) {
      // The property must be set by generated code.
      Load(value);

      // Get the value off the stack.
      __ pop(eax);
      // Fetch the object literal while leaving on the stack.
      __ mov(ecx, TOS);
      // Get the elements array.
      __ mov(ecx, FieldOperand(ecx, JSObject::kElementsOffset));

      // Write to the indexed properties array.
      int offset = i * kPointerSize + Array::kHeaderSize;
      __ mov(FieldOperand(ecx, offset), eax);

      // Update the write barrier for the array address.
      __ RecordWrite(ecx, offset, eax, ebx);
    }
  }
}


void Ia32CodeGenerator::VisitAssignment(Assignment* node) {
  Comment cmnt(masm_, "[ Assignment");

  if (FLAG_debug_info) RecordStatementPosition(node);
  Reference target(this, node->target());
  if (target.is_illegal()) return;

  if (node->op() == Token::ASSIGN ||
      node->op() == Token::INIT_VAR ||
      node->op() == Token::INIT_CONST) {
    Load(node->value());

  } else {
    GetValue(&target);
    Literal* literal = node->value()->AsLiteral();
    if (literal != NULL && literal->handle()->IsSmi()) {
      SmiOperation(node->binary_op(), literal->handle(), false, NO_OVERWRITE);
    } else {
      Load(node->value());
      bool done = InlinedGenericOperation(node->binary_op(), NO_OVERWRITE,
                                          false /*negate_result*/);
      if (!done) {
        GenericOperation(node->binary_op());
      }
    }
  }

  Variable* var = node->target()->AsVariableProxy()->AsVariable();
  if (var != NULL &&
      var->mode() == Variable::CONST &&
      node->op() != Token::INIT_VAR && node->op() != Token::INIT_CONST) {
    // Assignment ignored - leave the value on the stack.
  } else {
    __ RecordPosition(node->position());
    if (node->op() == Token::INIT_CONST) {
      // Dynamic constant initializations must use the function context
      // and initialize the actual constant declared. Dynamic variable
      // initializations are simply assignments and use SetValue.
      InitConst(&target);
    } else {
      SetValue(&target);
    }
  }
}


void Ia32CodeGenerator::VisitThrow(Throw* node) {
  Comment cmnt(masm_, "[ Throw");

  Load(node->exception());
  __ RecordPosition(node->position());
  __ CallRuntime(Runtime::kThrow, 1);
  __ push(eax);
}


void Ia32CodeGenerator::VisitProperty(Property* node) {
  Comment cmnt(masm_, "[ Property");

  if (is_referenced()) {
    __ RecordPosition(node->position());
    AccessReferenceProperty(node->key(), access());
  } else {
    // All stores are through references.
    ASSERT(access() != CodeGenState::STORE);
    Reference property(this, node);
    __ RecordPosition(node->position());
    GetValue(&property);
  }
}


void Ia32CodeGenerator::VisitCall(Call* node) {
  Comment cmnt(masm_, "[ Call");

  ZoneList<Expression*>* args = node->arguments();

  if (FLAG_debug_info) RecordStatementPosition(node);

  // Check if the function is a variable or a property.
  Expression* function = node->expression();
  Variable* var = function->AsVariableProxy()->AsVariable();
  Property* property = function->AsProperty();

  // ------------------------------------------------------------------------
  // Fast-case: Use inline caching.
  // ---
  // According to ECMA-262, section 11.2.3, page 44, the function to call
  // must be resolved after the arguments have been evaluated. The IC code
  // automatically handles this by loading the arguments before the function
  // is resolved in cache misses (this also holds for megamorphic calls).
  // ------------------------------------------------------------------------

  if (var != NULL && !var->is_this() && var->is_global()) {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is global
    // ----------------------------------

    // Push the name of the function and the receiver onto the stack.
    __ push(Immediate(var->name()));
    LoadGlobal();

    // Load the arguments.
    for (int i = 0; i < args->length(); i++) {
      Load(args->at(i));
    }

    // Setup the receiver register and call the IC initialization code.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ RecordPosition(node->position());
    __ call(stub, code_target_context);
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

    // Overwrite the function on the stack with the result.
    __ mov(TOS, eax);

  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function
    __ push(Operand(esi));
    __ push(Immediate(var->name()));
    __ CallRuntime(Runtime::kLoadContextSlot, 2);
    // eax: slot value; edx: receiver

    // Load the receiver.
    __ push(eax);
    __ push(edx);

    // Call the function.
    CallWithArguments(args, node->position());

  } else if (property != NULL) {
    // Check if the key is a literal string.
    Literal* literal = property->key()->AsLiteral();

    if (literal != NULL && literal->handle()->IsSymbol()) {
      // ------------------------------------------------------------------
      // JavaScript example: 'object.foo(1, 2, 3)' or 'map["key"](1, 2, 3)'
      // ------------------------------------------------------------------

      // Push the name of the function and the receiver onto the stack.
      __ push(Immediate(literal->handle()));
      Load(property->obj());

      // Load the arguments.
      for (int i = 0; i < args->length(); i++) Load(args->at(i));

      // Call the IC initialization code.
      Handle<Code> stub = ComputeCallInitialize(args->length());
      __ RecordPosition(node->position());
      __ call(stub, code_target);
      __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

      // Overwrite the function on the stack with the result.
      __ mov(TOS, eax);

    } else {
      // -------------------------------------------
      // JavaScript example: 'array[index](1, 2, 3)'
      // -------------------------------------------

      // Load the function to call from the property through a reference.
      Reference ref(this, property);
      GetValue(&ref);

      // Pass receiver to called function.
      __ push(Operand(esp, ref.size() * kPointerSize));

      // Call the function.
      CallWithArguments(args, node->position());
    }

  } else {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is not global
    // ----------------------------------

    // Load the function.
    Load(function);

    // Pass the global object as the receiver.
    LoadGlobal();

    // Call the function.
    CallWithArguments(args, node->position());
  }
}


void Ia32CodeGenerator::VisitCallNew(CallNew* node) {
  Comment cmnt(masm_, "[ CallNew");

  // According to ECMA-262, section 11.2.2, page 44, the function
  // expression in new calls must be evaluated before the
  // arguments. This is different from ordinary calls, where the
  // actual function to call is resolved after the arguments have been
  // evaluated.

  // Compute function to call and use the global object as the
  // receiver.
  Load(node->expression());
  LoadGlobal();

  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = node->arguments();
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  // Constructors are called with the number of arguments in register
  // eax for now. Another option would be to have separate construct
  // call trampolines per different arguments counts encountered.
  __ Set(eax, Immediate(args->length()));

  // Load the function into temporary function slot as per calling
  // convention.
  __ mov(edi, Operand(esp, (args->length() + 1) * kPointerSize));

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  __ RecordPosition(node->position());
  __ call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
          js_construct_call);
  __ mov(TOS, eax);  // discard the function and "push" the newly created object
}


void Ia32CodeGenerator::GenerateSetThisFunction(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateGetThisFunction(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateSetThis(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateSetArgumentsLength(
    ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateGetArgumentsLength(
    ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateTailCallWithArguments(
     ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateSetArgument(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateSquashFrame(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateExpandFrame(ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateShiftDownAndTailCall(
    ZoneList<Expression*>* args) {
  // Not used on IA-32 anymore. Should go away soon.
  __ int3();
}


void Ia32CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ pop(eax);
  __ test(eax, Immediate(kSmiTagMask));
  cc_reg_ = zero;
}


void Ia32CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  Label answer;
  // We need the CC bits to come out as not_equal in the case where the
  // object is a Smi.  This can't be done with the usual test opcode so
  // we copy the object to ecx and do some destructive ops on it that
  // result in the right CC bits.
  __ pop(eax);
  __ mov(ecx, Operand(eax));
  __ and_(ecx, kSmiTagMask);
  __ xor_(ecx, kSmiTagMask);
  __ j(not_equal, &answer, not_taken);
  // It is a heap object - get map.
  __ mov(eax, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(eax, FieldOperand(eax, Map::kInstanceTypeOffset));
  // Check if the object is a JS array or not.
  __ cmp(eax, JS_ARRAY_TYPE);
  __ bind(&answer);
  cc_reg_ = equal;
}


void Ia32CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);

  // Seed the result with the formal parameters count, which will be
  // used in case no arguments adaptor frame is found below the
  // current frame.
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to the arguments.length.
  ArgumentsAccessStub stub(true);
  __ CallStub(&stub);
  __ push(eax);
}


void Ia32CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Label leave;
  Load(args->at(0));  // Load the object.
  __ mov(eax, TOS);
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &leave, taken);
  // It is a heap object - get map.
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ecx, JS_VALUE_TYPE);
  __ j(not_equal, &leave, not_taken);
  __ mov(eax, FieldOperand(eax, JSValue::kValueOffset));
  __ mov(TOS, eax);
  __ bind(&leave);
}


void Ia32CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  Label leave;
  Load(args->at(0));  // Load the object.
  Load(args->at(1));  // Load the value.
  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, TOS);
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &leave, taken);
  // It is a heap object - get map.
  __ mov(ebx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ebx, JS_VALUE_TYPE);
  __ j(not_equal, &leave, not_taken);
  // Store the value.
  __ mov(FieldOperand(eax, JSValue::kValueOffset), ecx);
  // Update the write barrier.
  __ RecordWrite(eax, JSValue::kValueOffset, ecx, ebx);
  // Leave.
  __ bind(&leave);
  __ mov(ecx, TOS);
  __ pop(eax);
  __ mov(TOS, ecx);
}


void Ia32CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);

  // Load the key onto the stack and set register eax to the formal
  // parameters count for the currently executing function.
  Load(args->at(0));
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to arguments[key].
  ArgumentsAccessStub stub(false);
  __ CallStub(&stub);
  __ mov(TOS, eax);
}



void Ia32CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  if (CheckForInlineRuntimeCall(node)) return;

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function == NULL) {
    // Prepare stack for calling JS runtime function.
    __ push(Immediate(node->name()));
    // Push the builtins object found in the current global object.
    __ mov(edx, GlobalObject());
    __ push(FieldOperand(edx, GlobalObject::kBuiltinsOffset));
  }

  // Push the arguments ("left-to-right").
  for (int i = 0; i < args->length(); i++)
    Load(args->at(i));

  if (function != NULL) {
    // Call the C runtime function.
    __ CallRuntime(function, args->length());
    __ push(eax);
  } else {
    // Call the JS runtime function.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ Set(eax, Immediate(args->length()));
    __ call(stub, code_target);
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
    __ mov(TOS, eax);
  }
}


void Ia32CodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  Comment cmnt(masm_, "[ UnaryOperation");

  Token::Value op = node->op();

  if (op == Token::NOT) {
    LoadCondition(node->expression(), CodeGenState::LOAD,
                  false_target(), true_target(), true);
    cc_reg_ = NegateCondition(cc_reg_);

  } else if (op == Token::DELETE) {
    Property* property = node->expression()->AsProperty();
    if (property != NULL) {
      Load(property->obj());
      Load(property->key());
      __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
      __ push(eax);
      return;
    }

    Variable* variable = node->expression()->AsVariableProxy()->AsVariable();
    if (variable != NULL) {
      Slot* slot = variable->slot();
      if (variable->is_global()) {
        LoadGlobal();
        __ push(Immediate(variable->name()));
        __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
        __ push(eax);
        return;

      } else if (slot != NULL && slot->type() == Slot::LOOKUP) {
        // lookup the context holding the named variable
        __ push(Operand(esi));
        __ push(Immediate(variable->name()));
        __ CallRuntime(Runtime::kLookupContext, 2);
        // eax: context
        __ push(eax);
        __ push(Immediate(variable->name()));
        __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
        __ push(eax);
        return;
      }

      // Default: Result of deleting non-global, not dynamically
      // introduced variables is false.
      __ push(Immediate(Factory::false_value()));

    } else {
      // Default: Result of deleting expressions is true.
      Load(node->expression());  // may have side-effects
      __ Set(TOS, Immediate(Factory::true_value()));
    }

  } else if (op == Token::TYPEOF) {
    // Special case for loading the typeof expression; see comment on
    // LoadTypeofExpression().
    LoadTypeofExpression(node->expression());
    __ CallRuntime(Runtime::kTypeof, 1);
    __ push(eax);

  } else {
    Load(node->expression());
    switch (op) {
      case Token::NOT:
      case Token::DELETE:
      case Token::TYPEOF:
        UNREACHABLE();  // handled above
        break;

      case Token::SUB: {
        UnarySubStub stub;
        // TODO(1222589): remove dependency of TOS being cached inside stub
        __ pop(eax);
        __ CallStub(&stub);
        __ push(eax);
        break;
      }

      case Token::BIT_NOT: {
        // smi check
        Label smi_label;
        Label continue_label;
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ j(zero, &smi_label, taken);

        __ push(eax);  // undo popping of TOS
        __ InvokeBuiltin(Builtins::BIT_NOT, CALL_FUNCTION);

        __ jmp(&continue_label);
        __ bind(&smi_label);
        __ not_(eax);
        __ and_(eax, ~kSmiTagMask);  // remove inverted smi-tag
        __ bind(&continue_label);
        __ push(eax);
        break;
      }

      case Token::VOID:
        __ mov(TOS, Factory::undefined_value());
        break;

      case Token::ADD:
        __ InvokeBuiltin(Builtins::TO_NUMBER, CALL_FUNCTION);
        __ push(eax);
        break;

      default:
        UNREACHABLE();
    }
  }
}


class CountOperationDeferred: public DeferredCode {
 public:
  CountOperationDeferred(CodeGenerator* generator,
                         bool is_postfix,
                         bool is_increment,
                         int result_offset)
      : DeferredCode(generator),
        is_postfix_(is_postfix),
        is_increment_(is_increment),
        result_offset_(result_offset) {
    set_comment("[ CountOperationDeferred");
  }

  virtual void Generate();

 private:
  bool is_postfix_;
  bool is_increment_;
  int result_offset_;
};


#undef __
#define __  masm->


class RevertToNumberStub: public CodeStub {
 public:
  explicit RevertToNumberStub(bool is_increment)
     :  is_increment_(is_increment) { }

 private:
  bool is_increment_;

  Major MajorKey() { return RevertToNumber; }
  int MinorKey() { return is_increment_ ? 1 : 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "RevertToNumberStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("RevertToNumberStub (is_increment %s)\n",
           is_increment_ ? "true" : "false");
  }
#endif
};


void RevertToNumberStub::Generate(MacroAssembler* masm) {
  // Revert optimistic increment/decrement.
  if (is_increment_) {
    __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
  } else {
    __ add(Operand(eax), Immediate(Smi::FromInt(1)));
  }

  __ pop(ecx);
  __ push(eax);
  __ push(ecx);
  __ InvokeBuiltin(Builtins::TO_NUMBER, JUMP_FUNCTION);
  // Code never returns due to JUMP_FUNCTION.
}


class CounterOpStub: public CodeStub {
 public:
  CounterOpStub(int result_offset, bool is_postfix, bool is_increment)
     :  result_offset_(result_offset),
        is_postfix_(is_postfix),
        is_increment_(is_increment) { }

 private:
  int result_offset_;
  bool is_postfix_;
  bool is_increment_;

  Major MajorKey() { return CounterOp; }
  int MinorKey() {
    return ((result_offset_ << 2) |
            (is_postfix_ ? 2 : 0) |
            (is_increment_ ? 1 : 0));
  }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "CounterOpStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("CounterOpStub (result_offset %d), (is_postfix %s),"
           " (is_increment %s)\n",
           result_offset_,
           is_postfix_ ? "true" : "false",
           is_increment_ ? "true" : "false");
  }
#endif
};


void CounterOpStub::Generate(MacroAssembler* masm) {
  // Store to the result on the stack (skip return address) before
  // performing the count operation.
  if (is_postfix_) {
    __ mov(Operand(esp, result_offset_ + kPointerSize), eax);
  }

  // Revert optimistic increment/decrement but only for prefix
  // counts. For postfix counts it has already been reverted before
  // the conversion to numbers.
  if (!is_postfix_) {
    if (is_increment_) {
      __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
    } else {
      __ add(Operand(eax), Immediate(Smi::FromInt(1)));
    }
  }

  // Compute the new value by calling the right JavaScript native.
  __ pop(ecx);
  __ push(eax);
  __ push(ecx);
  Builtins::JavaScript builtin = is_increment_ ? Builtins::INC : Builtins::DEC;
  __ InvokeBuiltin(builtin, JUMP_FUNCTION);
  // Code never returns due to JUMP_FUNCTION.
}


#undef __
#define __  masm_->


void CountOperationDeferred::Generate() {
  if (is_postfix_) {
    RevertToNumberStub to_number_stub(is_increment_);
    __ CallStub(&to_number_stub);
  }
  CounterOpStub stub(result_offset_, is_postfix_, is_increment_);
  __ CallStub(&stub);
}


void Ia32CodeGenerator::VisitCountOperation(CountOperation* node) {
  Comment cmnt(masm_, "[ CountOperation");

  bool is_postfix = node->is_postfix();
  bool is_increment = node->op() == Token::INC;

  Variable* var = node->expression()->AsVariableProxy()->AsVariable();
  bool is_const = (var != NULL && var->mode() == Variable::CONST);

  // Postfix: Make room for the result.
  if (is_postfix) __ push(Immediate(0));

  { Reference target(this, node->expression());
    if (target.is_illegal()) return;
    GetValue(&target);

    int result_offset = target.size() * kPointerSize;
    CountOperationDeferred* deferred =
        new CountOperationDeferred(this, is_postfix,
                                   is_increment, result_offset);

    __ pop(eax);  // Load TOS into eax for calculations below

    // Postfix: Store the old value as the result.
    if (is_postfix) __ mov(Operand(esp, result_offset), eax);

    // Perform optimistic increment/decrement.
    if (is_increment) {
      __ add(Operand(eax), Immediate(Smi::FromInt(1)));
    } else {
      __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
    }

    // If the count operation didn't overflow and the result is a
    // valid smi, we're done. Otherwise, we jump to the deferred
    // slow-case code.
    __ j(overflow, deferred->enter(), not_taken);
    __ test(eax, Immediate(kSmiTagMask));
    __ j(not_zero, deferred->enter(), not_taken);

    // Store the new value in the target if not const.
    __ bind(deferred->exit());
    __ push(eax);  // Push the new value to TOS
    if (!is_const) SetValue(&target);
  }

  // Postfix: Discard the new value and use the old.
  if (is_postfix) __ pop(eax);
}


// Returns 'true' if able to defer negation to the consuming arithmetic
// operation.
bool Ia32CodeGenerator::TryDeferNegate(Expression* x) {
  UnaryOperation* unary = x->AsUnaryOperation();
  if (FLAG_defer_negation && unary != NULL && unary->op() == Token::SUB) {
    Load(unary->expression());
    return true;
  } else {
    Load(x);
    return false;
  }
}


void Ia32CodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
  Comment cmnt(masm_, "[ BinaryOperation");
  Token::Value op = node->op();

  // According to ECMA-262 section 11.11, page 58, the binary logical
  // operators must yield the result of one of the two expressions
  // before any ToBoolean() conversions. This means that the value
  // produced by a && or || operator is not necessarily a boolean.

  // NOTE: If the left hand side produces a materialized value (not in
  // the CC register), we force the right hand side to do the
  // same. This is necessary because we may have to branch to the exit
  // after evaluating the left hand side (due to the shortcut
  // semantics), but the compiler must (statically) know if the result
  // of compiling the binary operation is materialized or not.

  if (op == Token::AND) {
    Label is_true;
    LoadCondition(node->left(), CodeGenState::LOAD, &is_true,
                  false_target(), false);
    if (has_cc()) {
      Branch(false, false_target());

      // Evaluate right side expression.
      __ bind(&is_true);
      LoadCondition(node->right(), CodeGenState::LOAD, true_target(),
                    false_target(), false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
       // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      __ mov(eax, TOS);
      __ push(eax);
      ToBoolean(&pop_and_continue, &exit);
      Branch(false, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(eax);

      // Evaluate right side expression.
      __ bind(&is_true);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else if (op == Token::OR) {
    Label is_false;
    LoadCondition(node->left(), CodeGenState::LOAD, true_target(),
                  &is_false, false);
    if (has_cc()) {
      Branch(true, true_target());

      // Evaluate right side expression.
      __ bind(&is_false);
      LoadCondition(node->right(), CodeGenState::LOAD, true_target(),
                    false_target(), false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      __ mov(eax, TOS);
      __ push(eax);
      ToBoolean(&exit, &pop_and_continue);
      Branch(true, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(eax);

      // Evaluate right side expression.
      __ bind(&is_false);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else {
    OverwriteMode overwrite_mode = NO_OVERWRITE;
    if (node->left()->AsBinaryOperation() != NULL &&
        node->left()->AsBinaryOperation()->ResultOverwriteAllowed()) {
      overwrite_mode = OVERWRITE_LEFT;
    } else if (node->right()->AsBinaryOperation() != NULL &&
               node->right()->AsBinaryOperation()->ResultOverwriteAllowed()) {
      overwrite_mode = OVERWRITE_RIGHT;
    }

    // Optimize for the case where (at least) one of the expressions
    // is a literal small integer.
    Literal* lliteral = node->left()->AsLiteral();
    Literal* rliteral = node->right()->AsLiteral();

    if (rliteral != NULL && rliteral->handle()->IsSmi()) {
      Load(node->left());
      SmiOperation(node->op(), rliteral->handle(), false, overwrite_mode);

    } else if (lliteral != NULL && lliteral->handle()->IsSmi()) {
      Load(node->right());
      SmiOperation(node->op(), lliteral->handle(), true, overwrite_mode);

    } else {
      bool negate_result = false;
      if (node->op() == Token::MUL) {  // Implement only MUL for starters
        bool left_negated = TryDeferNegate(node->left());
        bool right_negated = TryDeferNegate(node->right());
        negate_result = left_negated != right_negated;
      } else {
        Load(node->left());
        Load(node->right());
      }
      const bool done = InlinedGenericOperation(node->op(), overwrite_mode,
                                                negate_result);
      if (!done) {
        // Defer negation implemented only for inlined generic ops.
        ASSERT(!negate_result);
        GenericOperation(node->op(), overwrite_mode);
      }
    }
  }
}


void Ia32CodeGenerator::VisitThisFunction(ThisFunction* node) {
  __ push(FunctionOperand());
}


void Ia32CodeGenerator::VisitCompareOperation(CompareOperation* node) {
  Comment cmnt(masm_, "[ CompareOperation");

  // Get the expressions from the node.
  Expression* left = node->left();
  Expression* right = node->right();
  Token::Value op = node->op();

  // NOTE: To make null checks efficient, we check if either left or
  // right is the literal 'null'. If so, we optimize the code by
  // inlining a null check instead of calling the (very) general
  // runtime routine for checking equality.

  bool left_is_null =
    left->AsLiteral() != NULL && left->AsLiteral()->IsNull();
  bool right_is_null =
    right->AsLiteral() != NULL && right->AsLiteral()->IsNull();

  if (op == Token::EQ || op == Token::EQ_STRICT) {
    // The 'null' value is only equal to 'null' or 'undefined'.
    if (left_is_null || right_is_null) {
      Load(left_is_null ? right : left);
      Label exit, undetectable;
      __ pop(eax);
      __ cmp(eax, Factory::null_value());

      // The 'null' value is only equal to 'undefined' if using
      // non-strict comparisons.
      if (op != Token::EQ_STRICT) {
        __ j(equal, &exit);
        __ cmp(eax, Factory::undefined_value());

        // NOTE: it can be an undetectable object.
        __ j(equal, &exit);
        __ test(eax, Immediate(kSmiTagMask));

        __ j(not_equal, &undetectable);
        __ jmp(false_target());

        __ bind(&undetectable);
        __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
        __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
        __ and_(ecx, 1 << Map::kIsUndetectable);
        __ cmp(ecx, 1 << Map::kIsUndetectable);
      }

      __ bind(&exit);

      cc_reg_ = equal;
      return;
    }
  }


  // NOTE: To make typeof testing for natives implemented in
  // JavaScript really efficient, we generate special code for
  // expressions of the form: 'typeof <expression> == <string>'.

  UnaryOperation* operation = left->AsUnaryOperation();
  if ((op == Token::EQ || op == Token::EQ_STRICT) &&
      (operation != NULL && operation->op() == Token::TYPEOF) &&
      (right->AsLiteral() != NULL &&
       right->AsLiteral()->handle()->IsString())) {
    Handle<String> check(String::cast(*right->AsLiteral()->handle()));

    // Load the operand, move it to register edx, and restore TOS.
    LoadTypeofExpression(operation->expression());
    __ pop(edx);

    if (check->Equals(Heap::number_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, true_target());
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ cmp(edx, Factory::heap_number_map());
      cc_reg_ = equal;

    } else if (check->Equals(Heap::string_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));

      // NOTE: it might be an undetectable string object
      __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
      __ and_(ecx, 1 << Map::kIsUndetectable);
      __ cmp(ecx, 1 << Map::kIsUndetectable);
      __ j(equal, false_target());

      __ movzx_b(ecx, FieldOperand(edx, Map::kInstanceTypeOffset));
      __ cmp(ecx, FIRST_NONSTRING_TYPE);
      cc_reg_ = less;

    } else if (check->Equals(Heap::boolean_symbol())) {
      __ cmp(edx, Factory::true_value());
      __ j(equal, true_target());
      __ cmp(edx, Factory::false_value());
      cc_reg_ = equal;

    } else if (check->Equals(Heap::undefined_symbol())) {
      __ cmp(edx, Factory::undefined_value());
      __ j(equal, true_target());

      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      // NOTE: it can be an undetectable object.
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
      __ and_(ecx, 1 << Map::kIsUndetectable);
      __ cmp(ecx, 1 << Map::kIsUndetectable);

      cc_reg_ = equal;

    } else if (check->Equals(Heap::function_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(edx, FieldOperand(edx, Map::kInstanceTypeOffset));
      __ cmp(edx, JS_FUNCTION_TYPE);
      cc_reg_ = equal;

    } else if (check->Equals(Heap::object_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
      __ cmp(edx, Factory::null_value());
      __ j(equal, true_target());

      // NOTE: it might be an undetectable object
      __ movzx_b(edx, FieldOperand(ecx, Map::kBitFieldOffset));
      __ and_(edx, 1 << Map::kIsUndetectable);
      __ cmp(edx, 1 << Map::kIsUndetectable);
      __ j(equal, false_target());

      __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
      __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
      __ j(less, false_target());
      __ cmp(ecx, LAST_JS_OBJECT_TYPE);
      cc_reg_ = less_equal;

    } else {
      // Uncommon case: Typeof testing against a string literal that
      // is never returned from the typeof operator.
      __ jmp(false_target());
    }
    return;
  }

  Condition cc = no_condition;
  bool strict = false;
  switch (op) {
    case Token::EQ_STRICT:
      strict = true;
      // Fall through
    case Token::EQ:
      cc = equal;
      break;
    case Token::LT:
      cc = less;
      break;
    case Token::GT:
      cc = greater;
      break;
    case Token::LTE:
      cc = less_equal;
      break;
    case Token::GTE:
      cc = greater_equal;
      break;
    case Token::IN: {
      Load(left);
      Load(right);
      __ InvokeBuiltin(Builtins::IN, CALL_FUNCTION);
      __ push(eax);  // push the result
      return;
    }
    case Token::INSTANCEOF: {
      Load(left);
      Load(right);
      __ InvokeBuiltin(Builtins::INSTANCE_OF, CALL_FUNCTION);
      __ push(eax);  // push the result
      return;
    }
    default:
      UNREACHABLE();
  }

  // Optimize for the case where (at least) one of the expressions
  // is a literal small integer.
  if (left->AsLiteral() != NULL && left->AsLiteral()->handle()->IsSmi()) {
    Load(right);
    SmiComparison(ReverseCondition(cc), left->AsLiteral()->handle(), strict);
    return;
  }
  if (right->AsLiteral() != NULL && right->AsLiteral()->handle()->IsSmi()) {
    Load(left);
    SmiComparison(cc, right->AsLiteral()->handle(), strict);
    return;
  }

  Load(left);
  Load(right);
  Comparison(cc, strict);
}


void Ia32CodeGenerator::RecordStatementPosition(Node* node) {
  if (FLAG_debug_info) {
    int pos = node->statement_pos();
    if (pos != kNoPosition) {
      __ RecordStatementPosition(pos);
    }
  }
}


void Ia32CodeGenerator::EnterJSFrame() {
  __ push(ebp);
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  __ push(esi);
  __ push(edi);

  // Clear the function slot when generating debug code.
  if (FLAG_debug_code) {
    __ Set(edi, Immediate(reinterpret_cast<int>(kZapValue)));
  }
}


void Ia32CodeGenerator::ExitJSFrame() {
  // Record the location of the JS exit code for patching when setting
  // break point.
  __ RecordJSReturn();

  // Avoid using the leave instruction here, because it is too
  // short. We need the return sequence to be a least the size of a
  // call instruction to support patching the exit code in the
  // debugger. See VisitReturnStatement for the full return sequence.
  __ mov(esp, Operand(ebp));
  __ pop(ebp);
}


#undef __
#define __  masm->


void CEntryStub::GenerateReserveCParameterSpace(MacroAssembler* masm,
                                                int num_parameters) {
  if (num_parameters > 0) {
    __ sub(Operand(esp), Immediate(num_parameters * kPointerSize));
  }
  // OS X activation frames are 16 byte-aligned
  // (see "Mac OS X ABI Function Call Guide").
  const int kFrameAlignment = 16;
  ASSERT(IsPowerOf2(kFrameAlignment));
  __ and_(esp, -kFrameAlignment);
}


void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  ASSERT(StackHandlerConstants::kSize == 6 * kPointerSize);  // adjust this code
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));
  __ mov(ecx, Operand(edx, -1 * kPointerSize));  // get next in chain
  __ mov(Operand::StaticVariable(handler_address), ecx);
  __ mov(esp, Operand(edx));
  __ pop(edi);
  __ pop(ebp);
  __ pop(edx);  // remove code pointer
  __ pop(edx);  // remove state

  // Before returning we restore the context from the frame pointer if not NULL.
  // The frame pointer is NULL in the exception handler of a JS entry frame.
  __ xor_(esi, Operand(esi));  // tentatively set context pointer to NULL
  Label skip;
  __ cmp(ebp, 0);
  __ j(equal, &skip, not_taken);
  __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  __ bind(&skip);

  __ ret(0);
}


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_out_of_memory_exception,
                              bool do_gc,
                              bool do_restore) {
  // eax: result parameter for PerformGC, if any
  // ebx: pointer to C function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // edi: number of arguments  (C callee-saved)

  if (do_gc) {
    __ mov(Operand(esp, 0 * kPointerSize), eax);  // Result.
    __ call(FUNCTION_ADDR(Runtime::PerformGC), runtime_entry);
  }

  // Call C function.
  __ lea(eax,
         Operand(ebp, edi, times_4, StandardFrameConstants::kCallerSPOffset));
  __ mov(Operand(esp, 0 * kPointerSize), edi);  // argc.
  __ mov(Operand(esp, 1 * kPointerSize), eax);  // argv.
  __ call(Operand(ebx));
  // Result is in eax or edx:eax - do not destroy these registers!

  // Check for failure result.
  Label failure_returned;
  ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
  __ lea(ecx, Operand(eax, 1));
  // Lower 2 bits of ecx are 0 iff eax has failure tag.
  __ test(ecx, Immediate(kFailureTagMask));
  __ j(zero, &failure_returned, not_taken);

  // Restore number of arguments to ecx and clear top frame.
  __ mov(ecx, Operand(edi));
  ExternalReference c_entry_fp_address(Top::k_c_entry_fp_address);
  __ mov(Operand::StaticVariable(c_entry_fp_address), Immediate(0));

  // Restore the memory copy of the registers by digging them out from
  // the stack.
  if (do_restore) {
    // Ok to clobber ebx and edi - function pointer and number of arguments not
    // needed anymore.
    const int kCallerSavedSize = kNumJSCallerSaved * kPointerSize;
    int kOffset = ExitFrameConstants::kDebugMarkOffset - kCallerSavedSize;
    __ lea(ebx, Operand(ebp, kOffset));
    __ CopyRegistersFromStackToMemory(ebx, edi, kJSCallerSaved);
  }

  // Exit C frame.
  __ lea(esp, Operand(ebp, -1 * kPointerSize));
  __ pop(ebx);
  __ pop(ebp);

  // Restore current context from top and clear it in debug mode.
  ExternalReference context_address(Top::k_context_address);
  __ mov(esi, Operand::StaticVariable(context_address));
  if (kDebug) {
    __ mov(Operand::StaticVariable(context_address), Immediate(0));
  }

  // Pop arguments from caller's stack and return.
  __ pop(ebx);  // Ok to clobber ebx - function pointer not needed anymore.
  __ lea(esp, Operand(esp, ecx, times_4, +1 * kPointerSize));  // +1 ~ receiver.
  __ push(ebx);
  __ ret(0);

  // Handling of Failure.
  __ bind(&failure_returned);

  Label retry;
  // If the returned exception is RETRY_AFTER_GC continue at retry label
  ASSERT(Failure::RETRY_AFTER_GC == 0);
  __ test(eax, Immediate(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
  __ j(zero, &retry, taken);

  Label continue_exception;
  // If the returned failure is EXCEPTION then promote Top::pending_exception().
  __ cmp(eax, reinterpret_cast<int32_t>(Failure::Exception()));
  __ j(not_equal, &continue_exception);

  // Retrieve the pending exception and clear the variable.
  ExternalReference pending_exception_address(Top::k_pending_exception_address);
  __ mov(eax, Operand::StaticVariable(pending_exception_address));
  __ mov(edx,
         Operand::StaticVariable(ExternalReference::the_hole_value_location()));
  __ mov(Operand::StaticVariable(pending_exception_address), edx);

  __ bind(&continue_exception);
  // Special handling of out of memory exception.
  __ cmp(eax, reinterpret_cast<int32_t>(Failure::OutOfMemoryException()));
  __ j(equal, throw_out_of_memory_exception);

  // Handle normal exception.
  __ jmp(throw_normal_exception);

  // Retry.
  __ bind(&retry);
}


void CEntryStub::GenerateThrowOutOfMemory(MacroAssembler* masm) {
  // Fetch top stack handler.
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));

  // Unwind the handlers until the ENTRY handler is found.
  Label loop, done;
  __ bind(&loop);
  // Load the type of the current stack handler.
  const int kStateOffset = StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kStateOffset;
  __ cmp(Operand(edx, kStateOffset), Immediate(StackHandler::ENTRY));
  __ j(equal, &done);
  // Fetch the next handler in the list.
  const int kNextOffset = StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kNextOffset;
  __ mov(edx, Operand(edx, kNextOffset));
  __ jmp(&loop);
  __ bind(&done);

  // Set the top handler address to next handler past the current ENTRY handler.
  __ mov(eax, Operand(edx, kNextOffset));
  __ mov(Operand::StaticVariable(handler_address), eax);

  // Set external caught exception to false.
  __ mov(eax, false);
  ExternalReference external_caught(Top::k_external_caught_exception_address);
  __ mov(Operand::StaticVariable(external_caught), eax);

  // Set pending exception and eax to out of memory exception.
  __ mov(eax, reinterpret_cast<int32_t>(Failure::OutOfMemoryException()));
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ mov(Operand::StaticVariable(pending_exception), eax);

  // Restore the stack to the address of the ENTRY handler
  __ mov(esp, Operand(edx));

  // Clear the context pointer;
  __ xor_(esi, Operand(esi));

  // Restore registers from handler.
  __ pop(edi);  // PP
  __ pop(ebp);  // FP
  __ pop(edx);  // Code
  __ pop(edx);  // State

  __ ret(0);
}


void CEntryStub::GenerateBody(MacroAssembler* masm, bool is_debug_break) {
  // eax: number of arguments
  // ebx: pointer to C function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // esi: current context (C callee-saved)
  // edi: caller's parameter pointer pp  (C callee-saved)

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  // Enter C frame.
  // Here we make the following assumptions and use them when setting
  // up the top-most Frame. Adjust the code if these assumptions
  // change.
  ASSERT(ExitFrameConstants::kPPDisplacement == +2 * kPointerSize);
  ASSERT(ExitFrameConstants::kCallerPCOffset == +1 * kPointerSize);
  ASSERT(ExitFrameConstants::kCallerFPOffset ==  0 * kPointerSize);
  ASSERT(ExitFrameConstants::kSPOffset  == -2 * kPointerSize);
  __ push(ebp);  // caller fp
  __ mov(ebp, Operand(esp));  // C entry fp
  __ push(ebx);  // C function
  __ push(Immediate(0));  // saved entry sp, set before call
  __ push(Immediate(is_debug_break ? 1 : 0));

  // Remember top frame.
  ExternalReference c_entry_fp(Top::k_c_entry_fp_address);
  ExternalReference context_address(Top::k_context_address);
  __ mov(Operand::StaticVariable(c_entry_fp), ebp);
  __ mov(Operand::StaticVariable(context_address), esi);

  if (is_debug_break) {
    // Save the state of all registers to the stack from the memory
    // location.

    // TODO(1243899): This should be symmetric to
    // CopyRegistersFromStackToMemory() but it isn't! esp is assumed
    // correct here, but computed for the other call. Very error
    // prone! FIX THIS.  Actually there are deeper problems with
    // register saving than this assymetry (see the buganizer report
    // associated with this issue).
    __ PushRegistersFromMemory(kJSCallerSaved);
  }

  // Move number of arguments (argc) into callee-saved register. Note
  // that edi is only available after remembering the top frame.
  __ mov(edi, Operand(eax));

  // Allocate stack space for 2 arguments (argc, argv).
  GenerateReserveCParameterSpace(masm, 2);
  __ mov(Operand(ebp, ExitFrameConstants::kSPOffset), esp);  // save entry sp

  // eax: result parameter for PerformGC, if any (setup below)
  // ebx: pointer to builtin function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // edi: number of arguments  (C callee-saved)

  Label entry;
  __ bind(&entry);

  Label throw_out_of_memory_exception;
  Label throw_normal_exception;

#ifdef DEBUG
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0, NEW_SPACE);
    __ mov(Operand(eax), Immediate(reinterpret_cast<int32_t>(failure)));
  }
  GenerateCore(masm, &throw_normal_exception,
               &throw_out_of_memory_exception,
               FLAG_gc_greedy,
               is_debug_break);
#else
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               false,
               is_debug_break);
#endif
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               true,
               is_debug_break);

  __ bind(&throw_out_of_memory_exception);
  GenerateThrowOutOfMemory(masm);
  // control flow for generated will not return.

  __ bind(&throw_normal_exception);
  GenerateThrowTOS(masm);
}


void JSEntryStub::GenerateBody(MacroAssembler* masm, bool is_construct) {
  Label invoke, exit;

  // Setup frame.
  __ push(ebp);
  __ mov(ebp, Operand(esp));

  // Save callee-saved registers (C calling conventions).
  int marker = is_construct ? StackFrame::ENTRY_CONSTRUCT : StackFrame::ENTRY;
  // Push something that is not an arguments adaptor.
  __ push(Immediate(~ArgumentsAdaptorFrame::SENTINEL));
  __ push(Immediate(Smi::FromInt(marker)));  // @ function offset
  __ push(edi);
  __ push(esi);
  __ push(ebx);

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp(Top::k_c_entry_fp_address);
  __ push(Operand::StaticVariable(c_entry_fp));

  // Call a faked try-block that does the invoke.
  __ call(&invoke);

  // Caught exception: Store result (exception) in the pending
  // exception field in the JSEnv and return a failure sentinel.
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ mov(Operand::StaticVariable(pending_exception), eax);
  __ mov(eax, Handle<Failure>(Failure::Exception()));
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushTryHandler(IN_JS_ENTRY, JS_ENTRY_HANDLER);
  __ push(eax);  // flush TOS

  // Clear any pending exceptions.
  __ mov(edx,
         Operand::StaticVariable(ExternalReference::the_hole_value_location()));
  __ mov(Operand::StaticVariable(pending_exception), edx);

  // Fake a receiver (NULL).
  __ push(Immediate(0));  // receiver

  // Invoke the function by calling through JS entry trampoline
  // builtin and pop the faked function when we return. Notice that we
  // cannot store a reference to the trampoline code directly in this
  // stub, because the builtin stubs may not have been generated yet.
  if (is_construct) {
    ExternalReference construct_entry(Builtins::JSConstructEntryTrampoline);
    __ mov(Operand(edx), Immediate(construct_entry));
  } else {
    ExternalReference entry(Builtins::JSEntryTrampoline);
    __ mov(Operand(edx), Immediate(entry));
  }
  __ mov(edx, Operand(edx, 0));  // deref address
  __ lea(edx, FieldOperand(edx, Code::kHeaderSize));
  __ call(Operand(edx));

  // Unlink this frame from the handler chain.
  __ pop(Operand::StaticVariable(ExternalReference(Top::k_handler_address)));
  // Pop next_sp.
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));

  // Restore the top frame descriptor from the stack.
  __ bind(&exit);
  __ pop(Operand::StaticVariable(ExternalReference(Top::k_c_entry_fp_address)));

  // Restore callee-saved registers (C calling conventions).
  __ pop(ebx);
  __ pop(esi);
  __ pop(edi);
  __ add(Operand(esp), Immediate(2 * kPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ pop(ebp);
  __ ret(0);
}


#undef __


// -----------------------------------------------------------------------------
// CodeGenerator interfaces

// MakeCode() is just a wrapper for CodeGenerator::MakeCode()
// so we don't have to expose the entire CodeGenerator class in
// the .h file.
Handle<Code> CodeGenerator::MakeCode(FunctionLiteral* fun,
                                     Handle<Script> script,
                                     bool is_eval) {
  Handle<Code> code = Ia32CodeGenerator::MakeCode(fun, script, is_eval);
  if (!code.is_null()) {
    Counters::total_compiled_code_size.Increment(code->instruction_size());
  }
  return code;
}


} }  // namespace v8::internal
