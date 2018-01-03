#ifndef __SDLSPEAKER_H
#define __SDLSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

#define SPEAKERQUEUESIZE 64

class SDLSpeaker : public PhysicalSpeaker {
 public:
  SDLSpeaker();
  virtual ~SDLSpeaker();

  virtual void toggle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c, uint64_t microseconds);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);
 private:
  int16_t mixerValue;
  bool toggleState;

  uint32_t toggleTimes[SPEAKERQUEUESIZE];
  uint8_t toggleCount;    // # of entries still in queue
  uint8_t toggleReadPtr;  // ring buffer pointer in queue
  uint8_t toggleWritePtr; // ring buffer pointer in queue

  uint64_t lastCycleCount;
  uint64_t lastSampleCount;

  FILE *f;
};

#endif
