#ifndef __NIXCLOCK_H
#define __NIXCLOCK_H

#include <stdint.h>
#include <stdio.h>

#include "slot.h"
#include "applemmu.h"

// Simple clock for *nix

class NixClock : public Slot {
 public:
  NixClock(AppleMMU *mmu);
  virtual ~NixClock();

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
