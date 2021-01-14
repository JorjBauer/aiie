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

uint8_t Disassembler::instructionToMnemonic(uint16_t addr, uint8_t *p, char *outp, uint16_t outpSize)
{
  const char *mn = opnames[*p];
  addrmode amode = opcodes[*p].mode;
  uint16_t target = 0;
  char arg[40] = "\0";
  char bytes[10] = "\0";

  switch (amode) {
  case A_REL:
    target = addr + *(int8_t *)(p+1) + 2; // FIXME: is this correct?
    break;
  case A_ABS:
  case A_ABY:
  case A_ABX:
  case A_ABI://indirect
  case A_ABXI:
  case A_ZIND:
      target = (*(p+2) << 8) | (*(p+1)); // FIXME: is this correct?
      break;
  case A_ZER:
  case A_INX:
  case A_INY:
  case A_ZEX:
  case A_ZEY:
  case A_ZPREL:
  case A_IMM:
    target = *(int8_t *)(p+1);
    break;
  default:
    target = 0;
    break;
  }

  opmode_t om = opmodeForInstruction(*p);
  switch (instructionBytes(*p)) {
  case 1:
    // no arguments
    sprintf(bytes, "      %.2X ", *(uint8_t *)p);
    break;
  case 2:
    sprintf(arg, "%s$%X%s", om.prefix, target, om.suffix);
    sprintf(bytes, "   %.2X %.2X ", *(uint8_t *)p, *(uint8_t *)(p+1));
    break;
  case 3:
    sprintf(arg, "%s$%X%s", om.prefix, target, om.suffix);
    sprintf(bytes, "%.2X %.2X %.2X ", *(uint8_t *)p, *(uint8_t *)(p+1), *(uint8_t *)(p+2));
    break;
  }
   
  sprintf(outp, "$%.4X %s  %s    %s", addr, bytes, mn, arg);

  return instructionBytes(*p);
}
