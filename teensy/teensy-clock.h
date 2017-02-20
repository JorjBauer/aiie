#ifndef __TEENSYCLOCK_H
#define __TEENSYCLOCK_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "slot.h"
#include "applemmu.h"

class TeensyClock : public Slot {
 public:
  TeensyClock(AppleMMU *mmu);
  virtual ~TeensyClock();

  virtual void Reset();

  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);

  virtual void loadROM(uint8_t *toWhere);

 private:
  AppleMMU *mmu;
};

#endif
