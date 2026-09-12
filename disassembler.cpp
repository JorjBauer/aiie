#include "disassembler.h"
#include "cpu.h"

#include <stdio.h>

// all the lower-case opcodes here are 65c02.
static const char *opnames[256] = { 
  "BRK", "ORA", "???", "???", "tsb", "ORA", "ASL", "rmb0", 
  "PHP", "ORA", "ASL", "???", "tsb", "ORA", "ASL", "bbr0", 
  "BPL", "ORA", "ora", "???", "trb", "ORA", "ASL", "rmb1", 
  "CLC", "ORA", "inc", "???", "trb", "ORA", "ASL", "bbr1", 
  "JSR", "AND", "???", "???", "BIT", "AND", "ROL", "rmb2", 
  "PLP", "AND", "ROL", "???", "BIT", "AND", "ROL", "bbr2", 
  "BMI", "AND", "and", "???", "bit", "AND", "ROL", "rmb3", 
  "SEC", "AND", "dec", "???", "bit", "AND", "ROL", "bbr3", 
  "RTI", "EOR", "???", "???", "???", "EOR", "LSR", "rmb4", 
  "PHA", "EOR", "LSR", "???", "JMP", "EOR", "LSR", "bbr4", 
  "BVC", "EOR", "eor", "???", "???", "EOR", "LSR", "rmb5", 
  "CLI", "EOR", "phy", "???", "???", "EOR", "LSR", "bbr5", 
  "RTS", "ADC", "???", "???", "stz", "ADC", "ROR", "rmb6", 
  "PLA", "ADC", "ROR", "???", "JMP", "ADC", "ROR", "bbr6", 
  "BVS", "ADC", "adc", "???", "stz", "ADC", "ROR", "rmb7",
  "SEI", "ADC", "ply", "???", "jmp", "ADC", "ROR", "bbr7", // 0x78-0x7F
  "bra", "STA", "???", "???", "STY", "STA", "STX", "smb0", 
  "DEY", "bit", "TXA", "???", "STY", "STA", "STX", "bbs0", 
  "BCC", "STA", "sta", "???", "STY", "STA", "STX", "smb1", 
  "TYA", "STA", "TXS", "???", "stz", "STA", "stz", "bbs1", 
  "LDY", "LDA", "LDX", "???", "LDY", "LDA", "LDX", "smb2", 
  "TAY", "LDA", "TAX", "???", "LDY", "LDA", "LDX", "bbs2", 
  "BCS", "LDA", "lda", "???", "LDY", "LDA", "LDX", "smb3", 
  "CLV", "LDA", "TSX", "???", "LDY", "LDA", "LDX", "bbs3", 
  "CPY", "CMP", "???", "???", "CPY", "CMP", "DEC", "smb4", 
  "INY", "CMP", "DEX", "wai", "CPY", "CMP", "DEC", "bbs4", // 0xC8-0xCF
  "BNE", "CMP", "cmp", "???", "???", "CMP", "DEC", "smb5", 
  "CLD", "CMP", "phx", "dcp", "???", "CMP", "DEC", "bbs5", // 0xD8-0xDF
  "CPX", "SBC", "???", "???", "CPX", "SBC", "INC", "smb6", 
  "INX", "SBC", "NOP", "???", "CPX", "SBC", "INC", "bbs6", // 0xE8-0xEF
  "BEQ", "SBC", "sbc", "???", "???", "SBC", "INC", "smb7", 
  "SED", "SBC", "plx", "???", "???", "SBC", "INC", "bbs7", // 0xF0-0xFF
};

typedef struct _opmode {
  addrmode mode;
  const char *prefix;
  const char *suffix;
} opmode_t;

opmode_t opmodes[16] = { { A_IMP, "", "" },
		     { A_IMM, "#", "" },
		     { A_ABS, "", ""},
		     { A_ZER, "", ""},
		     { A_REL, "", ""},
		     { A_ABI, "(", ")"},
		     { A_ZEX, "", ",X"},
		     { A_ZEY, "", ",Y"},
		     { A_ZIND, "(", ")"},
		     { A_ABX, "", ",X"},
		     { A_ABXI, "(", ",X)"},
		     { A_ABY, "", ",Y"},
		     { A_INX, "(", ",X)"},
		     { A_INY, "(", "),Y"},
		     { A_ZPREL,"", "" },
		     { A_ACC, "A", ""},
};

Disassembler::Disassembler()
{
}

Disassembler::~Disassembler()
{
}

uint8_t Disassembler::instructionBytes(uint8_t i)
{
  switch (opcodes[i].mode) {
  case A_REL:
    return 2;
  case A_IMM:
    return 2;
  case A_ABS:
    return 3;
  case A_ZER:
    return 2;
  case A_IMP:
    return 1;
  case A_ACC:
    return 1;
  case A_ABI:
    return 3;
  case A_ZEX:
    return 2;
  case A_ZEY:
    return 2;
  case A_ABX:
    return 3;
  case A_ABXI:
    return 3;
  case A_ABY:
    return 3;
  case A_INX:
    return 2;
  case A_INY:
    return 2;
  case A_ZIND:
    return 2;
  case A_ZPREL:
    return 3;
  case A_ILLEGAL:
    return 1;
  }

  // Default to 1 byte for anything that falls through
  return 1;
}

opmode_t opmodeForInstruction(uint8_t ins)
{
  for (int i=0; i<16; i++) {
    if (opcodes[ins].mode == opmodes[i].mode) {
      return opmodes[i];
    }
  }

  /* NOTREACHED */
  opmode_t ret = { A_ILLEGAL, "", "" };
  return ret;
}

uint8_t Disassembler::instructionToMnemonic(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize)
{
  return format(addr, p, outp, outpSize, true);
}

uint8_t Disassembler::instructionToOperands(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize)
{
  return format(addr, p, outp, outpSize, false);
}

uint8_t Disassembler::format(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize, bool withAddress)
{
  const char *mn = opnames[*p];
  addrmode amode = opcodes[*p].mode;
  uint16_t target = 0;
  char arg[40] = "\0";
  char bytes[10] = "\0";

  switch (amode) {
  case A_REL:
    target = addr + *(const int8_t *)(p+1) + 2;
    break;
  case A_ABS:
  case A_ABY:
  case A_ABX:
  case A_ABI://indirect
  case A_ABXI:
      target = (*(p+2) << 8) | (*(p+1));
      break;
  case A_ZER:
  case A_ZIND:   // (zp): a one-byte operand, not an absolute address
  case A_INX:
  case A_INY:
  case A_ZEX:
  case A_ZEY:
  case A_IMM:
    target = *(p+1);   // an unsigned byte: LDA #$FF is #$FF, not #$FFFF
    break;
  case A_ZPREL:
    target = *(p+1);
    break;
  default:
    target = 0;
    break;
  }

  opmode_t om = opmodeForInstruction(*p);
  switch (instructionBytes(*p)) {
  case 1:
    // no arguments
    snprintf(bytes, sizeof(bytes), "      %.2X ", *p);
    break;
  case 2:
    snprintf(arg, sizeof(arg), "%s$%X%s", om.prefix, target, om.suffix);
    snprintf(bytes, sizeof(bytes), "   %.2X %.2X ", *p, *(p+1));
    break;
  case 3:
    if (amode == A_ZPREL) {
      // BBR/BBS: a zero-page address, then a relative branch target.
      snprintf(arg, sizeof(arg), "$%X,$%X", target,
	       (uint16_t)(addr + *(const int8_t *)(p+2) + 3));
    } else {
      snprintf(arg, sizeof(arg), "%s$%X%s", om.prefix, target, om.suffix);
    }
    snprintf(bytes, sizeof(bytes), "%.2X %.2X %.2X ", *p, *(p+1), *(p+2));
    break;
  }

  if (withAddress) {
    snprintf(outp, outpSize, "$%.4X %s  %s    %s", addr, bytes, mn, arg);
  } else {
    snprintf(outp, outpSize, "%s    %s", mn, arg);
  }

  return instructionBytes(*p);
}
