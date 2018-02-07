#ifndef __SDLSPEAKER_H
#define __SDLSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

#define SPEAKERQUEUESIZE 64

class LinuxSpeaker : public PhysicalSpeaker {
 public:
  LinuxSpeaker();
  virtual ~LinuxSpeaker();

  virtual void toggle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c, uint64_t microseconds);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);
};

#endif
