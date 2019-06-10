// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2006-2008 Google Inc. All Rights Reserved.

// A light-weight IA32 Assembler.

#ifndef V8_ASSEMBLER_IA32_H_
#define V8_ASSEMBLER_IA32_H_

namespace v8 { namespace internal {

// CPU Registers.
//
// 1) We would prefer to use an enum, but enum values are assignment-
// compatible with int, which has caused code-generation bugs.
//
// 2) We would prefer to use a class instead of a struct but we don't like
// the register initialization to depend on the particular initialization
// order (which appears to be different on OS X, Linux, and Windows for the
// installed versions of C++ we tried). Using a struct permits C-style
// "initialization". Also, the Register objects cannot be const as this
// forces initialization stubs in MSVC, making us dependent on initialization
// order.
//
// 3) By not using an enum, we are possibly preventing the compiler from
// doing certain constant folds, which may significantly reduce the
// code generated for some assembly instructions (because they boil down
// to a few constants). If this is a problem, we could change the code
// such that we use an enum in optimized mode, and the struct in debug
// mode. This way we get the compile-time error checking in debug mode
// and best performance in optimized code.
//
struct Register {
  bool is_valid() const  { return 0 <= code_ && code_ < 8; }
  bool is(Register reg) const  { return code_ == reg.code_; }
  int code() const  {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const  {
    ASSERT(is_valid());
    return 1 << code_;
  }

  // (unfortunately we can't make this private in a struct)
  int code_;
};

extern Register eax;
extern Register ecx;
extern Register edx;
extern Register ebx;
extern Register esp;
extern Register ebp;
extern Register esi;
extern Register edi;
extern Register no_reg;


struct XMMRegister {
  bool is_valid() const  { return 0 <= code_ && code_ < 2; }  // currently
  int code() const  {
    ASSERT(is_valid());
    return code_;
  }

  int code_;
};

extern XMMRegister xmm0;
extern XMMRegister xmm1;
extern XMMRegister xmm2;
extern XMMRegister xmm3;
extern XMMRegister xmm4;
extern XMMRegister xmm5;
extern XMMRegister xmm6;
extern XMMRegister xmm7;

enum Condition {
  // any value < 0 is considered no_condition
  no_condition  = -1,

  overflow      =  0,
  no_overflow   =  1,
  below         =  2,
  above_equal   =  3,
  equal         =  4,
  not_equal     =  5,
  below_equal   =  6,
  above         =  7,
  sign          =  8,
  not_sign      =  9,
  parity_even   = 10,
  parity_odd    = 11,
  less          = 12,
  greater_equal = 13,
  less_equal    = 14,
  greater       = 15,

  // aliases
  zero          = equal,
  not_zero      = not_equal,
  negative      = sign,
  positive      = not_sign
};


// Returns the equivalent of !cc.
// Negation of the default no_condition (-1) results in a non-default
// no_condition value (-2). As long as tests for no_condition check
// for condition < 0, this will work as expected.
inline Condition NegateCondition(Condition cc);

// Corresponds to transposing the operands of a comparison.
inline Condition ReverseCondition(Condition cc) {
  switch (cc) {
    case below:
      return above;
    case above:
      return below;
    case above_equal:
      return below_equal;
    case below_equal:
      return above_equal;
    case less:
      return greater;
    case greater:
      return less;
    case greater_equal:
      return less_equal;
    case less_equal:
      return greater_equal;
    default:
      return cc;
  };
}

enum Hint {
  no_hint = 0,
  not_taken = 0x2e,
  taken = 0x3e
};


// -----------------------------------------------------------------------------
// Machine instruction Immediates

class Immediate BASE_EMBEDDED {
 public:
  inline explicit Immediate(int x);
  inline explicit Immediate(const char* s);
  inline explicit Immediate(const ExternalReference& ext);
  inline explicit Immediate(Handle<Object> handle);
  inline explicit Immediate(Smi* value);

  bool is_zero() const  { return x_ == 0 && rmode_ == no_reloc; }
  bool is_int8() const  { return -128 <= x_ && x_ < 128 && rmode_ == no_reloc; }

 private:
  int x_;
  RelocMode rmode_;

  friend class Assembler;
};


// -----------------------------------------------------------------------------
// Machine instruction Operands

enum ScaleFactor {
  times_1 = 0,
  times_2 = 1,
  times_4 = 2,
  times_8 = 3
};


class Operand BASE_EMBEDDED {
 public:
  // reg
  INLINE(explicit Operand(Register reg));

  // [disp/r]
  INLINE(explicit Operand(int32_t disp, RelocMode rmode));
  // disp only must always be relocated

  // [base + disp/r]
  explicit Operand(Register base, int32_t disp, RelocMode rmode = no_reloc);

  // [base + index*scale + disp/r]
  explicit Operand(Register base,
                   Register index,
                   ScaleFactor scale,
                   int32_t disp,
                   RelocMode rmode = no_reloc);

  // [index*scale + disp/r]
  explicit Operand(Register index,
                   ScaleFactor scale,
                   int32_t disp,
                   RelocMode rmode = no_reloc);

  static Operand StaticVariable(const ExternalReference& ext) {
    return Operand(reinterpret_cast<int32_t>(ext.address()),
                   external_reference);
  }

  static Operand StaticArray(Register index,
                             ScaleFactor scale,
                             const ExternalReference& arr) {
    return Operand(index, scale, reinterpret_cast<int32_t>(arr.address()),
                   external_reference);
  }

  // Returns true if this Operand is a wrapper for the specified register.
  bool is_reg(Register reg) const;

 private:
  // Mutable because reg in ModR/M byte is set by Assembler via set_reg().
  mutable byte buf_[6];
  // The number of bytes in buf_.
  unsigned int len_;
  // Only valid if len_ > 4.
  RelocMode rmode_;

  inline void set_modrm(int mod,  // reg == 0
                        Register rm);
  inline void set_sib(ScaleFactor scale, Register index, Register base);
  inline void set_disp8(int8_t disp);
  inline void set_dispr(int32_t disp, RelocMode rmode);
  inline void set_reg(Register reg) const;

  friend class Assembler;
};


// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a Scope before use.
// Example:
//   if (CpuFeatures::IsSupported(SSE2)) {
//     CpuFeatures::Scope fscope(SSE2);
//     // Generate SSE2 floating point code.
//   } else {
//     // Generate standard x87 floating point code.
//   }
class CpuFeatures : public AllStatic {
 public:
  // Feature flags bit positions. They are mostly based on the CPUID spec.
  // (We assign CPUID itself to one of the currently reserved bits --
  // feel free to change this if needed.)
  enum Feature { SSE2 = 26, CMOV = 15, RDTSC = 4, CPUID = 10 };
  // Detect features of the target CPU. Set safe defaults if the serializer
  // is enabled (snapshots must be portable).
  static void Probe();
  // Check whether a feature is supported by the target CPU.
  static bool IsSupported(Feature f) { return supported_ & (1 << f); }
  // Check whether a feature is currently enabled.
  static bool IsEnabled(Feature f) { return enabled_ & (1 << f); }
  // Enable a specified feature within a scope.
  class Scope BASE_EMBEDDED {
#ifdef DEBUG
   public:
    explicit Scope(Feature f) {
      ASSERT(CpuFeatures::IsSupported(f));
      old_enabled_ = CpuFeatures::enabled_;
      CpuFeatures::enabled_ |= (1 << f);
    }
    ~Scope() { CpuFeatures::enabled_ = old_enabled_; }
   private:
    uint32_t old_enabled_;
#else
   public:
    explicit Scope(Feature f) {}
#endif
  };
 private:
  static uint32_t supported_;
  static uint32_t enabled_;
};


class Assembler : public Malloced {
 private:
  // The relocation writer's position is kGap bytes below the end of
  // the generated instructions. This leaves enough space for the
  // longest possible ia32 instruction (17 bytes as of 9/26/06) and
  // allows for a single, fast space check per instruction.
  static const int kGap = 32;

 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is NULL, the assembler allocates and grows its own
  // buffer, and buffer_size determines the initial buffer size. The buffer is
  // owned by the assembler and deallocated upon destruction of the assembler.
  //
  // If the provided buffer is not NULL, the assembler uses the provided buffer
  // for code generation and assumes its size to be buffer_size. If the buffer
  // is too small, a fatal error occurs. No deallocation of the buffer is done
  // upon destruction of the assembler.
  Assembler(void* buffer, int buffer_size);
  ~Assembler();

  // GetCode emits any pending (non-emitted) code and fills the descriptor
  // desc. GetCode() is idempotent; it returns the same result if no other
  // Assembler functions are invoked inbetween GetCode() calls.
  void GetCode(CodeDesc* desc);

  // Read/Modify the code target in the branch/call instruction at pc.
  inline static Address target_address_at(Address pc);
  inline static void set_target_address_at(Address pc, Address target);

  // Distance between the address of the code target in the call instruction
  // and the return address
  static const int kTargetAddrToReturnAddrDist = kPointerSize;


  // ---------------------------------------------------------------------------
  // Code generation
  //
  // - function names correspond one-to-one to ia32 instruction mnemonics
  // - unless specified otherwise, instructions operate on 32bit operands
  // - instructions on 8bit (byte) operands/registers have a trailing '_b'
  // - instructions on 16bit (word) operands/registers have a trailing '_w'
  // - naming conflicts with C++ keywords are resolved via a trailing '_'

  // NOTE ON INTERFACE: Currently, the interface is not very consistent
  // in the sense that some operations (e.g. mov()) can be called in more
  // the one way to generate the same instruction: The Register argument
  // can in some cases be replaced with an Operand(Register) argument.
  // This should be cleaned up and made more othogonal. The questions
  // is: should we always use Operands instead of Registers where an
  // Operand is possible, or should we have a Register (overloaded) form
  // instead? We must be carefull to make sure that the selected instruction
  // is obvious from the parameters to avoid hard-to-find code generation
  // bugs.

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2.
  void Align(int m);

  // Stack
  void pushad();
  void popad();

  void pushfd();
  void popfd();

  void push(const Immediate& x);
  void push(Register src);
  void push(const Operand& src);

  void pop(Register dst);
  void pop(const Operand& dst);

  // Moves
  void mov_b(Register dst, const Operand& src);
  void mov_b(const Operand& dst, int8_t imm8);
  void mov_b(const Operand& dst, Register src);

  void mov_w(Register dst, const Operand& src);
  void mov_w(const Operand& dst, Register src);

  void mov(Register dst, int32_t imm32);
  void mov(Register dst, Handle<Object> handle);
  void mov(Register dst, const Operand& src);
  void mov(const Operand& dst, const Immediate& x);
  void mov(const Operand& dst, Handle<Object> handle);
  void mov(const Operand& dst, Register src);

  void movsx_b(Register dst, const Operand& src);

  void movsx_w(Register dst, const Operand& src);

  void movzx_b(Register dst, const Operand& src);

  void movzx_w(Register dst, const Operand& src);

  // Conditional moves
  void cmov(Condition cc, Register dst, int32_t imm32);
  void cmov(Condition cc, Register dst, Handle<Object> handle);
  void cmov(Condition cc, Register dst, const Operand& src);

  // Arithmetics
  void adc(Register dst, int32_t imm32);
  void adc(Register dst, const Operand& src);

  void add(Register dst, const Operand& src);
  void add(const Operand& dst, const Immediate& x);

  void and_(Register dst, int32_t imm32);
  void and_(Register dst, const Operand& src);
  void and_(const Operand& src, Register dst);
  void and_(const Operand& dst, const Immediate& x);

  void cmp(Register reg, int32_t imm32);
  void cmp(Register reg, Handle<Object> handle);
  void cmp(Register reg, const Operand& op);
  void cmp(const Operand& op, const Immediate& imm);

  void dec_b(Register dst);

  void dec(Register dst);
  void dec(const Operand& dst);

  void cdq();

  void idiv(Register src);

  void imul(Register dst, const Operand& src);
  void imul(Register dst, Register src, int32_t imm32);

  void inc(Register dst);
  void inc(const Operand& dst);

  void lea(Register dst, const Operand& src);

  void mul(Register src);

  void neg(Register dst);

  void not_(Register dst);

  void or_(Register dst, int32_t imm32);
  void or_(Register dst, const Operand& src);
  void or_(const Operand& dst, Register src);
  void or_(const Operand& dst, const Immediate& x);

  void rcl(Register dst, uint8_t imm8);

  void sar(Register dst, uint8_t imm8);
  void sar(Register dst);

  void sbb(Register dst, const Operand& src);

  void shld(Register dst, const Operand& src);

  void shl(Register dst, uint8_t imm8);
  void shl(Register dst);

  void shrd(Register dst, const Operand& src);

  void shr(Register dst, uint8_t imm8);
  void shr(Register dst);

  void sub(const Operand& dst, const Immediate& x);
  void sub(Register dst, const Operand& src);
  void sub(const Operand& dst, Register src);

  void test(Register reg, const Immediate& imm);
  void test(Register reg, const Operand& op);
  void test(const Operand& op, const Immediate& imm);

  void xor_(Register dst, int32_t imm32);
  void xor_(Register dst, const Operand& src);
  void xor_(const Operand& src, Register dst);
  void xor_(const Operand& dst, const Immediate& x);

  // Bit operations.
  void bts(const Operand& dst, Register src);

  // Miscellaneous
  void hlt();
  void int3();
  void nop();
  void rdtsc();
  void ret(int imm16);
  void leave();

  // Label operations & relative jumps (PPUM Appendix D)
  //
  // Takes a branch opcode (cc) and a label (L) and generates
  // either a backward branch or a forward branch and links it
  // to the label fixup chain. Usage:
  //
  // Label L;    // unbound label
  // j(cc, &L);  // forward branch to unbound label
  // bind(&L);   // bind label to the current pc
  // j(cc, &L);  // backward branch to bound label
  // bind(&L);   // illegal: a label may be bound only once
  //
  // Note: The same Label can be used for forward and backward branches
  // but it may be bound only once.

  void bind(Label* L);  // binds an unbound label L to the current code position

  // Calls
  void call(Label* L);
  void call(byte* entry, RelocMode rmode);
  void call(const Operand& adr);
  void call(Handle<Code> code, RelocMode rmode);

  // Jumps
  void jmp(Label* L);  // unconditional jump to L
  void jmp(byte* entry, RelocMode rmode);
  void jmp(const Operand& adr);
  void jmp(Handle<Code> code, RelocMode rmode);

  // Conditional jumps
  void j(Condition cc, Label* L, Hint hint = no_hint);
  void j(Condition cc, byte* entry, RelocMode rmode, Hint hint = no_hint);
  void j(Condition cc, Handle<Code> code, Hint hint = no_hint);

  // Floating-point operations
  void fld(int i);

  void fld1();
  void fldz();

  void fld_s(const Operand& adr);
  void fld_d(const Operand& adr);

  void fstp_s(const Operand& adr);
  void fstp_d(const Operand& adr);

  void fild_s(const Operand& adr);
  void fild_d(const Operand& adr);

  void fist_s(const Operand& adr);

  void fistp_s(const Operand& adr);
  void fistp_d(const Operand& adr);

  void fabs();
  void fchs();

  void fadd(int i);
  void fsub(int i);
  void fmul(int i);
  void fdiv(int i);

  void fisub_s(const Operand& adr);

  void faddp(int i = 1);
  void fsubp(int i = 1);
  void fsubrp(int i = 1);
  void fmulp(int i = 1);
  void fdivp(int i = 1);
  void fprem();
  void fprem1();

  void fxch(int i = 1);
  void fincstp();
  void ffree(int i = 0);

  void ftst();
  void fucomp(int i);
  void fucompp();
  void fcompp();
  void fnstsw_ax();
  void fwait();

  void frndint();

  void sahf();

  void cpuid();

  // SSE2 instructions
  void cvttss2si(Register dst, const Operand& src);
  void cvttsd2si(Register dst, const Operand& src);

  void cvtsi2sd(XMMRegister dst, const Operand& src);

  void addsd(XMMRegister dst, XMMRegister src);
  void subsd(XMMRegister dst, XMMRegister src);
  void mulsd(XMMRegister dst, XMMRegister src);
  void divsd(XMMRegister dst, XMMRegister src);

  // Use either movsd or movlpd.
  void movdbl(XMMRegister dst, const Operand& src);
  void movdbl(const Operand& dst, XMMRegister src);

  // Debugging
  void Print();

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* l) { return pc_offset() - l->pos(); }

  // Mark address of the ExitJSFrame code.
  void RecordJSReturn();

  // Record a comment relocation entry that can be used by a disassembler.
  // Use --debug_code to enable.
  void RecordComment(const char* msg);

  void RecordPosition(int pos);
  void RecordStatementPosition(int pos);

  int pc_offset() const  { return pc_ - buffer_; }
  int last_position() const  { return last_position_; }
  bool last_position_is_statement() const  {
    return last_position_is_statement_;
  }

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool overflow() const { return pc_ >= reloc_info_writer.pos() - kGap; }

  // Get the number of bytes available in the buffer.
  inline int available_space() const { return reloc_info_writer.pos() - pc_; }

  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512*MB;
  static const int kMinimalBufferSize = 4*KB;

 protected:
  void movsd(XMMRegister dst, const Operand& src);
  void movsd(const Operand& dst, XMMRegister src);

  void emit_sse_operand(XMMRegister reg, const Operand& adr);
  void emit_sse_operand(XMMRegister dst, XMMRegister src);


 private:
  // Code buffer:
  // The buffer into which code and relocation info are generated.
  byte* buffer_;
  int buffer_size_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  // code generation
  byte* pc_;  // the program counter; moves forward
  RelocInfoWriter reloc_info_writer;

  // push-pop elimination
  byte* last_pc_;

  // Jump-to-jump elimination:
  // The last label to be bound to _binding_pos, if unbound.
  Label unbound_label_;
  // The position to which _unbound_label has to be bound, if present.
  int binding_pos_;
  // The position before which jumps cannot be eliminated.
  int last_bound_pos_;

  // source position information
  int last_position_;
  bool last_position_is_statement_;

  byte* addr_at(int pos)  { return buffer_ + pos; }
  byte byte_at(int pos)  { return buffer_[pos]; }
  uint32_t long_at(int pos)  {
    return *reinterpret_cast<uint32_t*>(addr_at(pos));
  }
  void long_at_put(int pos, uint32_t x)  {
    *reinterpret_cast<uint32_t*>(addr_at(pos)) = x;
  }

  // code emission
  void GrowBuffer();
  inline void emit(uint32_t x);
  inline void emit(Handle<Object> handle);
  inline void emit(uint32_t x, RelocMode rmode);
  inline void emit(const Immediate& x);

  // instruction generation
  void emit_arith_b(int op1, int op2, Register dst, int imm8);

  // Emit a basic arithmetic instruction (i.e. first byte of the family is 0x81)
  // with a given destination expression and an immediate operand.  It attempts
  // to use the shortest encoding possible.
  // sel specifies the /n in the modrm byte (see the Intel PRM).
  void emit_arith(int sel, Operand dst, const Immediate& x);

  void emit_operand(Register reg, const Operand& adr);
  void emit_operand(const Operand& adr, Register reg);

  void emit_farith(int b1, int b2, int i);

  // labels
  void print(Label* L);
  void bind_to(Label* L, int pos);
  void link_to(Label* L, Label* appendix);

  // record reloc info for current pc_
  void RecordRelocInfo(RelocMode rmode, intptr_t data = 0);

  friend class CodePatcher;
  friend class EnsureSpace;
};


// Helper class that ensures that there is enough space for generating
// instructions and relocation information.  The constructor makes
// sure that there is enough space and (in debug mode) the destructor
// checks that we did not generate too much.
class EnsureSpace BASE_EMBEDDED {
 public:
  explicit EnsureSpace(Assembler* assembler) : assembler_(assembler) {
    if (assembler_->overflow()) assembler_->GrowBuffer();
#ifdef DEBUG
    space_before_ = assembler_->available_space();
#endif
  }

#ifdef DEBUG
  ~EnsureSpace() {
    int bytes_generated = space_before_ - assembler_->available_space();
    ASSERT(bytes_generated < assembler_->kGap);
  }
#endif

 private:
  Assembler* assembler_;
#ifdef DEBUG
  int space_before_;
#endif
};

} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_IA32_H_
