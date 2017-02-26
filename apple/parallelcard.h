#ifndef __PARALLELCARD_H
#define __PARALLELCARD_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "applemmu.h"
#include "slot.h"

class Fx80;

class ParallelCard : public Slot {
 public:
  ParallelCard();
  virtual ~ParallelCard();

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

 private:
  Fx80 *fx80;
};

#endif
