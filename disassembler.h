#ifndef __DISASSEMBLER_H
#define __DISASSEMBLER_H

#include <stdint.h>

class Disassembler {
 public:
  Disassembler();
  ~Disassembler();

  uint8_t instructionBytes(uint8_t i);
  // "$ADDR  b1 b2 b3  MNEMONIC    operand": a full listing line.
  uint8_t instructionToMnemonic(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize);
  // "MNEMONIC    operand" only, for a front end that lays out its own columns.
  uint8_t instructionToOperands(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize);

 private:
  uint8_t format(uint16_t addr, const uint8_t *p, char *outp, uint16_t outpSize, bool withAddress);
};

#endif
