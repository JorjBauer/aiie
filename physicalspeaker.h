#ifndef __PHYSICALSPEAKER_H
#define __PHYSICALSPEAKER_H

#include <stdint.h>

class PhysicalSpeaker {
 public:
  virtual ~PhysicalSpeaker() {}

  virtual void begin() = 0;

  virtual void toggle(int64_t c) = 0;
  virtual void maintainSpeaker(int64_t c, uint64_t microseconds) = 0;
  virtual void beginMixing() = 0;
  virtual void mixOutput(uint8_t v) = 0;

};

#endif
