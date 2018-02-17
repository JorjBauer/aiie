#ifndef __TEENSYCLOCK_H
#define __TEENSYCLOCK_H

#include <Arduino.h>

#include "applemmu.h"

#include "NoSlotClock.h"

class TeensyClock : public NoSlotClock {
 public:
  TeensyClock(AppleMMU *mmu);
  virtual ~TeensyClock();

 protected:
  virtual void populateClockRegister();
  virtual void updateClockFromRegister();
};

#endif
