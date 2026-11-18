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

// IRQ sources (see Cpu::assertIrq)
#define IRQSRC_OTHER        0x01  // the mouse, and anything that doesn't say
#define IRQSRC_MOCKINGBOARD 0x02
#define IRQSRC_SOUNDSPEECH  0x04

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

  // The //e's IRQ line is wired-OR: any card can hold it low. Each source
  // asserts and releases its own bit, so one card releasing the line doesn't
  // drop another card's interrupt.
  //
  // 'when' is the bus cycle the line fell, for a card that knows it (the
  // 6522s). The 65C02 samples its interrupt lines at the end of an
  // instruction's next-to-last cycle, so we only take the interrupt at a
  // boundary at least 2 cycles after that. Without 'when', it's taken at the
  // next boundary.
  void assertIrq(uint8_t source = IRQSRC_OTHER, int64_t when = -1);
  void deassertIrq(uint8_t source = IRQSRC_OTHER);

  // NMI is edge-triggered: a card calls this when its NMI line falls, and we
  // take the interrupt at the next instruction boundary (with the same 'when'
  // rule as assertIrq).
  void triggerNmi(int64_t when = -1);

  // The cycle the current instruction's last bus access lands on. Loads and
  // stores touch their operand on their last cycle, and a card that counts
  // cycles (the 6522) needs that, not the cycle the instruction started on.
  // Outside of an instruction it's just 'cycles'.
  int64_t busCycle() { return cycles + opCycles - 1; }

  // A card that needs to act on a particular cycle (a 6522 timer running
  // out) asks to be called back at the first instruction boundary on or after
  // it. There's one hook; the VM points it at a function that updates every
  // card that might have asked, and they each ask again for their next one.
  void scheduleEvent(int64_t when) { if (when < nextEventCycle) nextEventCycle = when; }
  void (*eventHook)();

  uint8_t Run(uint8_t numSteps);
  uint8_t step();

  uint8_t X();
  uint8_t Y();
  uint8_t A();
  uint16_t PC();
  uint8_t SP();
  uint8_t P();

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

  bool irqPending;       // true while any bit of irqSources is set
  uint8_t irqSources;
  int64_t irqReadyCycle;   // earliest boundary to take the IRQ at
  // The IRQ poll happens before an instruction's last cycle, so an
  // instruction that changes I (CLI, SEI, PLP) only changes it for the
  // boundary after next. These hold the old answer for one boundary.
  bool irqHoldOff;         // I was just cleared: still masked this once
  bool irqLetThrough;      // I was just set: still open this once
  bool nmiPending;
  int64_t nmiReadyCycle;
  uint8_t opCycles;        // cycles of the instruction being run (1 between)
  int64_t nextEventCycle;
  
  MMU *mmu;

  bool realtimeProcessing;
};



#endif
