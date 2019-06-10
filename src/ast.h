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

#ifndef V8_AST_H_
#define V8_AST_H_

#include "execution.h"
#include "factory.h"
#include "runtime.h"
#include "token.h"
#include "variables.h"
#include "macro-assembler.h"

namespace v8 { namespace internal {

// The abstract syntax tree is an intermediate, light-weight
// representation of the parsed JavaScript code suitable for
// compilation to native code.

// Nodes are allocated in a separate zone, which allows faster
// allocation and constant-time deallocation of the entire syntax
// tree.


// ----------------------------------------------------------------------------
// Nodes of the abstract syntax tree. Only concrete classes are
// enumerated here.

#define NODE_LIST(V)                            \
  V(Block)                                      \
  V(Declaration)                                \
  V(ExpressionStatement)                        \
  V(EmptyStatement)                             \
  V(IfStatement)                                \
  V(ContinueStatement)                          \
  V(BreakStatement)                             \
  V(ReturnStatement)                            \
  V(WithEnterStatement)                         \
  V(WithExitStatement)                          \
  V(SwitchStatement)                            \
  V(LoopStatement)                              \
  V(ForInStatement)                             \
  V(TryCatch)                                   \
  V(TryFinally)                                 \
  V(DebuggerStatement)                          \
  V(FunctionLiteral)                            \
  V(FunctionBoilerplateLiteral)                 \
  V(Conditional)                                \
  V(Slot)                                       \
  V(VariableProxy)                              \
  V(Literal)                                    \
  V(RegExpLiteral)                              \
  V(ObjectLiteral)                              \
  V(ArrayLiteral)                               \
  V(Assignment)                                 \
  V(Throw)                                      \
  V(Property)                                   \
  V(Call)                                       \
  V(CallNew)                                    \
  V(CallRuntime)                                \
  V(UnaryOperation)                             \
  V(CountOperation)                             \
  V(BinaryOperation)                            \
  V(CompareOperation)                           \
  V(ThisFunction)


#define DEF_FORWARD_DECLARATION(type) class type;
NODE_LIST(DEF_FORWARD_DECLARATION)
#undef DEF_FORWARD_DECLARATION


// Typedef only introduced to avoid unreadable code.
// Please do appreciate the required space in "> >".
typedef ZoneList<Handle<String> > ZoneStringList;


class Node: public ZoneObject {
 public:
  Node(): statement_pos_(kNoPosition) { }
  virtual ~Node() { }
  virtual void Accept(Visitor* v) = 0;

  // Type testing & conversion.
  virtual Statement* AsStatement() { return NULL; }
  virtual ExpressionStatement* AsExpressionStatement() { return NULL; }
  virtual EmptyStatement* AsEmptyStatement() { return NULL; }
  virtual Expression* AsExpression() { return NULL; }
  virtual Literal* AsLiteral() { return NULL; }
  virtual Slot* AsSlot() { return NULL; }
  virtual VariableProxy* AsVariableProxy() { return NULL; }
  virtual Property* AsProperty() { return NULL; }
  virtual Call* AsCall() { return NULL; }
  virtual LabelCollector* AsLabelCollector() { return NULL; }
  virtual BreakableStatement* AsBreakableStatement() { return NULL; }
  virtual IterationStatement* AsIterationStatement() { return NULL; }
  virtual UnaryOperation* AsUnaryOperation() { return NULL; }
  virtual BinaryOperation* AsBinaryOperation() { return NULL; }
  virtual Assignment* AsAssignment() { return NULL; }
  virtual FunctionLiteral* AsFunctionLiteral() { return NULL; }

  void set_statement_pos(int statement_pos) { statement_pos_ = statement_pos; }
  int statement_pos() const { return statement_pos_; }

 private:
  int statement_pos_;
};


class Statement: public Node {
 public:
  virtual Statement* AsStatement()  { return this; }
  virtual ReturnStatement* AsReturnStatement() { return NULL; }

  bool IsEmpty() { return AsEmptyStatement() != NULL; }
};


class Expression: public Node {
 public:
  virtual Expression* AsExpression()  { return this; }

  virtual bool IsValidLeftHandSide() { return false; }

  // Mark the expression as being compiled as an expression
  // statement. This is used to transform postfix increments to
  // (faster) prefix increments.
  virtual void MarkAsStatement() { /* do nothing */ }
};


/**
 * A sentinel used during pre parsing that represents some expression
 * that is a valid left hand side without having to actually build
 * the expression.
 */
class ValidLeftHandSideSentinel: public Expression {
 public:
  virtual bool IsValidLeftHandSide() { return true; }
  virtual void Accept(Visitor* v) { UNREACHABLE(); }
  static ValidLeftHandSideSentinel* instance() { return &instance_; }
 private:
  static ValidLeftHandSideSentinel instance_;
};


class BreakableStatement: public Statement {
 public:
  enum Type {
    TARGET_FOR_ANONYMOUS,
    TARGET_FOR_NAMED_ONLY
  };

  // The labels associated with this statement. May be NULL;
  // if it is != NULL, guaranteed to contain at least one entry.
  ZoneStringList* labels() const { return labels_; }

  // Type testing & conversion.
  virtual BreakableStatement* AsBreakableStatement() { return this; }

  // Code generation
  Label* break_target() { return &break_target_; }

  // Used during code generation for restoring the stack when a
  // break/continue crosses a statement that keeps stuff on the stack.
  int break_stack_height() { return break_stack_height_; }
  void set_break_stack_height(int height) { break_stack_height_ = height; }

  // Testers.
  bool is_target_for_anonymous() const { return type_ == TARGET_FOR_ANONYMOUS; }

 protected:
  BreakableStatement(ZoneStringList* labels, Type type)
      : labels_(labels), type_(type) {
    ASSERT(labels == NULL || labels->length() > 0);
  }

 private:
  ZoneStringList* labels_;
  Type type_;
  Label break_target_;
  int break_stack_height_;
};


class Block: public BreakableStatement {
 public:
  Block(ZoneStringList* labels, int capacity, bool is_initializer_block)
      : BreakableStatement(labels, TARGET_FOR_NAMED_ONLY),
        statements_(capacity),
        is_initializer_block_(is_initializer_block) { }

  virtual void Accept(Visitor* v);

  void AddStatement(Statement* statement) { statements_.Add(statement); }

  ZoneList<Statement*>* statements() { return &statements_; }
  bool is_initializer_block() const  { return is_initializer_block_; }

 private:
  ZoneList<Statement*> statements_;
  bool is_initializer_block_;
};


class Declaration: public Node {
 public:
  Declaration(VariableProxy* proxy, Variable::Mode mode, FunctionLiteral* fun)
    : proxy_(proxy),
      mode_(mode),
      fun_(fun) {
    ASSERT(mode == Variable::VAR || mode == Variable::CONST);
    // At the moment there are no "const functions"'s in JavaScript...
    ASSERT(fun == NULL || mode == Variable::VAR);
  }

  virtual void Accept(Visitor* v);

  VariableProxy* proxy() const  { return proxy_; }
  Variable::Mode mode() const  { return mode_; }
  FunctionLiteral* fun() const  { return fun_; }  // may be NULL

 private:
  VariableProxy* proxy_;
  Variable::Mode mode_;
  FunctionLiteral* fun_;
};


class IterationStatement: public BreakableStatement {
 public:
  // Type testing & conversion.
  virtual IterationStatement* AsIterationStatement() { return this; }

  Statement* body() const { return body_; }

  // Code generation
  Label* continue_target()  { return &continue_target_; }

 protected:
  explicit IterationStatement(ZoneStringList* labels)
    : BreakableStatement(labels, TARGET_FOR_ANONYMOUS), body_(NULL) { }

  void Initialize(Statement* body) {
    body_ = body;
  }

 private:
  Statement* body_;
  Label continue_target_;
};


class LoopStatement: public IterationStatement {
 public:
  enum Type { DO_LOOP, FOR_LOOP, WHILE_LOOP };

  LoopStatement(ZoneStringList* labels, Type type)
      : IterationStatement(labels), type_(type), init_(NULL),
        cond_(NULL), next_(NULL) { }

  void Initialize(Statement* init,
                  Expression* cond,
                  Statement* next,
                  Statement* body) {
    ASSERT(init == NULL || type_ == FOR_LOOP);
    ASSERT(next == NULL || type_ == FOR_LOOP);
    IterationStatement::Initialize(body);
    init_ = init;
    cond_ = cond;
    next_ = next;
  }

  virtual void Accept(Visitor* v);

  Type type() const  { return type_; }
  Statement* init() const  { return init_; }
  Expression* cond() const  { return cond_; }
  Statement* next() const  { return next_; }

#ifdef DEBUG
  const char* OperatorString() const;
#endif

 private:
  Type type_;
  Statement* init_;
  Expression* cond_;
  Statement* next_;
};


class ForInStatement: public IterationStatement {
 public:
  explicit ForInStatement(ZoneStringList* labels)
    : IterationStatement(labels), each_(NULL), enumerable_(NULL) { }

  void Initialize(Expression* each, Expression* enumerable, Statement* body) {
    IterationStatement::Initialize(body);
    each_ = each;
    enumerable_ = enumerable;
  }

  virtual void Accept(Visitor* v);

  Expression* each() const { return each_; }
  Expression* enumerable() const { return enumerable_; }

 private:
  Expression* each_;
  Expression* enumerable_;
};


class ExpressionStatement: public Statement {
 public:
  explicit ExpressionStatement(Expression* expression)
      : expression_(expression) { }

  virtual void Accept(Visitor* v);

  // Type testing & conversion.
  virtual ExpressionStatement* AsExpressionStatement() { return this; }

  void set_expression(Expression* e) { expression_ = e; }
  Expression* expression() { return expression_; }

 private:
  Expression* expression_;
};


class ContinueStatement: public Statement {
 public:
  explicit ContinueStatement(IterationStatement* target)
      : target_(target) { }

  virtual void Accept(Visitor* v);

  IterationStatement* target() const  { return target_; }

 private:
  IterationStatement* target_;
};


class BreakStatement: public Statement {
 public:
  explicit BreakStatement(BreakableStatement* target)
      : target_(target) { }

  virtual void Accept(Visitor* v);

  BreakableStatement* target() const  { return target_; }

 private:
  BreakableStatement* target_;
};


class ReturnStatement: public Statement {
 public:
  explicit ReturnStatement(Expression* expression)
      : expression_(expression) { }

  virtual void Accept(Visitor* v);

  // Type testing & conversion.
  virtual ReturnStatement* AsReturnStatement() { return this; }

  Expression* expression() { return expression_; }

 private:
  Expression* expression_;
};


class WithEnterStatement: public Statement {
 public:
  explicit WithEnterStatement(Expression* expression)
      : expression_(expression) { }

  virtual void Accept(Visitor* v);

  Expression* expression() const  { return expression_; }

 private:
  Expression* expression_;
};


class WithExitStatement: public Statement {
 public:
  WithExitStatement() { }

  virtual void Accept(Visitor* v);
};


class CaseClause: public ZoneObject {
 public:
  CaseClause(Expression* label, ZoneList<Statement*>* statements)
      : label_(label), statements_(statements) { }

  bool is_default() const  { return label_ == NULL; }
  Expression* label() const  {
    CHECK(!is_default());
    return label_;
  }
  ZoneList<Statement*>* statements() const  { return statements_; }

 private:
  Expression* label_;
  ZoneList<Statement*>* statements_;
};


class SwitchStatement: public BreakableStatement {
 public:
  explicit SwitchStatement(ZoneStringList* labels)
      : BreakableStatement(labels, TARGET_FOR_ANONYMOUS),
        tag_(NULL), cases_(NULL) { }

  void Initialize(Expression* tag, ZoneList<CaseClause*>* cases) {
    tag_ = tag;
    cases_ = cases;
  }

  virtual void Accept(Visitor* v);

  Expression* tag() const  { return tag_; }
  ZoneList<CaseClause*>* cases() const  { return cases_; }

 private:
  Expression* tag_;
  ZoneList<CaseClause*>* cases_;
};


// If-statements always have non-null references to their then- and
// else-parts. When parsing if-statements with no explicit else-part,
// the parser implicitly creates an empty statement. Use the
// HasThenStatement() and HasElseStatement() functions to check if a
// given if-statement has a then- or an else-part containing code.
class IfStatement: public Statement {
 public:
  IfStatement(Expression* condition,
              Statement* then_statement,
              Statement* else_statement)
      : condition_(condition),
        then_statement_(then_statement),
        else_statement_(else_statement) { }

  virtual void Accept(Visitor* v);

  bool HasThenStatement() const { return !then_statement()->IsEmpty(); }
  bool HasElseStatement() const { return !else_statement()->IsEmpty(); }

  Expression* condition() const { return condition_; }
  Statement* then_statement() const { return then_statement_; }
  Statement* else_statement() const { return else_statement_; }

 private:
  Expression* condition_;
  Statement* then_statement_;
  Statement* else_statement_;
};


// NOTE: LabelCollectors are represented as nodes to fit in the target
// stack in the compiler; this should probably be reworked.
class LabelCollector: public Node {
 public:
  explicit LabelCollector(ZoneList<Label*>* labels) : labels_(labels) { }

  // Adds a label to the collector. The collector stores a pointer not
  // a copy of the label to make binding work, so make sure not to
  // pass in references to something on the stack.
  void AddLabel(Label* label);

  // Virtual behaviour. LabelCollectors are never part of the AST.
  virtual void Accept(Visitor* v) { UNREACHABLE(); }
  virtual LabelCollector* AsLabelCollector() { return this; }

  ZoneList<Label*>* labels() { return labels_; }

 private:
  ZoneList<Label*>* labels_;
};


class TryStatement: public Statement {
 public:
  explicit TryStatement(Block* try_block)
      : try_block_(try_block), escaping_labels_(NULL) { }

  void set_escaping_labels(ZoneList<Label*>* labels) {
    escaping_labels_ = labels;
  }

  Block* try_block() const { return try_block_; }
  ZoneList<Label*>* escaping_labels() const { return escaping_labels_; }

 private:
  Block* try_block_;
  ZoneList<Label*>* escaping_labels_;
};


class TryCatch: public TryStatement {
 public:
  TryCatch(Block* try_block, Expression* catch_var, Block* catch_block)
      : TryStatement(try_block),
        catch_var_(catch_var),
        catch_block_(catch_block) {
    ASSERT(catch_var->AsVariableProxy() != NULL);
  }

  virtual void Accept(Visitor* v);

  Expression* catch_var() const  { return catch_var_; }
  Block* catch_block() const  { return catch_block_; }

 private:
  Expression* catch_var_;
  Block* catch_block_;
};


class TryFinally: public TryStatement {
 public:
  TryFinally(Block* try_block, Expression* finally_var, Block* finally_block)
      : TryStatement(try_block),
        finally_var_(finally_var),
        finally_block_(finally_block) { }

  virtual void Accept(Visitor* v);

  // If the finally block is non-trivial it may be problematic to have
  // extra stuff on the expression stack while evaluating it. The
  // finally variable is used to hold the state instead of storing it
  // on the stack. It may be NULL in which case the state is stored on
  // the stack.
  Expression* finally_var() const { return finally_var_; }

  Block* finally_block() const { return finally_block_; }

 private:
  Expression* finally_var_;
  Block* finally_block_;
};


class DebuggerStatement: public Statement {
 public:
  virtual void Accept(Visitor* v);
};


class EmptyStatement: public Statement {
 public:
  virtual void Accept(Visitor* v);

  // Type testing & conversion.
  virtual EmptyStatement* AsEmptyStatement() { return this; }
};


class Literal: public Expression {
 public:
  explicit Literal(Handle<Object> handle) : handle_(handle) { }

  virtual void Accept(Visitor* v);

  // Type testing & conversion.
  virtual Literal* AsLiteral() { return this; }

  // Check if this literal is identical to the other literal.
  bool IsIdenticalTo(const Literal* other) const {
    return handle_.is_identical_to(other->handle_);
  }

  // Identity testers.
  bool IsNull() const { return handle_.is_identical_to(Factory::null_value()); }
  bool IsTrue() const { return handle_.is_identical_to(Factory::true_value()); }
  bool IsFalse() const {
    return handle_.is_identical_to(Factory::false_value());
  }

  Handle<Object> handle() const { return handle_; }

 private:
  Handle<Object> handle_;
};


// Base class for literals that needs space in the corresponding JSFunction.
class MaterializedLiteral: public Expression {
 public:
  explicit MaterializedLiteral(int literal_index)
      : literal_index_(literal_index) {}
  int literal_index() { return literal_index_; }
 private:
  int literal_index_;
};


// An object literal has a boilerplate object that is used
// for minimizing the work when constructing it at runtime.
class ObjectLiteral: public MaterializedLiteral {
 public:
  // Property is used for passing information
  // about an object literal's properties from the parser
  // to the code generator.
  class Property: public ZoneObject {
   public:

    enum Kind {
      CONSTANT,       // Property with constant value (at compile time).
      COMPUTED,       // Property with computed value (at execution time).
      GETTER, SETTER,  // Property is an accessor function.
      PROTOTYPE       // Property is __proto__.
    };

    Property(Literal* key, Expression* value);
    Property(bool is_getter, FunctionLiteral* value);

    Literal* key() { return key_; }
    Expression* value() { return value_; }
    Kind kind() { return kind_; }

   private:
    Literal* key_;
    Expression* value_;
    Kind kind_;
  };

  ObjectLiteral(Handle<FixedArray> constant_properties,
                Expression* result,
                ZoneList<Property*>* properties,
                int literal_index)
      : MaterializedLiteral(literal_index),
        constant_properties_(constant_properties),
        result_(result),
        properties_(properties) {
  }

  virtual void Accept(Visitor* v);

  Handle<FixedArray> constant_properties() const {
    return constant_properties_;
  }
  Expression* result() const { return result_; }
  ZoneList<Property*>* properties() const { return properties_; }

 private:
  Handle<FixedArray> constant_properties_;
  Expression* result_;
  ZoneList<Property*>* properties_;
};


// Node for capturing a regexp literal.
class RegExpLiteral: public MaterializedLiteral {
 public:
  RegExpLiteral(Handle<String> pattern,
                Handle<String> flags,
                int literal_index)
      : MaterializedLiteral(literal_index),
        pattern_(pattern),
        flags_(flags) {}

  virtual void Accept(Visitor* v);

  Handle<String> pattern() const { return pattern_; }
  Handle<String> flags() const { return flags_; }

 private:
  Handle<String> pattern_;
  Handle<String> flags_;
};

// An array literal has a literals object that is used
// used for minimizing the work when contructing it at runtime.
class ArrayLiteral: public Expression {
 public:
  ArrayLiteral(Handle<FixedArray> literals,
               Expression* result,
               ZoneList<Expression*>* values)
      : literals_(literals), result_(result), values_(values) {
  }

  virtual void Accept(Visitor* v);

  Handle<FixedArray> literals() const { return literals_; }
  Expression* result() const { return result_; }
  ZoneList<Expression*>* values() const { return values_; }

 private:
  Handle<FixedArray> literals_;
  Expression* result_;
  ZoneList<Expression*>* values_;
};


class VariableProxy: public Expression {
 public:
  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual Property* AsProperty() {
    return var_ == NULL ? NULL : var_->AsProperty();
  }
  virtual VariableProxy* AsVariableProxy()  { return this; }

  Variable* AsVariable() {
    return this == NULL || var_ == NULL ? NULL : var_->AsVariable();
  }
  virtual bool IsValidLeftHandSide() {
    return var_ == NULL ? true : var_->IsValidLeftHandSide();
  }
  bool IsVariable(Handle<String> n) {
    return !is_this() && name().is_identical_to(n);
  }

  // If this assertion fails it means that some code has tried to
  // treat the special "this" variable as an ordinary variable with
  // the name "this".
  Handle<String> name() const  { return name_; }
  Variable* var() const  { return var_; }
  UseCount* var_uses()  { return &var_uses_; }
  UseCount* obj_uses()  { return &obj_uses_; }
  bool is_this() const  { return is_this_; }
  bool inside_with() const  { return inside_with_; }

  // Bind this proxy to the variable var.
  void BindTo(Variable* var);

 protected:
  Handle<String> name_;
  Variable* var_;  // resolved variable, or NULL
  bool is_this_;
  bool inside_with_;

  // VariableProxy usage info.
  UseCount var_uses_;  // uses of the variable value
  UseCount obj_uses_;  // uses of the object the variable points to

  VariableProxy(Handle<String> name, bool is_this, bool inside_with);
  explicit VariableProxy(bool is_this);

  friend class Scope;
};


class VariableProxySentinel: public VariableProxy {
 public:
  virtual bool IsValidLeftHandSide() { return !is_this(); }
  static VariableProxySentinel* this_proxy() { return &this_proxy_; }
  static VariableProxySentinel* identifier_proxy() {
    return &identifier_proxy_;
  }

 private:
  explicit VariableProxySentinel(bool is_this) : VariableProxy(is_this) { }
  static VariableProxySentinel this_proxy_;
  static VariableProxySentinel identifier_proxy_;
};


class Slot: public Expression {
 public:
  enum Type {
    // A slot in the parameter section on the stack. index() is
    // the parameter index, counting left-to-right, starting at 0.
    PARAMETER,

    // A slot in the local section on the stack. index() is
    // the variable index in the stack frame, starting at 0.
    LOCAL,

    // An indexed slot in a heap context. index() is the
    // variable index in the context object on the heap,
    // starting at 0. var()->scope() is the corresponding
    // scope.
    CONTEXT,

    // A named slot in a heap context. var()->name() is the
    // variable name in the context object on the heap,
    // with lookup starting at the current context. index()
    // is invalid.
    LOOKUP,

    // A property in the global object. var()->name() is
    // the property name.
    GLOBAL
  };

  Slot(Variable* var, Type type, int index)
    : var_(var), type_(type), index_(index) {
    ASSERT(var != NULL);
  }

  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual Slot* AsSlot()  { return this; }

  // Accessors
  Variable* var() const  { return var_; }
  Type type() const  { return type_; }
  int index() const  { return index_; }

 private:
  Variable* var_;
  Type type_;
  int index_;
};


class Property: public Expression {
 public:
  Property(Expression* obj, Expression* key, int pos)
      : obj_(obj), key_(key), pos_(pos) { }

  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual Property* AsProperty() { return this; }

  virtual bool IsValidLeftHandSide() { return true; }

  Expression* obj() const { return obj_; }
  Expression* key() const { return key_; }
  int position() const { return pos_; }

  // Returns a property singleton property access on 'this'.  Used
  // during preparsing.
  static Property* this_property() { return &this_property_; }

 private:
  Expression* obj_;
  Expression* key_;
  int pos_;

  // Dummy property used during preparsing
  static Property this_property_;
};


class Call: public Expression {
 public:
  Call(Expression* expression,
       ZoneList<Expression*>* arguments,
       bool is_eval,
       int pos)
      : expression_(expression),
        arguments_(arguments),
        is_eval_(is_eval),
        pos_(pos) { }

  virtual void Accept(Visitor* v);

  // Type testing and conversion.
  virtual Call* AsCall() { return this; }

  Expression* expression() const { return expression_; }
  ZoneList<Expression*>* arguments() const { return arguments_; }
  bool is_eval()  { return is_eval_; }
  int position() { return pos_; }

  static Call* sentinel() { return &sentinel_; }

 private:
  Expression* expression_;
  ZoneList<Expression*>* arguments_;
  bool is_eval_;
  int pos_;

  static Call sentinel_;
};


class CallNew: public Call {
 public:
  CallNew(Expression* expression, ZoneList<Expression*>* arguments, int pos)
      : Call(expression, arguments, false, pos) { }

  virtual void Accept(Visitor* v);
};


// The CallRuntime class does not represent any official JavaScript
// language construct. Instead it is used to call a C or JS function
// with a set of arguments. This is used from the builtins that are
// implemented in JavaScript (see "v8natives.js").
class CallRuntime: public Expression {
 public:
  CallRuntime(Handle<String> name,
              Runtime::Function* function,
              ZoneList<Expression*>* arguments)
      : name_(name), function_(function), arguments_(arguments) { }

  virtual void Accept(Visitor* v);

  Handle<String> name() const { return name_; }
  Runtime::Function* function() const { return function_; }
  ZoneList<Expression*>* arguments() const { return arguments_; }

 private:
  Handle<String> name_;
  Runtime::Function* function_;
  ZoneList<Expression*>* arguments_;
};


class UnaryOperation: public Expression {
 public:
  UnaryOperation(Token::Value op, Expression* expression)
    : op_(op), expression_(expression) {
    ASSERT(Token::IsUnaryOp(op));
  }

  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual UnaryOperation* AsUnaryOperation() { return this; }

  Token::Value op() const { return op_; }
  Expression* expression() const { return expression_; }

 private:
  Token::Value op_;
  Expression* expression_;
};


class BinaryOperation: public Expression {
 public:
  BinaryOperation(Token::Value op, Expression* left, Expression* right)
    : op_(op), left_(left), right_(right) {
    ASSERT(Token::IsBinaryOp(op));
  }

  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual BinaryOperation* AsBinaryOperation() { return this; }

  // True iff the result can be safely overwritten (to avoid allocation).
  // False for operations that can return one of their operands.
  bool ResultOverwriteAllowed() {
    switch (op_) {
      case Token::COMMA:
      case Token::OR:
      case Token::AND:
        return false;
      case Token::BIT_OR:
      case Token::BIT_XOR:
      case Token::BIT_AND:
      case Token::SHL:
      case Token::SAR:
      case Token::SHR:
      case Token::ADD:
      case Token::SUB:
      case Token::MUL:
      case Token::DIV:
      case Token::MOD:
        return true;
      default:
        UNREACHABLE();
    }
    return false;
  }

  Token::Value op() const { return op_; }
  Expression* left() const { return left_; }
  Expression* right() const { return right_; }

 private:
  Token::Value op_;
  Expression* left_;
  Expression* right_;
};


class CountOperation: public Expression {
 public:
  CountOperation(bool is_prefix, Token::Value op, Expression* expression)
    : is_prefix_(is_prefix), op_(op), expression_(expression) {
    ASSERT(Token::IsCountOp(op));
  }

  virtual void Accept(Visitor* v);

  bool is_prefix() const { return is_prefix_; }
  bool is_postfix() const { return !is_prefix_; }
  Token::Value op() const { return op_; }
  Expression* expression() const { return expression_; }

  virtual void MarkAsStatement() { is_prefix_ = true; }

 private:
  bool is_prefix_;
  Token::Value op_;
  Expression* expression_;
};


class CompareOperation: public Expression {
 public:
  CompareOperation(Token::Value op, Expression* left, Expression* right)
    : op_(op), left_(left), right_(right) {
    ASSERT(Token::IsCompareOp(op));
  }

  virtual void Accept(Visitor* v);

  Token::Value op() const { return op_; }
  Expression* left() const { return left_; }
  Expression* right() const { return right_; }

 private:
  Token::Value op_;
  Expression* left_;
  Expression* right_;
};


class Conditional: public Expression {
 public:
  Conditional(Expression* condition,
              Expression* then_expression,
              Expression* else_expression)
      : condition_(condition),
        then_expression_(then_expression),
        else_expression_(else_expression) { }

  virtual void Accept(Visitor* v);

  Expression* condition() const { return condition_; }
  Expression* then_expression() const { return then_expression_; }
  Expression* else_expression() const { return else_expression_; }

 private:
  Expression* condition_;
  Expression* then_expression_;
  Expression* else_expression_;
};


class Assignment: public Expression {
 public:
  Assignment(Token::Value op, Expression* target, Expression* value, int pos)
    : op_(op), target_(target), value_(value), pos_(pos) {
    ASSERT(Token::IsAssignmentOp(op));
  }

  virtual void Accept(Visitor* v);
  virtual Assignment* AsAssignment() { return this; }

  Token::Value binary_op() const;

  Token::Value op() const { return op_; }
  Expression* target() const { return target_; }
  Expression* value() const { return value_; }
  int position() { return pos_; }

 private:
  Token::Value op_;
  Expression* target_;
  Expression* value_;
  int pos_;
};


class Throw: public Expression {
 public:
  Throw(Expression* exception, int pos)
      : exception_(exception), pos_(pos) {}

  virtual void Accept(Visitor* v);
  Expression* exception() const { return exception_; }
  int position() const { return pos_; }

 private:
  Expression* exception_;
  int pos_;
};


class FunctionLiteral: public Expression {
 public:
  FunctionLiteral(Handle<String> name,
                  Scope* scope,
                  ZoneList<Statement*>* body,
                  int materialized_literal_count,
                  int expected_property_count,
                  int num_parameters,
                  int start_position,
                  int end_position,
                  bool is_expression)
      : name_(name),
        scope_(scope),
        body_(body),
        materialized_literal_count_(materialized_literal_count),
        expected_property_count_(expected_property_count),
        num_parameters_(num_parameters),
        start_position_(start_position),
        end_position_(end_position),
        is_expression_(is_expression),
        function_token_position_(kNoPosition) {
  }

  virtual void Accept(Visitor* v);

  // Type testing & conversion
  virtual FunctionLiteral* AsFunctionLiteral()  { return this; }

  Handle<String> name() const  { return name_; }
  Scope* scope() const  { return scope_; }
  ZoneList<Statement*>* body() const  { return body_; }
  void set_function_token_position(int pos) { function_token_position_ = pos; }
  int function_token_position() const { return function_token_position_; }
  int start_position() const { return start_position_; }
  int end_position() const { return end_position_; }
  bool is_expression() const { return is_expression_; }

  int materialized_literal_count() { return materialized_literal_count_; }
  int expected_property_count() { return expected_property_count_; }
  int num_parameters() { return num_parameters_; }

  bool AllowsLazyCompilation();

 private:
  Handle<String> name_;
  Scope* scope_;
  ZoneList<Statement*>* body_;
  int materialized_literal_count_;
  int expected_property_count_;
  int num_parameters_;
  int start_position_;
  int end_position_;
  bool is_expression_;
  int function_token_position_;
};


class FunctionBoilerplateLiteral: public Expression {
 public:
  explicit FunctionBoilerplateLiteral(Handle<JSFunction> boilerplate)
      : boilerplate_(boilerplate) {
    ASSERT(boilerplate->IsBoilerplate());
  }

  Handle<JSFunction> boilerplate() const { return boilerplate_; }

  virtual void Accept(Visitor* v);

 private:
  Handle<JSFunction> boilerplate_;
};


class ThisFunction: public Expression {
 public:
  virtual void Accept(Visitor* v);
};


// ----------------------------------------------------------------------------
// Basic visitor
// - leaf node visitors are abstract.

class Visitor BASE_EMBEDDED {
 public:
  Visitor() : stack_overflow_(false) { }
  virtual ~Visitor() { }

  // Dispatch
  void Visit(Node* node) { node->Accept(this); }

  // Iteration
  virtual void VisitStatements(ZoneList<Statement*>* statements);
  virtual void VisitExpressions(ZoneList<Expression*>* expressions);

  // Stack overflow tracking support.
  bool HasStackOverflow() const { return stack_overflow_; }
  bool CheckStackOverflow() {
    if (stack_overflow_) return true;
    StackLimitCheck check;
    if (!check.HasOverflowed()) return false;
    return (stack_overflow_ = true);
  }

  // If a stack-overflow exception is encountered when visiting a
  // node, calling SetStackOverflow will make sure that the visitor
  // bails out without visiting more nodes.
  void SetStackOverflow() { stack_overflow_ = true; }


  // Individual nodes
#define DEF_VISIT(type)                         \
  virtual void Visit##type(type* node) = 0;
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

 private:
  bool stack_overflow_;
};


} }  // namespace v8::internal

#endif  // V8_AST_H_
