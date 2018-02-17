#ifndef __NIXCLOCK_H
#define __NIXCLOCK_H

#include <stdint.h>
#include <stdio.h>

#include "noslotclock.h"

class NixClock : public NoSlotClock {
 public:
  NixClock(AppleMMU *mmu);
  virtual ~NixClock();

 protected:
  virtual void populateClockRegister();
  virtual void updateClockFromRegister();

};

#endif
