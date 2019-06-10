// Copyright 2008 Google Inc. All Rights Reserved.
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

#ifndef V8_CONSTANTS_ARM_H_
#define V8_CONSTANTS_ARM_H_

namespace assembler { namespace arm {

// Defines constants and accessor classes to assemble, disassemble and
// simulate ARM instructions.
//
// Constants for specific fields are defined in their respective named enums.
// General constants are in an anonymous enum in class Instr.

typedef unsigned char byte;

enum Condition {
  no_condition = -1,
  EQ =  0,
  NE =  1,
  CS =  2,
  CC =  3,
  MI =  4,
  PL =  5,
  VS =  6,
  VC =  7,
  HI =  8,
  LS =  9,
  GE = 10,
  LT = 11,
  GT = 12,
  LE = 13,
  AL = 14,
  special_condition = 15
};


enum Opcode {
  no_operand = -1,
  AND =  0,
  EOR =  1,
  SUB =  2,
  RSB =  3,
  ADD =  4,
  ADC =  5,
  SBC =  6,
  RSC =  7,
  TST =  8,
  TEQ =  9,
  CMP = 10,
  CMN = 11,
  ORR = 12,
  MOV = 13,
  BIC = 14,
  MVN = 15
};


enum Shift {
  no_shift = -1,
  LSL = 0,
  LSR = 1,
  ASR = 2,
  ROR = 3
};


enum SoftwareInterruptCodes {
  // transition to C code
  call_rt_r5 = 0x10,
  call_rt_r2 = 0x11,
  // break point
  break_point = 0x20
};


typedef int32_t instr_t;


// The class Instr enables access to individual fields defined in the ARM
// architecture.
class Instr {
 public:
  enum {
    kInstrSize = 4,
    kPCReadOffset = 8
  };

  // Get the raw instruction bits
  inline instr_t InstructionBits() const {
    return *reinterpret_cast<const instr_t*>(this);
  }

  inline void SetInstructionBits(instr_t value) {
    *reinterpret_cast<instr_t*>(this) = value;
  }

  inline int Bit(int nr) const {
    return (InstructionBits() >> nr) & 1;
  }

  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2 << (hi - lo)) - 1);
  }


  // Accessors for the different named fields used in the ARM encoding.
  // Generally applicable fields
  inline Condition ConditionField() const {
    return static_cast<Condition>(Bits(31, 28));
  }
  inline int TypeField() const { return Bits(27, 25); }

  inline int RnField() const { return Bits(19, 16); }
  inline int RdField() const { return Bits(15, 12); }

  // Fields used in Data processing instructions
  inline Opcode OpcodeField() const {
    return static_cast<Opcode>(Bits(24, 21));
  }
  inline int SField() const { return Bit(20); }
    // with register
  inline int RmField() const { return Bits(3, 0); }
  inline Shift ShiftField() const { return static_cast<Shift>(Bits(6, 5)); }
  inline int RegShiftField() const { return Bit(4); }
  inline int RsField() const { return Bits(11, 8); }
  inline int ShiftAmountField() const { return Bits(11, 7); }
    // with immediate
  inline int RotateField() const { return Bits(11, 8); }
  inline int Immed8Field() const { return Bits(7, 0); }

  // Fields used in Load/Store instructions
  inline int PUField() const { return Bits(24, 23); }
  inline int  BField() const { return Bit(22); }
  inline int  WField() const { return Bit(21); }
  inline int  LField() const { return Bit(20); }
    // with register uses same fields as Data processing instructions above
    // with immediate
  inline int Offset12Field() const { return Bits(11, 0); }
    // multiple
  inline int RlistField() const { return Bits(15, 0); }
    // extra loads and stores
  inline int SignField() const { return Bit(6); }
  inline int HField() const { return Bit(5); }
  inline int ImmedHField() const { return Bits(11, 8); }
  inline int ImmedLField() const { return Bits(3, 0); }

  // Fields used in Branch instructions
  inline int LinkField() const { return Bit(24); }
  inline int SImmed24Field() const { return ((InstructionBits() << 8) >> 8); }

  // Fields used in Software interrupt instructions
  inline SoftwareInterruptCodes SwiField() const {
    return static_cast<SoftwareInterruptCodes>(Bits(23, 0));
  }

  // Test for special encodings of type 0 instructions (extra loads and stores,
  // as well as multiplications).
  inline bool IsSpecialType0() const { return (Bit(7) == 1) && (Bit(4) == 1); }

  // Special accessors that test for existence of a value.
  inline bool HasS()    const { return SField() == 1; }
  inline bool HasB()    const { return BField() == 1; }
  inline bool HasW()    const { return WField() == 1; }
  inline bool HasL()    const { return LField() == 1; }
  inline bool HasSign() const { return SignField() == 1; }
  inline bool HasH()    const { return HField() == 1; }
  inline bool HasLink() const { return LinkField() == 1; }

  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instr.
  // Use the At(pc) function to create references to Instr.
  static Instr* At(byte* pc) { return reinterpret_cast<Instr*>(pc); }

 private:
  // We need to prevent the creation of instances of class Instr.
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instr);
};


} }  // namespace assembler::arm

#endif  // V8_CONSTANTS_ARM_H_
