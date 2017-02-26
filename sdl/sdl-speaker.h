#ifndef __SDLSPEAKER_H
#define __SDLSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

class SDLSpeaker : public PhysicalSpeaker {
 public:
  SDLSpeaker();
  virtual ~SDLSpeaker();

  virtual void toggle();
  virtual void maintainSpeaker(uint32_t c);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);
 private:
  uint32_t mixerValue;
  uint8_t numMixed;
  bool toggleState;
  bool needsToggle;

  FILE *f;
};

#endif
