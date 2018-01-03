#ifndef __PHYSICALSPEAKER_H
#define __PHYSICALSPEAKER_H

#include <stdint.h>

class PhysicalSpeaker {
 public:
  virtual ~PhysicalSpeaker() {}

  virtual void toggle(uint32_t c) = 0;
  virtual void maintainSpeaker(uint32_t c, uint64_t microseconds) = 0;
  virtual void beginMixing() = 0;
  virtual void mixOutput(uint8_t v) = 0;

};

#endif
