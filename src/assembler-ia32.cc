// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been modified
// significantly by Google Inc.
// Copyright 2006-2008 Google Inc. All Rights Reserved.

#include "v8.h"

#include "disassembler.h"
#include "macro-assembler.h"
#include "serialize.h"

namespace v8 { namespace internal {

DEFINE_bool(debug_code, false,
            "generate extra code (comments, assertions) for debugging");
DEFINE_bool(emit_branch_hints, false, "emit branch hints");

// -----------------------------------------------------------------------------
// Implementation of Register

Register eax = { 0 };
Register ecx = { 1 };
Register edx = { 2 };
Register ebx = { 3 };
Register esp = { 4 };
Register ebp = { 5 };
Register esi = { 6 };
Register edi = { 7 };
Register no_reg = { -1 };

XMMRegister xmm0 = { 0 };
XMMRegister xmm1 = { 1 };
XMMRegister xmm2 = { 2 };
XMMRegister xmm3 = { 3 };
XMMRegister xmm4 = { 4 };
XMMRegister xmm5 = { 5 };
XMMRegister xmm6 = { 6 };
XMMRegister xmm7 = { 7 };


// -----------------------------------------------------------------------------
// Implementation of CpuFeatures

// Safe default is no features.
uint32_t CpuFeatures::supported_ = 0;
uint32_t CpuFeatures::enabled_ = 0;


typedef int (*F0)();

// The Probe method needs executable memory, so it uses Heap::CreateCode.
// Allocation failure is silent and leads to safe default.
void CpuFeatures::Probe() {
  supported_ = 0;
  if (Serializer::enabled()) return;  // No features if we might serialize.
  Assembler assm(NULL, 0);
  Label done;
#define __ assm.
  // Save old esp, since we are going to modify the stack.
  __ push(ebp);
  __ pushfd();
  __ push(ecx);
  __ push(edx);
  __ push(ebx);
  __ mov(ebp, Operand(esp));
  // If we can modify bit 21 of the EFLAGS register, then CPUID is supported.
  __ pushfd();
  __ pop(eax);
  __ mov(edx, Operand(eax));
  __ xor_(eax, 0x200000);  // Flip bit 21.
  __ push(eax);
  __ popfd();
  __ pushfd();
  __ pop(eax);
  __ xor_(eax, Operand(edx));  // Different if CPUID is supported.
  __ j(zero, &done);
  // Invoke CPUID with 1 in eax to get feature information in edx.
  __ mov(eax, 1);
  // Temporarily force CPUID support, since we know it is safe here.
  supported_ = (1 << CPUID);
  { Scope fscope(CPUID);
    __ cpuid();
  }
  supported_ = 0;
  // Return result in eax.
  __ mov(eax, Operand(edx));
  __ bind(&done);
  __ mov(esp, Operand(ebp));
  __ pop(ebx);
  __ pop(edx);
  __ pop(ecx);
  __ popfd();
  __ pop(ebp);
  __ ret(0);
#undef __
  CodeDesc desc;
  assm.GetCode(&desc);
  Object* code = Heap::CreateCode(desc, NULL, Code::ComputeFlags(Code::STUB));
  if (!code->IsCode()) return;
  F0 f = FUNCTION_CAST<F0>(Code::cast(code)->entry());
  uint32_t res = f();
  supported_ = (res | (1 << CPUID));
}


// -----------------------------------------------------------------------------
// A Displacement describes the 32bit immediate field of an instruction which
// may be used together with a Label in order to refer to a yet unknown code
// position. Displacements stored in the instruction stream are used to describe
// the instruction and to chain a list of instructions using the same Label.
// A Displacement contains 3 different fields:
//
// next field: position of next displacement in the chain (0 = end of list)
// type field: instruction type
//
// A next value of null (0) indicates the end of a chain (note that there can
// be no displacement at position zero, because there is always at least one
// instruction byte before the displacement).
//
// Displacement _data field layout
//
// |31.....1|.......0|
// [  next  |  type  |

class Displacement BASE_EMBEDDED {
 private:
  enum Type {
    UNCONDITIONAL_JUMP,
    OTHER
  };

  int data_;

  class TypeField: public BitField<Type, 0, 1> {};
  class NextField: public BitField<int,  1, 32-1> {};

  void init(Label* L, Type type) {
    ASSERT(!L->is_bound());
    int next = 0;
    if (L->is_linked()) {
      next = L->pos();
      ASSERT(next > 0);  // Displacements must be at positions > 0
    }
    // Ensure that we _never_ overflow the next field.
    ASSERT(NextField::is_valid(Assembler::kMaximalBufferSize));
    data_ = NextField::encode(next) | TypeField::encode(type);
  }

  int data() const { return data_; }
  Type type() const { return TypeField::decode(data_); }
  void next(Label* L) const {
    int n = NextField::decode(data_);
    n > 0 ? L->link_to(n) : L->Unuse();
  }
  void link_to(Label* L) { init(L, type()); }

  explicit Displacement(int data) { data_ = data; }

  Displacement(Label* L, Type type) { init(L, type); }

  void print() {
    PrintF("%s (%x) ", (type() == UNCONDITIONAL_JUMP ? "jmp" : "[other]"),
                       NextField::decode(data_));
  }

  friend class Assembler;
  friend class MacroAssembler;
};


// TODO(1236137): Stop using macros here. The reason for using them is
// to avoid declaring the Displacement class in the .h file and have
// functions on the assembler that returns them. Maybe that's not a
// big issue?
#define disp_at(L)                              \
  Displacement(long_at((L)->pos()))

#define disp_at_put(L, disp)                    \
  long_at_put((L)->pos(), (disp).data())

#define emit_disp(L, type) {                    \
    Displacement disp((L), (type));             \
    (L)->link_to(pc_offset());                  \
    emit(static_cast<int>(disp.data()));        \
  }


// -----------------------------------------------------------------------------
// Implementation of RelocInfo


const int RelocInfo::kApplyMask =
  RelocInfo::kCodeTargetMask | 1 << runtime_entry | 1 << js_return;


void RelocInfo::patch_code(byte* instructions, int instruction_count) {
  // Patch the code at the current address with the supplied instructions.
  for (int i = 0; i < instruction_count; i++) {
    *(pc_ + i) = *(instructions + i);
  }
}


// Patch the code at the current PC with a call to the target address.
// Additional guard int3 instructions can be added if required.
void RelocInfo::patch_code_with_call(Address target, int guard_bytes) {
  // Call instruction takes up 5 bytes and int3 takes up one byte.
  int code_size = 5 + guard_bytes;

  // Patch the code.
  CodePatcher patcher(pc_, code_size);
  patcher.masm()->call(target, no_reloc);

  // Add the requested number of int3 instructions after the call.
  for (int i = 0; i < guard_bytes; i++) {
    patcher.masm()->int3();
  }
}


// -----------------------------------------------------------------------------
// Implementation of Operand

Operand::Operand(Register base, int32_t disp, RelocMode rmode) {
  // [base + disp/r]
  if (disp == 0 && rmode == no_reloc && !base.is(ebp)) {
    // [base]
    set_modrm(0, base);
    if (base.is(esp)) set_sib(times_1, esp, base);
  } else if (is_int8(disp) && rmode == no_reloc) {
    // [base + disp8]
    set_modrm(1, base);
    if (base.is(esp)) set_sib(times_1, esp, base);
    set_disp8(disp);
  } else {
    // [base + disp/r]
    set_modrm(2, base);
    if (base.is(esp)) set_sib(times_1, esp, base);
    set_dispr(disp, rmode);
  }
}


Operand::Operand(Register base,
                 Register index,
                 ScaleFactor scale,
                 int32_t disp,
                 RelocMode rmode) {
  ASSERT(!index.is(esp));  // illegal addressing mode
  // [base + index*scale + disp/r]
  if (disp == 0 && rmode == no_reloc && !base.is(ebp)) {
    // [base + index*scale]
    set_modrm(0, esp);
    set_sib(scale, index, base);
  } else if (is_int8(disp) && rmode == no_reloc) {
    // [base + index*scale + disp8]
    set_modrm(1, esp);
    set_sib(scale, index, base);
    set_disp8(disp);
  } else {
    // [base + index*scale + disp/r]
    set_modrm(2, esp);
    set_sib(scale, index, base);
    set_dispr(disp, rmode);
  }
}


Operand::Operand(Register index,
                 ScaleFactor scale,
                 int32_t disp,
                 RelocMode rmode) {
  ASSERT(!index.is(esp));  // illegal addressing mode
  // [index*scale + disp/r]
  set_modrm(0, esp);
  set_sib(scale, index, ebp);
  set_dispr(disp, rmode);
}


void Operand::set_sib(ScaleFactor scale, Register index, Register base) {
  ASSERT(len_ == 1);
  ASSERT((scale & -4) == 0);
  buf_[1] = scale << 6 | index.code() << 3 | base.code();
  len_ = 2;
}


void Operand::set_disp8(int8_t disp) {
  ASSERT(len_ == 1 || len_ == 2);
  *reinterpret_cast<int8_t*>(&buf_[len_++]) = disp;
}


void Operand::set_reg(Register reg) const {
  ASSERT(len_ > 0);
  buf_[0] = (buf_[0] & ~0x38) | static_cast<byte>(reg.code() << 3);
}


bool Operand::is_reg(Register reg) const {
  return ((buf_[0] & 0xF8) == 0xC0)  // addressing mode is register only.
      && ((buf_[0] & 0x07) == reg.code());  // register codes match.
}

// -----------------------------------------------------------------------------
// Implementation of Assembler

// Emit a single byte. Must always be inlined.
#define EMIT(x)                                 \
  *pc_++ = (x)


// spare_buffer_
static byte* spare_buffer_ = NULL;

Assembler::Assembler(void* buffer, int buffer_size) {
  if (buffer == NULL) {
    // do our own buffer management
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (spare_buffer_ != NULL) {
        buffer = spare_buffer_;
        spare_buffer_ = NULL;
      }
    }
    if (buffer == NULL) {
      buffer_ = NewArray<byte>(buffer_size);
    } else {
      buffer_ = static_cast<byte*>(buffer);
    }
    buffer_size_ = buffer_size;
    own_buffer_ = true;

  } else {
    // use externally provided buffer instead
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Clear the buffer in debug mode unless it was provided by the
  // caller in which case we can't be sure it's okay to overwrite
  // existing code in it; see CodePatcher::CodePatcher(...).
  if (kDebug && own_buffer_) {
    memset(buffer_, 0xCC, buffer_size);  // int3
  }

  // setup buffer pointers
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
  reloc_info_writer.Reposition(buffer_ + buffer_size, pc_);

  last_pc_ = NULL;
  last_bound_pos_ = 0;
  last_position_ = kNoPosition;
  last_position_is_statement_ = false;
}


Assembler::~Assembler() {
  if (own_buffer_) {
    if (spare_buffer_ == NULL && buffer_size_ == kMinimalBufferSize) {
      spare_buffer_ = buffer_;
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  // finalize code
  if (unbound_label_.is_linked())
    bind_to(&unbound_label_, binding_pos_);

  // (at this point overflow() may be true, but the gap ensures that
  // we are still not overlapping instructions and relocation info)
  ASSERT(pc_ <= reloc_info_writer.pos());  // no overlap
  // setup desc
  desc->buffer = buffer_;
  desc->buffer_size = buffer_size_;
  desc->instr_size = pc_offset();
  desc->reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();

  Counters::reloc_info_size.Increment(desc->reloc_size);
}


void Assembler::Align(int m) {
  ASSERT(IsPowerOf2(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}


void Assembler::cpuid() {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::CPUID));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xA2);
}


void Assembler::pushad() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x60);
}


void Assembler::popad() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x61);
}


void Assembler::pushfd() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x9C);
}


void Assembler::popfd() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x9D);
}


void Assembler::push(const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (x.is_int8()) {
    EMIT(0x6a);
    EMIT(x.x_);
  } else {
    EMIT(0x68);
    emit(x);
  }
}


DEFINE_bool(push_pop_elimination, true,
            "eliminate redundant push/pops in assembly code");
DEFINE_bool(print_push_pop_elimination, false,
            "print elimination of redundant push/pops in assembly code");

void Assembler::push(Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x50 | src.code());
}


void Assembler::push(const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFF);
  emit_operand(esi, src);
}


void Assembler::pop(Register dst) {
  ASSERT(reloc_info_writer.last_pc() != NULL);
  if (FLAG_push_pop_elimination && (reloc_info_writer.last_pc() <= last_pc_)) {
    // (last_pc_ != NULL) is rolled into the above check
    // If a last_pc_ is set, we need to make sure that there has not been any
    // relocation information generated between the last instruction and this
    // pop instruction.
    byte instr = last_pc_[0];
    if (instr == (0x50 | dst.code())) {
      pc_ = last_pc_;
      last_pc_ = NULL;
      if (FLAG_print_push_pop_elimination) {
        PrintF("%d push/pop (same reg) eliminated\n", pc_offset());
      }
      return;
    } else if (instr == 0xff) {  // push of an operand, convert to a move
      byte op1 = last_pc_[1];
      // Check if the operation is really a push
      if ((op1 & 0x38) == (6 << 3)) {
        op1 = (op1 & ~0x38) | static_cast<byte>(dst.code() << 3);
        last_pc_[0] = 0x8b;
        last_pc_[1] = op1;
        last_pc_ = NULL;
        if (FLAG_print_push_pop_elimination) {
          PrintF("%d push/pop (op->reg) eliminated\n", pc_offset());
        }
        return;
      }
    } else if ((instr == 0x89) &&
               (last_pc_[1] == 0x04) &&
               (last_pc_[2] == 0x24)) {
      // 0x71283c   396  890424         mov [esp],eax
      // 0x71283f   399  58             pop eax
      if (dst.is(eax)) {
        // change to
        // 0x710fac   216  83c404         add esp,0x4
        last_pc_[0] = 0x83;
        last_pc_[1] = 0xc4;
        last_pc_[2] = 0x04;
        last_pc_ = NULL;
        if (FLAG_print_push_pop_elimination) {
          PrintF("%d push/pop (mov-pop) eliminated\n", pc_offset());
        }
        return;
      }
    }
    // Other potential patterns for peephole:
    // 0x712716   102  890424         mov [esp], eax
    // 0x712719   105  8b1424         mov edx, [esp]
  }
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x58 | dst.code());
}


void Assembler::pop(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x8F);
  emit_operand(eax, dst);
}


void Assembler::mov_b(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x8A);
  emit_operand(dst, src);
}


void Assembler::mov_b(const Operand& dst, int8_t imm8) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xC6);
  emit_operand(eax, dst);
  EMIT(imm8);
}


void Assembler::mov_b(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x88);
  emit_operand(src, dst);
}


void Assembler::mov_w(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x66);
  EMIT(0x8B);
  emit_operand(dst, src);
}


void Assembler::mov_w(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x66);
  EMIT(0x89);
  emit_operand(src, dst);
}


void Assembler::mov(Register dst, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xB8 | dst.code());
  emit(imm32);
}


void Assembler::mov(Register dst, Handle<Object> handle) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xB8 | dst.code());
  emit(handle);
}


void Assembler::mov(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x8B);
  emit_operand(dst, src);
}


void Assembler::mov(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xC7);
  emit_operand(eax, dst);
  emit(x);
}


void Assembler::mov(const Operand& dst, Handle<Object> handle) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xC7);
  emit_operand(eax, dst);
  emit(handle);
}


void Assembler::mov(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x89);
  emit_operand(src, dst);
}


void Assembler::movsx_b(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xBE);
  emit_operand(dst, src);
}


void Assembler::movsx_w(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xBF);
  emit_operand(dst, src);
}


void Assembler::movzx_b(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xB6);
  emit_operand(dst, src);
}


void Assembler::movzx_w(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xB7);
  emit_operand(dst, src);
}


void Assembler::cmov(Condition cc, Register dst, int32_t imm32) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::CMOV));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  UNIMPLEMENTED();
  USE(cc);
  USE(dst);
  USE(imm32);
}


void Assembler::cmov(Condition cc, Register dst, Handle<Object> handle) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::CMOV));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  UNIMPLEMENTED();
  USE(cc);
  USE(dst);
  USE(handle);
}


void Assembler::cmov(Condition cc, Register dst, const Operand& src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::CMOV));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  UNIMPLEMENTED();
  USE(cc);
  USE(dst);
  USE(src);
}


void Assembler::adc(Register dst, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(2, Operand(dst), Immediate(imm32));
}


void Assembler::adc(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x13);
  emit_operand(dst, src);
}


void Assembler::add(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x03);
  emit_operand(dst, src);
}


void Assembler::add(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(0, dst, x);
}


void Assembler::and_(Register dst, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(4, Operand(dst), Immediate(imm32));
}


void Assembler::and_(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x23);
  emit_operand(dst, src);
}


void Assembler::and_(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(4, dst, x);
}


void Assembler::and_(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x21);
  emit_operand(dst, src);
}


void Assembler::cmp(Register reg, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(7, Operand(reg), Immediate(imm32));
}


void Assembler::cmp(Register reg, Handle<Object> handle) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(7, Operand(reg), Immediate(handle));
}


void Assembler::cmp(Register reg, const Operand& op) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x3B);
  emit_operand(reg, op);
}


void Assembler::cmp(const Operand& op, const Immediate& imm) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(7, op, imm);
}


void Assembler::dec_b(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFE);
  EMIT(0xC8 | dst.code());
}


void Assembler::dec(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x48 | dst.code());
}


void Assembler::dec(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFF);
  emit_operand(ecx, dst);
}


void Assembler::cdq() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x99);
}


void Assembler::idiv(Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF7);
  EMIT(0xF8 | src.code());
}


void Assembler::imul(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xAF);
  emit_operand(dst, src);
}


void Assembler::imul(Register dst, Register src, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (is_int8(imm32)) {
    EMIT(0x6B);
    EMIT(0xC0 | dst.code() << 3 | src.code());
    EMIT(imm32);
  } else {
    EMIT(0x69);
    EMIT(0xC0 | dst.code() << 3 | src.code());
    emit(imm32);
  }
}


void Assembler::inc(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x40 | dst.code());
}


void Assembler::inc(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFF);
  emit_operand(eax, dst);
}


void Assembler::lea(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x8D);
  emit_operand(dst, src);
}


void Assembler::mul(Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF7);
  EMIT(0xE0 | src.code());
}


void Assembler::neg(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF7);
  EMIT(0xD8 | dst.code());
}


void Assembler::not_(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF7);
  EMIT(0xD0 | dst.code());
}


void Assembler::or_(Register dst, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(1, Operand(dst), Immediate(imm32));
}


void Assembler::or_(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0B);
  emit_operand(dst, src);
}


void Assembler::or_(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(1, dst, x);
}


void Assembler::or_(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x09);
  emit_operand(dst, src);
}


void Assembler::rcl(Register dst, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint5(imm8));  // illegal shift count
  if (imm8 == 1) {
    EMIT(0xD1);
    EMIT(0xD0 | dst.code());
  } else {
    EMIT(0xC1);
    EMIT(0xD0 | dst.code());
    EMIT(imm8);
  }
}


void Assembler::sar(Register dst, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint5(imm8));  // illegal shift count
  if (imm8 == 1) {
    EMIT(0xD1);
    EMIT(0xF8 | dst.code());
  } else {
    EMIT(0xC1);
    EMIT(0xF8 | dst.code());
    EMIT(imm8);
  }
}


void Assembler::sar(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD3);
  EMIT(0xF8 | dst.code());
}


void Assembler::sbb(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x1B);
  emit_operand(dst, src);
}


void Assembler::shld(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xA5);
  emit_operand(dst, src);
}


void Assembler::shl(Register dst, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint5(imm8));  // illegal shift count
  if (imm8 == 1) {
    EMIT(0xD1);
    EMIT(0xE0 | dst.code());
  } else {
    EMIT(0xC1);
    EMIT(0xE0 | dst.code());
    EMIT(imm8);
  }
}


void Assembler::shl(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD3);
  EMIT(0xE0 | dst.code());
}


void Assembler::shrd(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xAD);
  emit_operand(dst, src);
}


void Assembler::shr(Register dst, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint5(imm8));  // illegal shift count
  EMIT(0xC1);
  EMIT(0xE8 | dst.code());
  EMIT(imm8);
}


void Assembler::shr(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD3);
  EMIT(0xE8 | dst.code());
}


void Assembler::sub(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(5, dst, x);
}


void Assembler::sub(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x2B);
  emit_operand(dst, src);
}


void Assembler::sub(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x29);
  emit_operand(dst, src);
}


void Assembler::test(Register reg, const Immediate& imm) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  // Only use test against byte for registers that have a byte
  // variant: eax, ebx, ecx, and edx.
  if (imm.rmode_ == no_reloc && is_uint8(imm.x_) && reg.code() < 4) {
    uint8_t imm8 = imm.x_;
    if (reg.is(eax)) {
      EMIT(0xA8);
      EMIT(imm8);
    } else {
      emit_arith_b(0xF6, 0xC0, reg, imm8);
    }
  } else {
    // This is not using emit_arith because test doesn't support
    // sign-extension of 8-bit operands.
    if (reg.is(eax)) {
      EMIT(0xA9);
    } else {
      EMIT(0xF7);
      EMIT(0xC0 | reg.code());
    }
    emit(imm);
  }
}


void Assembler::test(Register reg, const Operand& op) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x85);
  emit_operand(reg, op);
}


void Assembler::test(const Operand& op, const Immediate& imm) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF7);
  emit_operand(eax, op);
  emit(imm);
}


void Assembler::xor_(Register dst, int32_t imm32) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(6, Operand(dst), Immediate(imm32));
}


void Assembler::xor_(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x33);
  emit_operand(dst, src);
}


void Assembler::xor_(const Operand& src, Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x31);
  emit_operand(dst, src);
}


void Assembler::xor_(const Operand& dst, const Immediate& x) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_arith(6, dst, x);
}


void Assembler::bts(const Operand& dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0xAB);
  emit_operand(src, dst);
}


void Assembler::hlt() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF4);
}


void Assembler::int3() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xCC);
}


void Assembler::nop() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x90);
}


void Assembler::rdtsc() {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::RDTSC));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x0F);
  EMIT(0x31);
}


void Assembler::ret(int imm16) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint16(imm16));
  if (imm16 == 0) {
    EMIT(0xC3);
  } else {
    EMIT(0xC2);
    EMIT(imm16 & 0xFF);
    EMIT((imm16 >> 8) & 0xFF);
  }
}


void Assembler::leave() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xC9);
}


// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the 32bit
// Displacement of the last instruction using the label.


void Assembler::print(Label* L) {
  if (L->is_unused()) {
    PrintF("unused label\n");
  } else if (L->is_bound()) {
    PrintF("bound label to %d\n", L->pos());
  } else if (L->is_linked()) {
    Label l = *L;
    PrintF("unbound label");
    while (l.is_linked()) {
      Displacement disp = disp_at(&l);
      PrintF("@ %d ", l.pos());
      disp.print();
      PrintF("\n");
      disp.next(&l);
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}


DEFINE_bool(eliminate_jumps, true, "eliminate jumps to jumps in assembly code");
DEFINE_bool(print_jump_elimination, false,
            "print elimination of jumps to jumps in assembly code");

void Assembler::bind_to(Label* L, int pos) {
  EnsureSpace ensure_space(this);
  last_pc_ = NULL;
  ASSERT(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  while (L->is_linked()) {
    Displacement disp = disp_at(L);
    int fixup_pos = L->pos();
    if (disp.type() == Displacement::UNCONDITIONAL_JUMP) {
      ASSERT(byte_at(fixup_pos - 1) == 0xE9);  // jmp expected
    }
    int imm32 = pos - (fixup_pos + sizeof(int32_t));
    long_at_put(fixup_pos, imm32);
    disp.next(L);
  }
  L->bind_to(pos);

  // do not eliminate jump instructions before the last bound position
  if (pos > last_bound_pos_)
    last_bound_pos_ = pos;
}


void Assembler::link_to(Label* L, Label* appendix) {
  EnsureSpace ensure_space(this);
  last_pc_ = NULL;
  if (appendix->is_linked()) {
    if (L->is_linked()) {
      // append appendix to L's list
      Label p;
      Label q = *L;
      do {
        p = q;
        Displacement disp = disp_at(&q);
        disp.next(&q);
      } while (q.is_linked());
      Displacement disp = disp_at(&p);
      disp.link_to(appendix);
      disp_at_put(&p, disp);
      p.Unuse();  // to avoid assertion failure in ~Label
    } else {
      // L is empty, simply use appendix
      *L = *appendix;
    }
  }
  appendix->Unuse();  // appendix should not be used anymore
}


void Assembler::bind(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = NULL;
  ASSERT(!L->is_bound());  // label can only be bound once
  if (FLAG_eliminate_jumps) {
    // Resolve unbound label.
    if (unbound_label_.is_linked()) {
      // Unbound label exists => link it with L if same binding
      // position, otherwise fix it.
      if (binding_pos_ == pc_offset()) {
        // Link it to L's list.
        link_to(L, &unbound_label_);
      } else {
        // Otherwise bind unbound label.
        ASSERT(binding_pos_ < pc_offset());
        bind_to(&unbound_label_, binding_pos_);
      }
    }
    ASSERT(!unbound_label_.is_linked());
    // try to eliminate jumps to next instruction
    const int absolute_jump_size = 5;
    // Do not remove an already bound jump target.
    while (last_bound_pos_ < pc_offset() &&
           reloc_info_writer.last_pc() <= pc_ - absolute_jump_size &&
           L->is_linked() &&
           (L->pos() + static_cast<int>(sizeof(int32_t)) == pc_offset()) &&
           (disp_at(L).type() == Displacement::UNCONDITIONAL_JUMP)) {
      // Previous instruction is jump jumping immediately after it =>
      // eliminate it.
      // jmp expected.
      ASSERT(byte_at(pc_offset() - absolute_jump_size) == 0xE9);
      if (FLAG_print_jump_elimination) {
        PrintF("@ %d jump to next eliminated\n", L->pos());
      }
      // Remove first entry from label list.
      Displacement disp = disp_at(L);
      disp.next(L);
      // Eliminate instruction (set code pointers back).
      pc_ -= absolute_jump_size;
      // Make sure not to skip relocation information when rewinding.
      ASSERT(reloc_info_writer.last_pc() <= pc_);
    }
    // Delay fixup of L => store it as unbound label.
    unbound_label_ = *L;
    binding_pos_ = pc_offset();
    L->Unuse();
  }
  bind_to(L, pc_offset());
}


void Assembler::call(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (L->is_bound()) {
    const int long_size = 5;
    int offs = L->pos() - pc_offset();
    ASSERT(offs <= 0);
    // 1110 1000 #32-bit disp
    EMIT(0xE8);
    emit(offs - long_size);
  } else {
    // 1110 1000 #32-bit disp
    EMIT(0xE8);
    emit_disp(L, Displacement::OTHER);
  }
}


void Assembler::call(byte* entry, RelocMode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(!is_code_target(rmode));
  EMIT(0xE8);
  emit(entry - (pc_ + sizeof(int32_t)), rmode);
}


void Assembler::call(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFF);
  emit_operand(edx, adr);
}


void Assembler::call(Handle<Code> code,  RelocMode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_code_target(rmode));
  EMIT(0xE8);
  emit(reinterpret_cast<intptr_t>(code.location()), rmode);
}


void Assembler::jmp(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (L->is_bound()) {
    const int short_size = 2;
    const int long_size  = 5;
    int offs = L->pos() - pc_offset();
    ASSERT(offs <= 0);
    if (is_int8(offs - short_size)) {
      // 1110 1011 #8-bit disp
      EMIT(0xEB);
      EMIT((offs - short_size) & 0xFF);
    } else {
      // 1110 1001 #32-bit disp
      EMIT(0xE9);
      emit(offs - long_size);
    }
  } else {
    if (FLAG_eliminate_jumps &&
        unbound_label_.is_linked() &&
        binding_pos_ == pc_offset()) {
      // Current position is target of jumps
      if (FLAG_print_jump_elimination) {
        PrintF("eliminated jumps/calls to %d from ", binding_pos_);
        print(&unbound_label_);
      }
      link_to(L, &unbound_label_);
    }
    // 1110 1001 #32-bit disp
    EMIT(0xE9);
    emit_disp(L, Displacement::UNCONDITIONAL_JUMP);
  }
}


void Assembler::jmp(byte* entry, RelocMode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(!is_code_target(rmode));
  EMIT(0xE9);
  emit(entry - (pc_ + sizeof(int32_t)), rmode);
}


void Assembler::jmp(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xFF);
  emit_operand(esp, adr);
}


void Assembler::jmp(Handle<Code> code, RelocMode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_code_target(rmode));
  EMIT(0xE9);
  emit(reinterpret_cast<intptr_t>(code.location()), rmode);
}



void Assembler::j(Condition cc, Label* L, Hint hint) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(0 <= cc && cc < 16);
  if (FLAG_emit_branch_hints && hint != no_hint) EMIT(hint);
  if (L->is_bound()) {
    const int short_size = 2;
    const int long_size  = 6;
    int offs = L->pos() - pc_offset();
    ASSERT(offs <= 0);
    if (is_int8(offs - short_size)) {
      // 0111 tttn #8-bit disp
      EMIT(0x70 | cc);
      EMIT((offs - short_size) & 0xFF);
    } else {
      // 0000 1111 1000 tttn #32-bit disp
      EMIT(0x0F);
      EMIT(0x80 | cc);
      emit(offs - long_size);
    }
  } else {
    // 0000 1111 1000 tttn #32-bit disp
    // Note: could eliminate cond. jumps to this jump if condition
    //       is the same however, seems to be rather unlikely case.
    EMIT(0x0F);
    EMIT(0x80 | cc);
    emit_disp(L, Displacement::OTHER);
  }
}


void Assembler::j(Condition cc, byte* entry, RelocMode rmode, Hint hint) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT((0 <= cc) && (cc < 16));
  if (FLAG_emit_branch_hints && hint != no_hint) EMIT(hint);
  // 0000 1111 1000 tttn #32-bit disp
  EMIT(0x0F);
  EMIT(0x80 | cc);
  emit(entry - (pc_ + sizeof(int32_t)), rmode);
}


void Assembler::j(Condition cc, Handle<Code> code, Hint hint) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (FLAG_emit_branch_hints && hint != no_hint) EMIT(hint);
  // 0000 1111 1000 tttn #32-bit disp
  EMIT(0x0F);
  EMIT(0x80 | cc);
  emit(reinterpret_cast<intptr_t>(code.location()), code_target);
}


// FPU instructions


void Assembler::fld(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xD9, 0xC0, i);
}


void Assembler::fld1() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xE8);
}


void Assembler::fldz() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xEE);
}


void Assembler::fld_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  emit_operand(eax, adr);
}


void Assembler::fld_d(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDD);
  emit_operand(eax, adr);
}


void Assembler::fstp_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  emit_operand(ebx, adr);
}


void Assembler::fstp_d(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDD);
  emit_operand(ebx, adr);
}


void Assembler::fild_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDB);
  emit_operand(eax, adr);
}


void Assembler::fild_d(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDF);
  emit_operand(ebp, adr);
}


void Assembler::fistp_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDB);
  emit_operand(ebx, adr);
}


void Assembler::fist_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDB);
  emit_operand(edx, adr);
}


void Assembler::fistp_d(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDF);
  emit_operand(edi, adr);
}


void Assembler::fabs() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xE1);
}


void Assembler::fchs() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xE0);
}


void Assembler::fadd(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDC, 0xC0, i);
}


void Assembler::fsub(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDC, 0xE8, i);
}


void Assembler::fisub_s(const Operand& adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDA);
  emit_operand(esp, adr);
}


void Assembler::fmul(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDC, 0xC8, i);
}


void Assembler::fdiv(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDC, 0xF8, i);
}


void Assembler::faddp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDE, 0xC0, i);
}


void Assembler::fsubp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDE, 0xE8, i);
}


void Assembler::fsubrp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDE, 0xE0, i);
}


void Assembler::fmulp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDE, 0xC8, i);
}


void Assembler::fdivp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDE, 0xF8, i);
}


void Assembler::fprem() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xF8);
}


void Assembler::fprem1() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xF5);
}


void Assembler::fxch(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xD9, 0xC8, i);
}


void Assembler::fincstp() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xF7);
}


void Assembler::ffree(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDD, 0xC0, i);
}


void Assembler::ftst() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xE4);
}


void Assembler::fucomp(int i) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_farith(0xDD, 0xE8, i);
}


void Assembler::fucompp() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDA);
  EMIT(0xE9);
}


void Assembler::fcompp() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xDE);
  EMIT(0xD9);
}


void Assembler::fnstsw_ax() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xdF);
  EMIT(0xE0);
}


void Assembler::fwait() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x9B);
}


void Assembler::frndint() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xD9);
  EMIT(0xFC);
}


void Assembler::sahf() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x9E);
}


void Assembler::cvttss2si(Register dst, const Operand& src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF3);
  EMIT(0x0F);
  EMIT(0x2C);
  emit_operand(dst, src);
}


void Assembler::cvttsd2si(Register dst, const Operand& src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x2C);
  emit_operand(dst, src);
}


void Assembler::cvtsi2sd(XMMRegister dst, const Operand& src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x2A);
  emit_sse_operand(dst, src);
}


void Assembler::addsd(XMMRegister dst, XMMRegister src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x58);
  emit_sse_operand(dst, src);
}


void Assembler::mulsd(XMMRegister dst, XMMRegister src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x59);
  emit_sse_operand(dst, src);
}


void Assembler::subsd(XMMRegister dst, XMMRegister src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x5C);
  emit_sse_operand(dst, src);
}


void Assembler::divsd(XMMRegister dst, XMMRegister src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);
  EMIT(0x0F);
  EMIT(0x5E);
  emit_sse_operand(dst, src);
}


void Assembler::movdbl(XMMRegister dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  movsd(dst, src);
}


void Assembler::movdbl(const Operand& dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  movsd(dst, src);
}


void Assembler::movsd(const Operand& dst, XMMRegister src ) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);  // double
  EMIT(0x0F);
  EMIT(0x11);  // store
  emit_sse_operand(src, dst);
}


void Assembler::movsd(XMMRegister dst, const Operand& src) {
  ASSERT(CpuFeatures::IsEnabled(CpuFeatures::SSE2));
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF2);  // double
  EMIT(0x0F);
  EMIT(0x10);  // load
  emit_sse_operand(dst, src);
}


void Assembler::emit_sse_operand(XMMRegister reg, const Operand& adr) {
  Register ireg = { reg.code() };
  emit_operand(ireg, adr);
}


void Assembler::emit_sse_operand(XMMRegister dst, XMMRegister src) {
  EMIT(0xC0 | dst.code() << 3 | src.code());
}


void Assembler::Print() {
  Disassembler::Decode(stdout, buffer_, pc_);
}


void Assembler::RecordJSReturn() {
  EnsureSpace ensure_space(this);
  RecordRelocInfo(js_return);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_debug_code) {
    EnsureSpace ensure_space(this);
    RecordRelocInfo(comment, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::RecordPosition(int pos) {
  if (pos == kNoPosition) return;
  ASSERT(position >= 0);
  if (pos == last_position_) return;
  EnsureSpace ensure_space(this);
  RecordRelocInfo(position, pos);
  last_position_ = pos;
  last_position_is_statement_ = false;
}


void Assembler::RecordStatementPosition(int pos) {
  if (pos == kNoPosition) return;
  ASSERT(position >= 0);
  if (pos == last_position_) return;
  EnsureSpace ensure_space(this);
  RecordRelocInfo(statement_position, pos);
  last_position_ = pos;
  last_position_is_statement_ = true;
}


void Assembler::GrowBuffer() {
  ASSERT(overflow());  // should not call this otherwise
  if (!own_buffer_) FATAL("external code buffer is too small");

  // compute new buffer size
  CodeDesc desc;  // the new buffer
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else {
    desc.buffer_size = 2*buffer_size_;
  }
  // Some internal data structures overflow for very large buffers,
  // they must ensure that kMaximalBufferSize is not too large.
  if ((desc.buffer_size > kMaximalBufferSize) ||
      (desc.buffer_size > Heap::OldGenerationSize())) {
    V8::FatalProcessOutOfMemory("Assembler::GrowBuffer");
  }

  // setup new buffer
  desc.buffer = NewArray<byte>(desc.buffer_size);
  desc.instr_size = pc_offset();
  desc.reloc_size = (buffer_ + buffer_size_) - (reloc_info_writer.pos());

  // Clear the buffer in debug mode. Use 'int3' instructions to make
  // sure to get into problems if we ever run uninitialized code.
  if (kDebug) {
    memset(desc.buffer, 0xCC, desc.buffer_size);
  }

  // copy the data
  int pc_delta = desc.buffer - buffer_;
  int rc_delta = (desc.buffer + desc.buffer_size) - (buffer_ + buffer_size_);
  memmove(desc.buffer, buffer_, desc.instr_size);
  memmove(rc_delta + reloc_info_writer.pos(),
          reloc_info_writer.pos(), desc.reloc_size);

  // switch buffers
  if (spare_buffer_ == NULL && buffer_size_ == kMinimalBufferSize) {
    spare_buffer_ = buffer_;
  } else {
    DeleteArray(buffer_);
  }
  buffer_ = desc.buffer;
  buffer_size_ = desc.buffer_size;
  pc_ += pc_delta;
  if (last_pc_ != NULL) {
    last_pc_ += pc_delta;
  }
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // relocate runtime entries
  for (RelocIterator it(desc); !it.done(); it.next()) {
    RelocMode rmode = it.rinfo()->rmode();
    if (rmode == runtime_entry) {
      int32_t* p = reinterpret_cast<int32_t*>(it.rinfo()->pc());
      *p -= pc_delta;  // relocate entry
    }
  }

  ASSERT(!overflow());
}


void Assembler::emit_arith_b(int op1, int op2, Register dst, int imm8) {
  ASSERT(is_uint8(op1) && is_uint8(op2));  // wrong opcode
  ASSERT(is_uint8(imm8));
  ASSERT((op1 & 0x01) == 0);  // should be 8bit operation
  EMIT(op1);
  EMIT(op2 | dst.code());
  EMIT(imm8);
}


void Assembler::emit_arith(int sel, Operand dst, const Immediate& x) {
  ASSERT((0 <= sel) && (sel <= 7));
  Register ireg = { sel };
  if (x.is_int8()) {
    EMIT(0x83);  // using a sign-extended 8-bit immediate.
    emit_operand(ireg, dst);
    EMIT(x.x_ & 0xFF);
  } else if (dst.is_reg(eax)) {
    EMIT((sel << 3) | 0x05);  // short form if the destination is eax.
    emit(x);
  } else {
    EMIT(0x81);  // using a literal 32-bit immediate.
    emit_operand(ireg, dst);
    emit(x);
  }
}


void Assembler::emit_operand(Register reg, const Operand& adr) {
  adr.set_reg(reg);
  memmove(pc_, adr.buf_, adr.len_);
  pc_ += adr.len_;
  if (adr.len_ >= sizeof(int32_t) && adr.rmode_ != no_reloc) {
    pc_ -= sizeof(int32_t);  // pc_ must be *at* disp32
    RecordRelocInfo(adr.rmode_);
    pc_ += sizeof(int32_t);
  }
}


void Assembler::emit_operand(const Operand& adr, Register reg) {
  adr.set_reg(reg);
  memmove(pc_, adr.buf_, adr.len_);
  pc_ += adr.len_;
  if (adr.len_ >= sizeof(int32_t) && adr.rmode_ != no_reloc) {
    pc_ -= sizeof(int32_t);  // pc_ must be *at* disp32
    RecordRelocInfo(adr.rmode_);
    pc_ += sizeof(int32_t);
  }
}


void Assembler::emit_farith(int b1, int b2, int i) {
  ASSERT(is_uint8(b1) && is_uint8(b2));  // wrong opcode
  ASSERT(0 <= i &&  i < 8);  // illegal stack offset
  EMIT(b1);
  EMIT(b2 + i);
}


void Assembler::RecordRelocInfo(RelocMode rmode, intptr_t data) {
  ASSERT(rmode != no_reloc);
  RelocInfo rinfo(pc_, rmode, data);
  reloc_info_writer.Write(&rinfo);
}


} }  // namespace v8::internal
