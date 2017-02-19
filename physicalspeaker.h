#ifndef __PHYSICALSPEAKER_H
#define __PHYSICALSPEAKER_H

#include <stdint.h>

class PhysicalSpeaker {
 public:
  virtual ~PhysicalSpeaker() {}

  virtual void toggleAtCycle(uint32_t c) = 0;
  virtual void maintainSpeaker(uint32_t c) = 0;

};

#endif
