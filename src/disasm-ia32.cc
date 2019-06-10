// Copyright 2007-2008 Google Inc. All Rights Reserved.
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

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#ifndef WIN32
#include <stdint.h>
#endif
#include "disasm.h"

namespace disasm {

// Windows is missing the stdint.h header file
#ifdef WIN32
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef int int32_t;
#endif


#define UNIMPLEMENTED() \
  assert(false)

#define UNREACHABLE() \
  assert(false)

enum OperandOrder {
  UNSET_OP_ORDER = 0,
  REG_OPER_OP_ORDER,
  OPER_REG_OP_ORDER
};


//------------------------------------------------------------------
// Tables
//------------------------------------------------------------------
struct ByteMnemonic {
  int b;  // -1 terminates, otherwise must be in range (0..255)
  const char* mnem;
  OperandOrder op_order_;
};


static ByteMnemonic two_operands_instr[] = {
  {0x03, "add", REG_OPER_OP_ORDER},
  {0x21, "and", OPER_REG_OP_ORDER},
  {0x23, "and", REG_OPER_OP_ORDER},
  {0x3B, "cmp", REG_OPER_OP_ORDER},
  {0x8D, "lea", REG_OPER_OP_ORDER},
  {0x09, "or", OPER_REG_OP_ORDER},
  {0x0B, "or", REG_OPER_OP_ORDER},
  {0x1B, "sbb", REG_OPER_OP_ORDER},
  {0x29, "sub", OPER_REG_OP_ORDER},
  {0x2B, "sub", REG_OPER_OP_ORDER},
  {0x85, "test", REG_OPER_OP_ORDER},
  {0x31, "xor", OPER_REG_OP_ORDER},
  {0x33, "xor", REG_OPER_OP_ORDER},
  {0x8A, "mov_b", REG_OPER_OP_ORDER},
  {0x8B, "mov", REG_OPER_OP_ORDER},
  {-1, "", UNSET_OP_ORDER}
};


static ByteMnemonic zero_operands_instr[] = {
  {0xC3, "ret", UNSET_OP_ORDER},
  {0xC9, "leave", UNSET_OP_ORDER},
  {0x90, "nop", UNSET_OP_ORDER},
  {0xF4, "hlt", UNSET_OP_ORDER},
  {0xCC, "int3", UNSET_OP_ORDER},
  {0x60, "pushad", UNSET_OP_ORDER},
  {0x61, "popad", UNSET_OP_ORDER},
  {0x9C, "pushfd", UNSET_OP_ORDER},
  {0x9D, "popfd", UNSET_OP_ORDER},
  {0x9E, "sahf", UNSET_OP_ORDER},
  {0x99, "cdq", UNSET_OP_ORDER},
  {0x9B, "fwait", UNSET_OP_ORDER},
  {-1, "", UNSET_OP_ORDER}
};


static ByteMnemonic call_jump_instr[] = {
  {0xE8, "call", UNSET_OP_ORDER},
  {0xE9, "jmp", UNSET_OP_ORDER},
  {-1, "", UNSET_OP_ORDER}
};


static ByteMnemonic short_immediate_instr[] = {
  {0x05, "add", UNSET_OP_ORDER},
  {0x0D, "or", UNSET_OP_ORDER},
  {0x15, "adc", UNSET_OP_ORDER},
  {0x25, "and", UNSET_OP_ORDER},
  {0x2D, "sub", UNSET_OP_ORDER},
  {0x35, "xor", UNSET_OP_ORDER},
  {0x3D, "cmp", UNSET_OP_ORDER},
  {-1, "", UNSET_OP_ORDER}
};


static const char* jump_conditional_mnem[] = {
  /*0*/ "jo", "jno", "jc", "jnc",
  /*4*/ "jz", "jnz", "jna", "ja",
  /*8*/ "js", "jns", "jpe", "jpo",
  /*12*/ "jl", "jnl", "jng", "jg"
};


enum InstructionType {
  NO_INSTR,
  ZERO_OPERANDS_INSTR,
  TWO_OPERANDS_INSTR,
  JUMP_CONDITIONAL_SHORT_INSTR,
  REGISTER_INSTR,
  MOVE_REG_INSTR,
  CALL_JUMP_INSTR,
  SHORT_IMMEDIATE_INSTR
};


struct InstructionDesc {
  const char* mnem;
  InstructionType type;
  OperandOrder op_order_;
};


class InstructionTable {
 public:
  InstructionTable();
  const InstructionDesc& Get(byte x) const { return instructions_[x]; }

 private:
  InstructionDesc instructions_[256];
  void Clear();
  void Init();
  void CopyTable(ByteMnemonic bm[], InstructionType type);
  void SetTableRange(InstructionType type,
                     byte start,
                     byte end,
                     const char* mnem);
  void AddJumpConditionalShort();
};


InstructionTable::InstructionTable() {
  Clear();
  Init();
}


void InstructionTable::Clear() {
  for (int i = 0; i < 256; i++) {
    instructions_[i].mnem = "";
    instructions_[i].type = NO_INSTR;
    instructions_[i].op_order_ = UNSET_OP_ORDER;
  }
}


void InstructionTable::Init() {
  CopyTable(two_operands_instr, TWO_OPERANDS_INSTR);
  CopyTable(zero_operands_instr, ZERO_OPERANDS_INSTR);
  CopyTable(call_jump_instr, CALL_JUMP_INSTR);
  CopyTable(short_immediate_instr, SHORT_IMMEDIATE_INSTR);
  AddJumpConditionalShort();
  SetTableRange(REGISTER_INSTR, 0x40, 0x47, "inc");
  SetTableRange(REGISTER_INSTR, 0x48, 0x4F, "dec");
  SetTableRange(REGISTER_INSTR, 0x50, 0x57, "push");
  SetTableRange(REGISTER_INSTR, 0x58, 0x5F, "pop");
  SetTableRange(MOVE_REG_INSTR, 0xB8, 0xBF, "mov");
}


void InstructionTable::CopyTable(ByteMnemonic bm[], InstructionType type) {
  for (int i = 0; bm[i].b >= 0; i++) {
    InstructionDesc* id = &instructions_[bm[i].b];
    id->mnem = bm[i].mnem;
    id->op_order_ = bm[i].op_order_;
    assert(id->type == NO_INSTR);  // Information already entered
    id->type = type;
  }
}


void InstructionTable::SetTableRange(InstructionType type,
                                     byte start,
                                     byte end,
                                     const char* mnem) {
  for (byte b = start; b <= end; b++) {
    InstructionDesc* id = &instructions_[b];
    assert(id->type == NO_INSTR);  // Information already entered
    id->mnem = mnem;
    id->type = type;
  }
}


void InstructionTable::AddJumpConditionalShort() {
  for (byte b = 0x70; b <= 0x7F; b++) {
    InstructionDesc* id = &instructions_[b];
    assert(id->type == NO_INSTR);  // Information already entered
    id->mnem = jump_conditional_mnem[b & 0x0F];
    id->type = JUMP_CONDITIONAL_SHORT_INSTR;
  }
}


static InstructionTable instruction_table;


// The IA32 disassembler implementation.
class DisassemblerIA32 {
 public:
  DisassemblerIA32(const NameConverter& converter,
                   bool abort_on_unimplemented = true)
      : converter_(converter),
        tmp_buffer_pos_(0),
        abort_on_unimplemented_(abort_on_unimplemented) {
    tmp_buffer_[0] = '\0';
  }

  virtual ~DisassemblerIA32() {}

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(char* buffer, const int buffer_size, byte* instruction);

 private:
  const NameConverter& converter_;
  char tmp_buffer_[128];
  unsigned int tmp_buffer_pos_;
  bool abort_on_unimplemented_;


  enum {
    eax = 0,
    ecx = 1,
    edx = 2,
    ebx = 3,
    esp = 4,
    ebp = 5,
    esi = 6,
    edi = 7
  };


  const char* NameOfCPURegister(int reg) const {
    return converter_.NameOfCPURegister(reg);
  }


  const char* NameOfXMMRegister(int reg) const {
    return converter_.NameOfXMMRegister(reg);
  }


  const char* NameOfAddress(byte* addr) const {
    return converter_.NameOfAddress(addr);
  }


  // Disassembler helper functions.
  static void get_modrm(byte data, int* mod, int* regop, int* rm) {
    *mod = (data >> 6) & 3;
    *regop = (data & 0x38) >> 3;
    *rm = data & 7;
  }


  static void get_sib(byte data, int* scale, int* index, int* base) {
    *scale = (data >> 6) & 3;
    *index = (data >> 3) & 7;
    *base = data & 7;
  }


  int PrintRightOperand(byte* modrmp);
  int PrintOperands(const char* mnem, OperandOrder op_order, byte* data);
  int PrintImmediateOp(byte* data);
  int F7Instruction(byte* data);
  int D1D3C1Instruction(byte* data);
  int JumpShort(byte* data);
  int JumpConditional(byte* data, const char* comment);
  int JumpConditionalShort(byte* data, const char* comment);
  int FPUInstruction(byte* data);
  void AppendToBuffer(const char* format, ...);


  void UnimplementedInstruction() {
    if (abort_on_unimplemented_) {
      UNIMPLEMENTED();
    } else {
      AppendToBuffer("'Unimplemented Instruction'");
    }
  }
};


void DisassemblerIA32::AppendToBuffer(const char* format, ...) {
  char* str = tmp_buffer_ + tmp_buffer_pos_;
  int size = (sizeof tmp_buffer_) - tmp_buffer_pos_;
  va_list args;
  va_start(args, format);
#ifdef WIN32
  int result = _vsnprintf(str, size, format, args);
#else
  int result = vsnprintf(str, size, format, args);
#endif
  va_end(args);
  tmp_buffer_pos_ += result;
}


// Returns number of bytes used including the current *modrmp.
// Writes instruction's right operand to 'tmp_buffer_'.
int DisassemblerIA32::PrintRightOperand(byte* modrmp) {
  int mod, regop, rm;
  get_modrm(*modrmp, &mod, &regop, &rm);
  switch (mod) {
    case 0:
      if (rm == ebp) {
        int32_t disp = *reinterpret_cast<int32_t*>(modrmp+1);
        AppendToBuffer("[0x%x]", disp);
        return 5;
      } else if (rm == esp) {
        byte sib = *(modrmp + 1);
        int scale, index, base;
        get_sib(sib, &scale, &index, &base);
        if (index == esp && base == esp && scale == 0 /*times_1*/) {
          AppendToBuffer("[%s]", NameOfCPURegister(rm));
          return 2;
        } else if (base == ebp) {
          int32_t disp = *reinterpret_cast<int32_t*>(modrmp + 2);
          AppendToBuffer("[%s*%d+0x%x]",
                         NameOfCPURegister(index),
                         1 << scale,
                         disp);
          return 6;
        } else if (index != esp && base != ebp) {
          // [base+index*scale]
          AppendToBuffer("[%s+%s*%d]",
                         NameOfCPURegister(base),
                         NameOfCPURegister(index),
                         1 << scale);
          return 2;
        } else {
          UnimplementedInstruction();
          return 1;
        }
      } else {
        AppendToBuffer("[%s]", NameOfCPURegister(rm));
        return 1;
      }
      break;
    case 1:  // fall through
    case 2:
      if (rm == esp) {
        byte sib = *(modrmp + 1);
        int scale, index, base;
        get_sib(sib, &scale, &index, &base);
        int disp =
            mod == 2 ? *reinterpret_cast<int32_t*>(modrmp + 2) : *(modrmp + 2);
        if (index == base && index == rm /*esp*/ && scale == 0 /*times_1*/) {
          AppendToBuffer("[%s+0x%x]", NameOfCPURegister(rm), disp);
        } else {
          AppendToBuffer("[%s+%s*%d+0x%x]",
                         NameOfCPURegister(base),
                         NameOfCPURegister(index),
                         1 << scale,
                         disp);
        }
        return mod == 2 ? 6 : 3;
      } else {
        // No sib.
        int disp =
            mod == 2 ? *reinterpret_cast<int32_t*>(modrmp + 1) : *(modrmp + 1);
        AppendToBuffer("[%s+0x%x]", NameOfCPURegister(rm), disp);
        return mod == 2 ? 5 : 2;
      }
      break;
    case 3:
      AppendToBuffer("%s", NameOfCPURegister(rm));
      return 1;
    default:
      UnimplementedInstruction();
      return 1;
  }
  UNREACHABLE();
}


// Returns number of bytes used including the current *data.
// Writes instruction's mnemonic, left and right operands to 'tmp_buffer_'.
int DisassemblerIA32::PrintOperands(const char* mnem,
                                    OperandOrder op_order,
                                    byte* data) {
  byte modrm = *data;
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  int advance = 0;
  switch (op_order) {
    case REG_OPER_OP_ORDER: {
      AppendToBuffer("%s %s,", mnem, NameOfCPURegister(regop));
      advance = PrintRightOperand(data);
      break;
    }
    case OPER_REG_OP_ORDER: {
      AppendToBuffer("%s ", mnem);
      advance = PrintRightOperand(data);
      AppendToBuffer(",%s", NameOfCPURegister(regop));
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  return advance;
}


// Returns number of bytes used by machine instruction, including *data byte.
// Writes immediate instructions to 'tmp_buffer_'.
int DisassemblerIA32::PrintImmediateOp(byte* data) {
  bool sign_extension_bit = (*data & 0x02) != 0;
  byte modrm = *(data+1);
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  const char* mnem = "Imm???";
  switch (regop) {
    case 0: mnem = "add"; break;
    case 1: mnem = "or"; break;
    case 2: mnem = "adc"; break;
    case 4: mnem = "and"; break;
    case 5: mnem = "sub"; break;
    case 6: mnem = "xor"; break;
    case 7: mnem = "cmp"; break;
    default: UnimplementedInstruction();
  }
  AppendToBuffer("%s ", mnem);
  int count = PrintRightOperand(data+1);
  if (sign_extension_bit) {
    AppendToBuffer(",0x%x", *(data + 1 + count));
    return 1 + count + 1 /*int8*/;
  } else {
    AppendToBuffer(",0x%x", *reinterpret_cast<int32_t*>(data + 1 + count));
    return 1 + count + 4 /*int32_t*/;
  }
}


// Returns number of bytes used, including *data.
int DisassemblerIA32::F7Instruction(byte* data) {
  assert(*data == 0xF7);
  byte modrm = *(data+1);
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  if (mod == 3 && regop != 0) {
    const char* mnem = NULL;
    switch (regop) {
      case 2: mnem = "not"; break;
      case 3: mnem = "neg"; break;
      case 4: mnem = "mul"; break;
      case 7: mnem = "idiv"; break;
      default: UnimplementedInstruction();
    }
    AppendToBuffer("%s %s", mnem, NameOfCPURegister(rm));
    return 2;
  } else if (mod == 3 && regop == eax) {
    int32_t imm = *reinterpret_cast<int32_t*>(data+2);
    AppendToBuffer("test %s,0x%x", NameOfCPURegister(rm), imm);
    return 6;
  } else if (regop == eax) {
    AppendToBuffer("test ");
    int count = PrintRightOperand(data+1);
    int32_t imm = *reinterpret_cast<int32_t*>(data+1+count);
    AppendToBuffer(",0x%x", imm);
    return 1+count+4 /*int32_t*/;
  } else {
    UnimplementedInstruction();
    return 2;
  }
}

int DisassemblerIA32::D1D3C1Instruction(byte* data) {
  byte op = *data;
  assert(op == 0xD1 || op == 0xD3 || op == 0xC1);
  byte modrm = *(data+1);
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  int imm8 = -1;
  int num_bytes = 2;
  if (mod == 3) {
    const char* mnem = NULL;
    if (op == 0xD1) {
      imm8 = 1;
      switch (regop) {
        case edx: mnem = "rcl"; break;
        case edi: mnem = "sar"; break;
        case esp: mnem = "shl"; break;
        default: UnimplementedInstruction();
      }
    } else if (op == 0xC1) {
      imm8 = *(data+2);
      num_bytes = 3;
      switch (regop) {
        case edx: mnem = "rcl"; break;
        case esp: mnem = "shl"; break;
        case ebp: mnem = "shr"; break;
        case edi: mnem = "sar"; break;
        default: UnimplementedInstruction();
      }
    } else if (op == 0xD3) {
      switch (regop) {
        case esp: mnem = "shl"; break;
        case ebp: mnem = "shr"; break;
        case edi: mnem = "sar"; break;
        default: UnimplementedInstruction();
      }
    }
    assert(mnem != NULL);
    AppendToBuffer("%s %s,", mnem, NameOfCPURegister(rm));
    if (imm8 > 0) {
      AppendToBuffer("%d", imm8);
    } else {
      AppendToBuffer("cl");
    }
  } else {
    UnimplementedInstruction();
  }
  return num_bytes;
}


// Returns number of bytes used, including *data.
int DisassemblerIA32::JumpShort(byte* data) {
  assert(*data == 0xEB);
  byte b = *(data+1);
  byte* dest = data + static_cast<int8_t>(b) + 2;
  AppendToBuffer("jmp %s", NameOfAddress(dest));
  return 2;
}


// Returns number of bytes used, including *data.
int DisassemblerIA32::JumpConditional(byte* data, const char* comment) {
  assert(*data == 0x0F);
  byte cond = *(data+1) & 0x0F;
  byte* dest = data + *reinterpret_cast<int32_t*>(data+2) + 6;
  const char* mnem = jump_conditional_mnem[cond];
  AppendToBuffer("%s %s", mnem, NameOfAddress(dest));
  if (comment != NULL) {
    AppendToBuffer(", %s", comment);
  }
  return 6;  // includes 0x0F
}


// Returns number of bytes used, including *data.
int DisassemblerIA32::JumpConditionalShort(byte* data, const char* comment) {
  byte cond = *data & 0x0F;
  byte b = *(data+1);
  byte* dest = data + static_cast<int8_t>(b) + 2;
  const char* mnem = jump_conditional_mnem[cond];
  AppendToBuffer("%s %s", mnem, NameOfAddress(dest));
  if (comment != NULL) {
    AppendToBuffer(", %s", comment);
  }
  return 2;
}


// Returns number of bytes used, including *data.
int DisassemblerIA32::FPUInstruction(byte* data) {
  byte b1 = *data;
  byte b2 = *(data + 1);
  if (b1 == 0xD9) {
    const char* mnem = NULL;
    switch (b2) {
      case 0xE8: mnem = "fld1"; break;
      case 0xEE: mnem = "fldz"; break;
      case 0xE1: mnem = "fabs"; break;
      case 0xE0: mnem = "fchs"; break;
      case 0xF8: mnem = "fprem"; break;
      case 0xF5: mnem = "fprem1"; break;
      case 0xF7: mnem = "fincstp"; break;
      case 0xE4: mnem = "ftst"; break;
    }
    if (mnem != NULL) {
      AppendToBuffer("%s", mnem);
      return 2;
    } else if ((b2 & 0xF8) == 0xC8) {
      AppendToBuffer("fxch st%d", b2 & 0x7);
      return 2;
    } else {
      int mod, regop, rm;
      get_modrm(*(data+1), &mod, &regop, &rm);
      const char* mnem = "?";
      switch (regop) {
        case eax: mnem = "fld_s"; break;
        case ebx: mnem = "fstp_s"; break;
        default: UnimplementedInstruction();
      }
      AppendToBuffer("%s ", mnem);
      int count = PrintRightOperand(data + 1);
      return count + 1;
    }
  } else if (b1 == 0xDD) {
    if ((b2 & 0xF8) == 0xC0) {
      AppendToBuffer("ffree st%d", b2 & 0x7);
      return 2;
    } else {
      int mod, regop, rm;
      get_modrm(*(data+1), &mod, &regop, &rm);
      const char* mnem = "?";
      switch (regop) {
        case eax: mnem = "fld_d"; break;
        case ebx: mnem = "fstp_d"; break;
        default: UnimplementedInstruction();
      }
      AppendToBuffer("%s ", mnem);
      int count = PrintRightOperand(data + 1);
      return count + 1;
    }
  } else if (b1 == 0xDB) {
    int mod, regop, rm;
    get_modrm(*(data+1), &mod, &regop, &rm);
    const char* mnem = "?";
    switch (regop) {
      case eax: mnem = "fild_s"; break;
      case ebx: mnem = "fistp_s"; break;
      default: UnimplementedInstruction();
    }
    AppendToBuffer("%s ", mnem);
    int count = PrintRightOperand(data + 1);
    return count + 1;
  } else if (b1 == 0xDF) {
    if (b2 == 0xE0) {
      AppendToBuffer("fnstsw_ax");
      return 2;
    }
    int mod, regop, rm;
    get_modrm(*(data+1), &mod, &regop, &rm);
    const char* mnem = "?";
    switch (regop) {
      case ebp: mnem = "fild_d"; break;
      case edi: mnem = "fistp_d"; break;
      default: UnimplementedInstruction();
    }
    AppendToBuffer("%s ", mnem);
    int count = PrintRightOperand(data + 1);
    return count + 1;
  } else if (b1 == 0xDC || b1 == 0xDE) {
    bool is_pop = (b1 == 0xDE);
    if (is_pop && b2 == 0xD9) {
      AppendToBuffer("fcompp");
      return 2;
    }
    const char* mnem = "FP0xDC";
    switch (b2 & 0xF8) {
      case 0xC0: mnem = "fadd"; break;
      case 0xE8: mnem = "fsub"; break;
      case 0xC8: mnem = "fmul"; break;
      case 0xF8: mnem = "fdiv"; break;
      default: UnimplementedInstruction();
    }
    AppendToBuffer("%s%s st%d", mnem, is_pop ? "p" : "", b2 & 0x7);
    return 2;
  }
  AppendToBuffer("Unknown FP instruction");
  return 2;
}


// Mnemonics for instructions 0xF0 byte.
// Returns NULL if the instruction is not handled here.
static const char* F0Mnem(byte f0byte) {
  switch (f0byte) {
    case 0xA2: return "cpuid";
    case 0x31: return "rdtsc";
    case 0xBE: return "movsx_b";
    case 0xBF: return "movsx_w";
    case 0xB6: return "movzx_b";
    case 0xB7: return "movzx_w";
    case 0xAF: return "imul";
    case 0xA5: return "shld";
    case 0xAD: return "shrd";
    case 0xAB: return "bts";
    default: return NULL;
  }
}


// Disassembled instruction '*instr' and writes it intro 'out_buffer'.
int DisassemblerIA32::InstructionDecode(char* out_buffer,
                                        const int out_buffer_size,
                                        byte* instr) {
  tmp_buffer_pos_ = 0;  // starting to write as position 0
  byte* data = instr;
  // Check for hints.
  const char* branch_hint = NULL;
  // We use this two prefixes only with branch prediction
  if (*data == 0x3E /*ds*/) {
    branch_hint = "predicted taken";
    data++;
  } else if (*data == 0x2E /*cs*/) {
    branch_hint = "predicted not taken";
    data++;
  }
  bool processed = true;  // Will be set to false if the current instruction
                          // is not in 'instructions' table.
  const InstructionDesc& idesc = instruction_table.Get(*data);
  switch (idesc.type) {
    case ZERO_OPERANDS_INSTR:
      AppendToBuffer(idesc.mnem);
      data++;
      break;

    case TWO_OPERANDS_INSTR:
      data++;
      data += PrintOperands(idesc.mnem, idesc.op_order_, data);
      break;

    case JUMP_CONDITIONAL_SHORT_INSTR:
      data += JumpConditionalShort(data, branch_hint);
      break;

    case REGISTER_INSTR:
      AppendToBuffer("%s %s", idesc.mnem, NameOfCPURegister(*data & 0x07));
      data++;
      break;

    case MOVE_REG_INSTR: {
      byte* addr = reinterpret_cast<byte*>(*reinterpret_cast<int32_t*>(data+1));
      AppendToBuffer("mov %s,%s",
                     NameOfCPURegister(*data & 0x07),
                     NameOfAddress(addr));
      data += 5;
      break;
    }

    case CALL_JUMP_INSTR: {
      byte* addr = data + *reinterpret_cast<int32_t*>(data+1) + 5;
      AppendToBuffer("%s %s", idesc.mnem, NameOfAddress(addr));
      data += 5;
      break;
    }

    case SHORT_IMMEDIATE_INSTR: {
      byte* addr = reinterpret_cast<byte*>(*reinterpret_cast<int32_t*>(data+1));
      AppendToBuffer("%s eax, %s", idesc.mnem, NameOfAddress(addr));
      data += 5;
      break;
    }

    case NO_INSTR:
      processed = false;
      break;

    default:
      UNIMPLEMENTED();  // This type is not implemented.
  }
  //----------------------------
  if (!processed) {
    switch (*data) {
      case 0xC2:
        AppendToBuffer("ret 0x%x", *reinterpret_cast<uint16_t*>(data+1));
        data += 3;
        break;

      case 0x69:  // fall through
      case 0x6B:
        { int mod, regop, rm;
          get_modrm(*(data+1), &mod, &regop, &rm);
          int32_t imm =
              *data == 0x6B ? *(data+2) : *reinterpret_cast<int32_t*>(data+2);
          AppendToBuffer("imul %s,%s,0x%x",
                         NameOfCPURegister(regop),
                         NameOfCPURegister(rm),
                         imm);
          data += 2 + (*data == 0x6B ? 1 : 4);
        }
        break;

      case 0xF6:
        { int mod, regop, rm;
          get_modrm(*(data+1), &mod, &regop, &rm);
          if (mod == 3 && regop == eax) {
            AppendToBuffer("test_b %s,%d", NameOfCPURegister(rm), *(data+2));
          } else {
            UnimplementedInstruction();
          }
          data += 3;
        }
        break;

      case 0x81:  // fall through
      case 0x83:  // 0x81 with sign extension bit set
        data += PrintImmediateOp(data);
        break;

      case 0x0F:
        { byte f0byte = *(data+1);
          const char* f0mnem = F0Mnem(f0byte);
          if (f0byte == 0xA2 || f0byte == 0x31) {
            AppendToBuffer("%s", f0mnem);
            data += 2;
          } else if ((f0byte & 0xF0) == 0x80) {
            data += JumpConditional(data, branch_hint);
          } else if (f0byte == 0xBE || f0byte == 0xBF || f0byte == 0xB6 ||
                     f0byte == 0xB7 || f0byte == 0xAF) {
            data += 2;
            data += PrintOperands(f0mnem, REG_OPER_OP_ORDER, data);
          } else {
            data += 2;
            if (f0byte == 0xAB || f0byte == 0xA5 || f0byte == 0xAD) {
              // shrd, shld, bts
              AppendToBuffer("%s ", f0mnem);
              int mod, regop, rm;
              get_modrm(*data, &mod, &regop, &rm);
              data += PrintRightOperand(data);
              if (f0byte == 0xAB) {
                AppendToBuffer(",%s", NameOfCPURegister(regop));
              } else {
                AppendToBuffer(",%s,cl", NameOfCPURegister(regop));
              }
            } else {
              UnimplementedInstruction();
            }
          }
        }
        break;

      case 0x8F:
        { data++;
          int mod, regop, rm;
          get_modrm(*data, &mod, &regop, &rm);
          if (regop == eax) {
            AppendToBuffer("pop ");
            data += PrintRightOperand(data);
          }
        }
        break;

      case 0xFF:
        { data++;
          int mod, regop, rm;
          get_modrm(*data, &mod, &regop, &rm);
          const char* mnem = NULL;
          switch (regop) {
            case esi: mnem = "push"; break;
            case eax: mnem = "inc"; break;
            case edx: mnem = "call"; break;
            case esp: mnem = "jmp"; break;
            default: mnem = "???";
          }
          AppendToBuffer("%s ", mnem);
          data += PrintRightOperand(data);
        }
        break;

      case 0xC7:  // imm32, fall through
      case 0xC6:  // imm8
        { bool is_byte = *data == 0xC6;
          data++;
          AppendToBuffer("%s ", is_byte ? "mov_b" : "mov");
          data += PrintRightOperand(data);
          int32_t imm = is_byte ? *data : *reinterpret_cast<int32_t*>(data);
          AppendToBuffer(",0x%x", imm);
          data += is_byte ? 1 : 4;
        }
        break;

      case 0x88:  // 8bit, fall through
      case 0x89:  // 32bit
        { bool is_byte = *data == 0x88;
          int mod, regop, rm;
          data++;
          get_modrm(*data, &mod, &regop, &rm);
          AppendToBuffer("%s ", is_byte ? "mov_b" : "mov");
          data += PrintRightOperand(data);
          AppendToBuffer(",%s", NameOfCPURegister(regop));
        }
        break;

      case 0x66:  // prefix
        data++;
        if (*data == 0x8B) {
          data++;
          data += PrintOperands("mov_w", REG_OPER_OP_ORDER, data);
        } else if (*data == 0x89) {
          data++;
          int mod, regop, rm;
          get_modrm(*data, &mod, &regop, &rm);
          AppendToBuffer("mov_w ");
          data += PrintRightOperand(data);
          AppendToBuffer(",%s", NameOfCPURegister(regop));
        } else {
          UnimplementedInstruction();
        }
        break;

      case 0xFE:
        { data++;
          int mod, regop, rm;
          get_modrm(*data, &mod, &regop, &rm);
          if (mod == 3 && regop == ecx) {
            AppendToBuffer("dec_b %s", NameOfCPURegister(rm));
          } else {
            UnimplementedInstruction();
          }
          data++;
        }
        break;

      case 0x68:
        AppendToBuffer("push 0x%x", *reinterpret_cast<int32_t*>(data+1));
        data += 5;
        break;

      case 0x6A:
        AppendToBuffer("push 0x%x", *reinterpret_cast<int8_t*>(data + 1));
        data += 2;
        break;

      case 0xA8:
        AppendToBuffer("test al,0x%x", *reinterpret_cast<uint8_t*>(data+1));
        data += 2;
        break;

      case 0xA9:
        AppendToBuffer("test eax,0x%x", *reinterpret_cast<int32_t*>(data+1));
        data += 5;
        break;

      case 0xD1:  // fall through
      case 0xD3:  // fall through
      case 0xC1:
        data += D1D3C1Instruction(data);
        break;

      case 0xD9:  // fall through
      case 0xDB:  // fall through
      case 0xDC:  // fall through
      case 0xDD:  // fall through
      case 0xDE:  // fall through
      case 0xDF:
        data += FPUInstruction(data);
        break;

      case 0xEB:
        data += JumpShort(data);
        break;

      case 0xF2:
        if (*(data+1) == 0x0F) {
          byte b2 = *(data+2);
          if (b2 == 0x11) {
            AppendToBuffer("movsd ");
            data += 3;
            int mod, regop, rm;
            get_modrm(*data, &mod, &regop, &rm);
            data += PrintRightOperand(data);
            AppendToBuffer(",%s", NameOfXMMRegister(regop));
          } else if (b2 == 0x10) {
            data += 3;
            int mod, regop, rm;
            get_modrm(*data, &mod, &regop, &rm);
            AppendToBuffer("movsd %s,", NameOfXMMRegister(regop));
            data += PrintRightOperand(data);
          } else {
            const char* mnem = "?";
            switch (b2) {
              case 0x2A: mnem = "cvtsi2sd"; break;
              case 0x58: mnem = "addsd"; break;
              case 0x59: mnem = "mulsd"; break;
              case 0x5C: mnem = "subsd"; break;
              case 0x5E: mnem = "divsd"; break;
            }
            data += 3;
            int mod, regop, rm;
            get_modrm(*data, &mod, &regop, &rm);
            if (b2 == 0x2A) {
              AppendToBuffer("%s %s,", mnem, NameOfXMMRegister(regop));
              data += PrintRightOperand(data);
            } else {
              AppendToBuffer("%s %s,%s",
                             mnem,
                             NameOfXMMRegister(regop),
                             NameOfXMMRegister(rm));
              data++;
            }
          }
        } else {
          UnimplementedInstruction();
        }
        break;

      case 0xF3:
        if (*(data+1) == 0x0F && *(data+2) == 0x2C) {
          data += 3;
          data += PrintOperands("cvttss2si", REG_OPER_OP_ORDER, data);
        } else {
          UnimplementedInstruction();
        }
        break;

      case 0xF7:
        data += F7Instruction(data);
        break;

      default:
        UnimplementedInstruction();
    }
  }

  if (tmp_buffer_pos_ < sizeof tmp_buffer_) {
    tmp_buffer_[tmp_buffer_pos_] = '\0';
  }

  int instr_len = data - instr;
  if (instr_len == 0) instr_len = 1;  // parse at least a byte
#ifdef WIN32
  _snprintf(out_buffer, out_buffer_size, "%s", tmp_buffer_);
#else
  snprintf(out_buffer, out_buffer_size, "%s", tmp_buffer_);
#endif
  return instr_len;
}


//------------------------------------------------------------------------------


static const char* cpu_regs[8] = {
  "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
};


static const char* xmm_regs[8] = {
  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
};


const char* NameConverter::NameOfAddress(byte* addr) const {
  static char tmp_buffer[32];
#ifdef WIN32
  _snprintf(tmp_buffer, sizeof tmp_buffer, "%p", addr);
#else
  snprintf(tmp_buffer, sizeof tmp_buffer, "%p", addr);
#endif
  return tmp_buffer;
}


const char* NameConverter::NameOfConstant(byte* addr) const {
  return NameOfAddress(addr);
}


const char* NameConverter::NameOfCPURegister(int reg) const {
  if (0 <= reg && reg < 8) return cpu_regs[reg];
  return "noreg";
}


const char* NameConverter::NameOfXMMRegister(int reg) const {
  if (0 <= reg && reg < 8) return xmm_regs[reg];
  return "noxmmreg";
}


const char* NameConverter::NameInCode(byte* addr) const {
  // IA32 does not embed debug strings at the moment.
  UNREACHABLE();
  return "";
}


//------------------------------------------------------------------------------

static NameConverter defaultConverter;

Disassembler::Disassembler() : converter_(defaultConverter) {}


Disassembler::Disassembler(const NameConverter& converter)
    : converter_(converter) {}


Disassembler::~Disassembler() {}


int Disassembler::InstructionDecode(char* buffer,
                                    const int buffer_size,
                                    byte* instruction) {
  DisassemblerIA32 d(converter_, false /*do not crash if unimplemented*/);
  return d.InstructionDecode(buffer, buffer_size, instruction);
}


/*static*/ void Disassembler::Disassemble(FILE* f, byte* begin, byte* end) {
  Disassembler d;
  for (byte* pc = begin; pc < end;) {
    char buffer[128];
    buffer[0] = '\0';
    byte* prev_pc = pc;
    pc += d.InstructionDecode(buffer, sizeof buffer, pc);
    fprintf(f, "%p", prev_pc);
    fprintf(f, "    ");

    for (byte* bp = prev_pc; bp < pc; bp++) {
      fprintf(f, "%02x",  *bp);
    }
    for (int i = 6 - (pc - prev_pc); i >= 0; i--) {
      fprintf(f, "  ");
    }
    fprintf(f, "  %s\n", buffer);
  }
}


}  // namespace disasm
