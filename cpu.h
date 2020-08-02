#ifndef __CPU_H
#define __CPU_H

#include <stdlib.h>
#include <stdint.h>

class MMU;

enum addrmode {
  A_ILLEGAL,
  A_IMM,
  A_ABS,
  A_ZER,
  A_IMP,
  A_ACC,
  A_REL,
  A_ABI,
  A_ZEX,
  A_ZEY,
  A_ZIND,
  A_ABX,
  A_ABXI,
  A_ABY,
  A_INX,
  A_INY,
  A_ZPREL
};

enum optype { 
  O_ILLEGAL,
  O_ADC,
  O_AND,
  O_ASL,
  O_ASL_ACC,
  O_BCC,
  O_BCS,
  O_BEQ,
  O_BIT,
  O_BMI,
  O_BNE,
  O_BPL,
  O_BRA,
  O_BRK,
  O_BVC,
  O_BVS,
  O_CLC,
  O_CLD,
  O_CLI,
  O_CLV,
  O_CMP,
  O_CPX,
  O_CPY,
  O_DEC,
  O_DEC_ACC,
  O_DEX,
  O_DEY,
  O_EOR,
  O_INC,
  O_INC_ACC,
  O_INX,
  O_INY,
  O_JMP,
  O_JSR,
  O_LDA,
  O_LDX,
  O_LDY,
  O_LSR,
  O_LSR_ACC,
  O_NOP,
  O_ORA,
  O_PHA,
  O_PHP,
  O_PHX,
  O_PHY,
  O_PLA,
  O_PLP,
  O_PLX,
  O_PLY,
  O_ROL,
  O_ROL_ACC,
  O_ROR,
  O_ROR_ACC,
  O_RTI,
  O_RTS,
  O_SBC,
  O_SEC,
  O_SED,
  O_SEI,
  O_STA,
  O_STX,
  O_STY,
  O_STZ,
  O_TAX,
  O_TAY,
  O_TRB,
  O_TSB,
  O_TSX,
  O_TXA,
  O_TXS,
  O_TYA,

  O_BBR,
  O_BBS,
  O_RMB,
  O_SMB,
  
  O_WAI,

  // and the "illegal" opcodes (those that don't officially exist for
  // the 65c02, but have repeatable results)
  O_DCP
};

typedef struct {
  optype op;
  addrmode mode;
  uint8_t cycles;
} optype_t;

extern optype_t opcodes[256];

// Flags (P) register bit definitions.
// Negative
#define F_N (1<<7)
// Overflow
#define F_V (1<<6)
#define F_UNK (1<<5) // What the heck is this?
// Break
#define F_B (1<<4)
// Decimal
#define F_D (1<<3)
// Interrupt Disable
#define F_I (1<<2)
// Zero
#define F_Z (1<<1)
// Carry
#define F_C (1<<0)

class Cpu {
 public:
  Cpu();
  ~Cpu();

  bool Serialize(int8_t fh);
  bool Deserialize(int8_t fh);

  void Reset();

  void nmi();
  void rst();
  void brk();
  void irq();

  uint8_t Run(uint8_t numSteps);
  uint8_t step();

  uint8_t X();
  uint8_t Y();
  uint8_t A();
  uint16_t PC();
  uint8_t SP();
  uint8_t P();

  void stageIRQ();

 protected:
  // Stack manipulation
  void pushS8(uint8_t b);
  void pushS16(uint16_t w);
  uint8_t popS8();
  uint16_t popS16();

 public:
  void SetMMU(MMU *mmu) { this->mmu = mmu; }

  void realtime();

 public:
  uint16_t pc;
  uint8_t sp;
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t flags;

  int64_t cycles;

  bool irqPending;
  
  MMU *mmu;

  bool realtimeProcessing;
};



#endif
