#ifndef __NSCLOCK_H
#define __NSCLOCK_H

#include <stdint.h>
#include <stdio.h>

class AppleMMU;

class NoSlotClock {
 public:
  NoSlotClock(AppleMMU *mmu);
  virtual ~NoSlotClock();

  bool read(uint8_t s, uint8_t *data);
  void write(uint8_t s);

 protected:
  bool doRead(uint8_t *d);
  void doWrite(uint8_t address);
  void writeNibble(uint8_t n);

  virtual void populateClockRegister() = 0;
  virtual void updateClockFromRegister() = 0;

 protected:
  AppleMMU *mmu;

  uint64_t clockReg;
  uint64_t compareReg;
  uint8_t clockRegPtr;
  uint8_t compareRegPtr;

  bool regEnabled;
  bool writeEnabled;
};

#endif
