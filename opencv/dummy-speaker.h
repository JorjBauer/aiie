#ifndef __DUMMYSPEAKER_H
#define __DUMMYSPEAKER_H

#include <stdint.h>
#include "physicalspeaker.h"

class DummySpeaker : public PhysicalSpeaker {
 public:
  virtual ~DummySpeaker();

  virtual void toggleAtCycle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c);
};

#endif
