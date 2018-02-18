#ifndef __DISASSEMBLER_H
#define __DISASSEMBLER_H

#include <stdint.h>

class Disassembler {
 public:
  Disassembler();
  ~Disassembler();

  uint8_t instructionBytes(uint8_t i);
  uint8_t instructionToMnemonic(uint16_t addr, uint8_t *p, char *outp, uint16_t outpSize);
};

#endif
