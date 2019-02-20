#ifndef __SDLSPEAKER_H
#define __SDLSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

#define SPEAKERQUEUESIZE 1024

class SDLSpeaker : public PhysicalSpeaker {
 public:
  SDLSpeaker();
  virtual ~SDLSpeaker();

  virtual void begin();

  virtual void toggle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c, uint64_t microseconds);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);

  virtual uint32_t bufferedContentSize();

 private:
  uint8_t mixerValue;
  bool toggleState;

  uint64_t lastCycleCount;
  uint64_t lastSampleCount;
};

#endif
