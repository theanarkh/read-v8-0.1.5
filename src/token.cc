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

#include "token.h"

namespace v8 { namespace internal {

#ifdef DEBUG
#define T(name, string, precedence) #name,
const char* Token::name_[NUM_TOKENS] = {
  TOKEN_LIST(T, T, IGNORE_TOKEN)
  /*
    EOS, LPAREN, RPAREN, LBRACK, RBRACK, LBRACE, RBRACE, COLON, SEMICOLON, PERIOD, CONDITIONAL, INC, DEC, INIT_VAR, INIT_CONST, ASSIGN, ASSIGN_BIT_OR, ASSIGN_BIT_XOR, ASSIGN_BIT_AND, ASSIGN_SHL, ASSIGN_SAR, ASSIGN_SHR, ASSIGN_ADD, ASSIGN_SUB, ASSIGN_MUL, ASSIGN_DIV, ASSIGN_MOD, COMMA, OR, AND, BIT_OR, BIT_XOR, BIT_AND, SHL, SAR, SHR, ADD, SUB, MUL, DIV, MOD, EQ, NE, EQ_STRICT, NE_STRICT, LT, GT, LTE, GTE, INSTANCEOF, IN, NOT, BIT_NOT, DELETE, TYPEOF, VOID, BREAK, CASE, CATCH, CONTINUE, DEBUGGER, DEFAULT, DO, ELSE, FINALLY, FOR, FUNCTION, IF, NEW, RETURN, SWITCH, THIS, THROW, TRY, VAR, WHILE, WITH, CONST, NATIVE, NULL_LITERAL, TRUE_LITERAL, FALSE_LITERAL, NUMBER, STRING, IDENTIFIER, ILLEGAL, COMMENT,
    NUM_TOKENS
  */
};
#undef T
#endif


#define T(name, string, precedence) string,
const char* Token::string_[NUM_TOKENS] = {
  /*
    TOKEN_LIST宏展开后变成
      T(EOS, "EOS", 0)  
      ...
      然后T宏展开后变成
      "EOS"
       ...
       F开头的会被忽略，因为F等于IGNORE_TOKEN
  */
  TOKEN_LIST(T, T, IGNORE_TOKEN)
  /*
    "EOS", "(", ")", "[", "]", "{", "}", ":", ";", ".", "?", "++", "--", "=init_var", "=init_const", "=", "|=", "^=", "&=", "<<=", ">>=", ">>>=", "+=", "-=", "*=", "/=", "%=", ",", "||", "&&", "|", "^", "&", "<<", ">>", ">>>", "+", "-", "*", "/", "%", "==", "!=", "===", "!==", "<", ">", "<=", ">=", "instanceof", "in", "!", "~", "delete", "typeof", "void", "break", "case", "catch", "continue", "debugger", "default", "do", "else", "finally", "for", "function", "if", "new", "return", "switch", "this", "throw", "try", "var", "while", "with", "const", "native", "null", "true", "false", NULL, NULL, NULL, "ILLEGAL", NULL,
  */
};
#undef T


#define T(name, string, precedence) precedence,
int8_t Token::precedence_[NUM_TOKENS] = {
    /*
    TOKEN_LIST宏展开后变成
      T(EOS, "EOS", 0)  
      ...
      然后T宏展开后变成
      0
      ...
      F开头的会被忽略，因为F等于IGNORE_TOKEN
    */
  TOKEN_LIST(T, T, IGNORE_TOKEN)
  /*
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 4, 5, 6, 7, 8, 11, 11, 11, 12, 12, 13, 13, 13, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  */
};
#undef T


// A perfect (0 collision) hash table of keyword token values.

// larger N will reduce the number of collisions (power of 2 for fast %)
const unsigned int N = 128;
// make this small since we have <= 256 tokens
static uint8_t Hashtable[N];
static bool IsInitialized = false;

// 哈希算法
static unsigned int Hash(const char* s) {
  // The following constants have been found using trial-and-error. If the
  // keyword set changes, they may have to be recomputed (make them flags
  // and play with the flag values). Increasing N is the simplest way to
  // reduce the number of collisions.

  // we must use at least 4 or more chars ('const' and 'continue' share
  // 'con')
  const unsigned int L = 5;
  // smaller S tend to reduce the number of collisions
  const unsigned int S = 4;
  // make this a prime, or at least an odd number
  const unsigned int M = 3;

  unsigned int h = 0;
  for (unsigned int i = 0; s[i] != '\0' && i < L; i++) {
    h += (h << S) + s[i];
  }
  // unsigned int % by a power of 2 (otherwise this will not be a bit mask)
  return h * M % N;
}

// 查找str字符串
Token::Value Token::Lookup(const char* str) {
  ASSERT(IsInitialized);
  // Hashtable[Hash(str)]得到str在string_中的索引
  Value k = static_cast<Value>(Hashtable[Hash(str)]);
  // 得到对应的字符串
  const char* s = string_[k];
  ASSERT(s != NULL || k == IDENTIFIER);
  // 对比
  if (s == NULL || strcmp(s, str) == 0) {
    return k;
  }
  return IDENTIFIER;
}


#ifdef DEBUG
// We need this function because C++ doesn't allow the expression
// NULL == NULL, which is a result of macro expansion below. What
// the hell?
static bool IsNull(const char* s) {
  return s == NULL;
}
#endif

// 建立哈希表，用于Lookup查找
void Token::Initialize() {
  if (IsInitialized) return;

  // A list of all keywords, terminated by ILLEGAL.
#define T(name, string, precedence) name,
  static Value keyword[] = {
    /*
    TOKEN_LIST宏展开后变成
      T(EOS, "EOS", 0)  
      ...
      然后T宏展开后变成
      EOS
      ...
      但是只有K开头，在宏展开后会保留，因为T和F都等于IGNORE_TOKEN
    */
    TOKEN_LIST(IGNORE_TOKEN, T, IGNORE_TOKEN)
    ILLEGAL
    /*
      INSTANCEOF, IN, DELETE, TYPEOF, VOID, BREAK, CASE, CATCH, CONTINUE, DEBUGGER, DEFAULT, DO, ELSE, FINALLY, FOR, FUNCTION, IF, NEW, RETURN, SWITCH, THIS, THROW, TRY, VAR, WHILE, WITH, CONST, NATIVE, NULL_LITERAL, TRUE_LITERAL, FALSE_LITERAL,
      ILLEGAL
  
    */
  };
#undef T

  // Assert that the keyword array contains the 25 keywords, 3 future
  // reserved words (const, debugger, and native), and the 3 named literals
  // defined by ECMA-262 standard.
  ASSERT(ARRAY_SIZE(keyword) == 25 + 3 + 3 + 1);  // +1 for ILLEGAL sentinel

  // Initialize Hashtable.
  ASSERT(NUM_TOKENS <= 256);  // Hashtable contains uint8_t elements
  // 初始化哈希表
  for (unsigned int i = 0; i < N; i++) {
    Hashtable[i] = IDENTIFIER;
  }

  // Insert all keywords into Hashtable.
  int collisions = 0;

  for (int i = 0; keyword[i] != ILLEGAL; i++) {
    Value k = keyword[i];
    /*
      string_的内容和Value的枚举范围是一一对应的，keyword是他们的子集，
      即string_里包括关键字和非关键字（T和K），keyword包括了关键字的在string_中的索引,
      string_[k]是一个字符串
    */
    unsigned int h = Hash(string_[k]);
    //不等于IDENTIFIER说明已经赋过值,即冲突了
    if (Hashtable[h] != IDENTIFIER) collisions++;
    // 保存哈希值到关键字索引的映射
    Hashtable[h] = k;
  }

  if (collisions > 0) {
    PrintF("%d collisions in keyword hashtable\n", collisions);
    FATAL("Fix keyword lookup!");
  }

  IsInitialized = true;

  // Verify hash table.
#define T(name, string, precedence) \
  ASSERT(IsNull(string) || Lookup(string) == IDENTIFIER);

#define K(name, string, precedence) \
  ASSERT(Lookup(string) == name);

  TOKEN_LIST(T, K, IGNORE_TOKEN)
/*
  宏展开后
  ASSERT(IsNull("EOS") || Lookup("EOS") == IDENTIFIER); ASSERT(IsNull("(") || Lookup("(") == IDENTIFIER); ASSERT(IsNull(")") || Lookup(")") == IDENTIFIER); ASSERT(IsNull("[") || Lookup("[") == IDENTIFIER); ASSERT(IsNull("]") || Lookup("]") == IDENTIFIER); ASSERT(IsNull("{") || Lookup("{") == IDENTIFIER); ASSERT(IsNull("}") || Lookup("}") == IDENTIFIER); ASSERT(IsNull(":") || Lookup(":") == IDENTIFIER); ASSERT(IsNull(";") || Lookup(";") == IDENTIFIER); ASSERT(IsNull(".") || Lookup(".") == IDENTIFIER); ASSERT(IsNull("?") || Lookup("?") == IDENTIFIER); ASSERT(IsNull("++") || Lookup("++") == IDENTIFIER); ASSERT(IsNull("--") || Lookup("--") == IDENTIFIER); ASSERT(IsNull("=init_var") || Lookup("=init_var") == IDENTIFIER); ASSERT(IsNull("=init_const") || Lookup("=init_const") == IDENTIFIER); ASSERT(IsNull("=") || Lookup("=") == IDENTIFIER); ASSERT(IsNull("|=") || Lookup("|=") == IDENTIFIER); ASSERT(IsNull("^=") || Lookup("^=") == IDENTIFIER); ASSERT(IsNull("&=") || Lookup("&=") == IDENTIFIER); ASSERT(IsNull("<<=") || Lookup("<<=") == IDENTIFIER); ASSERT(IsNull(">>=") || Lookup(">>=") == IDENTIFIER); ASSERT(IsNull(">>>=") || Lookup(">>>=") == IDENTIFIER); ASSERT(IsNull("+=") || Lookup("+=") == IDENTIFIER); ASSERT(IsNull("-=") || Lookup("-=") == IDENTIFIER); ASSERT(IsNull("*=") || Lookup("*=") == IDENTIFIER); ASSERT(IsNull("/=") || Lookup("/=") == IDENTIFIER); ASSERT(IsNull("%=") || Lookup("%=") == IDENTIFIER); ASSERT(IsNull(",") || Lookup(",") == IDENTIFIER); ASSERT(IsNull("||") || Lookup("||") == IDENTIFIER); ASSERT(IsNull("&&") || Lookup("&&") == IDENTIFIER); ASSERT(IsNull("|") || Lookup("|") == IDENTIFIER); ASSERT(IsNull("^") || Lookup("^") == IDENTIFIER); ASSERT(IsNull("&") || Lookup("&") == IDENTIFIER); ASSERT(IsNull("<<") || Lookup("<<") == IDENTIFIER); ASSERT(IsNull(">>") || Lookup(">>") == IDENTIFIER); ASSERT(IsNull(">>>") || Lookup(">>>") == IDENTIFIER); ASSERT(IsNull("+") || Lookup("+") == IDENTIFIER); ASSERT(IsNull("-") || Lookup("-") == IDENTIFIER); ASSERT(IsNull("*") || Lookup("*") == IDENTIFIER); ASSERT(IsNull("/") || Lookup("/") == IDENTIFIER); ASSERT(IsNull("%") || Lookup("%") == IDENTIFIER); ASSERT(IsNull("==") || Lookup("==") == IDENTIFIER); ASSERT(IsNull("!=") || Lookup("!=") == IDENTIFIER); ASSERT(IsNull("===") || Lookup("===") == IDENTIFIER); ASSERT(IsNull("!==") || Lookup("!==") == IDENTIFIER); ASSERT(IsNull("<") || Lookup("<") == IDENTIFIER); ASSERT(IsNull(">") || Lookup(">") == IDENTIFIER); ASSERT(IsNull("<=") || Lookup("<=") == IDENTIFIER); ASSERT(IsNull(">=") || Lookup(">=") == IDENTIFIER); ASSERT(Lookup("instanceof") == INSTANCEOF); ASSERT(Lookup("in") == IN); ASSERT(IsNull("!") || Lookup("!") == IDENTIFIER); ASSERT(IsNull("~") || Lookup("~") == IDENTIFIER); ASSERT(Lookup("delete") == DELETE); ASSERT(Lookup("typeof") == TYPEOF); ASSERT(Lookup("void") == VOID); ASSERT(Lookup("break") == BREAK); ASSERT(Lookup("case") == CASE); ASSERT(Lookup("catch") == CATCH); ASSERT(Lookup("continue") == CONTINUE); ASSERT(Lookup("debugger") == DEBUGGER); ASSERT(Lookup("default") == DEFAULT); ASSERT(Lookup("do") == DO); ASSERT(Lookup("else") == ELSE); ASSERT(Lookup("finally") == FINALLY); ASSERT(Lookup("for") == FOR); ASSERT(Lookup("function") == FUNCTION); ASSERT(Lookup("if") == IF); ASSERT(Lookup("new") == NEW); ASSERT(Lookup("return") == RETURN); ASSERT(Lookup("switch") == SWITCH); ASSERT(Lookup("this") == THIS); ASSERT(Lookup("throw") == THROW); ASSERT(Lookup("try") == TRY); ASSERT(Lookup("var") == VAR); ASSERT(Lookup("while") == WHILE); ASSERT(Lookup("with") == WITH); ASSERT(Lookup("const") == CONST); ASSERT(Lookup("native") == NATIVE); ASSERT(Lookup("null") == NULL_LITERAL); ASSERT(Lookup("true") == TRUE_LITERAL); ASSERT(Lookup("false") == FALSE_LITERAL); ASSERT(IsNull(NULL) || Lookup(NULL) == IDENTIFIER); ASSERT(IsNull(NULL) || Lookup(NULL) == IDENTIFIER); ASSERT(IsNull(NULL) || Lookup(NULL) == IDENTIFIER); ASSERT(IsNull("ILLEGAL") || Lookup("ILLEGAL") == IDENTIFIER); ASSERT(IsNull(NULL) || Lookup(NULL) == IDENTIFIER);
*/
#undef K
#undef T
}

} }  // namespace v8::internal
