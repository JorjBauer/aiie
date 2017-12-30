#ifndef __SDLCLOCK_H
#define __SDLCLOCK_H

#include <stdint.h>
#include <stdio.h>

#include "Slot.h"
#include "applemmu.h"

class SDLClock : public Slot {
 public:
  SDLClock(AppleMMU *mmu);
  virtual ~SDLClock();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset();

  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);

  virtual void loadROM(uint8_t *toWhere);

 private:
  AppleMMU *mmu;
};

#endif
