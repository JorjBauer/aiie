#ifndef __SDLSPEAKER_H
#define __SDLSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

class SDLSpeaker : public PhysicalSpeaker {
 public:
  SDLSpeaker();
  virtual ~SDLSpeaker();

  virtual void begin();

  virtual void toggle(int64_t c);
  virtual void maintainSpeaker(int64_t c, uint64_t microseconds);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);

 private:
  uint8_t mixerValue;
  bool toggleState;
};

#endif
