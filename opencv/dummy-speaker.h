#ifndef __DUMMYSPEAKER_H
#define __DUMMYSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

class DummySpeaker : public PhysicalSpeaker {
 public:
  DummySpeaker();
  virtual ~DummySpeaker();

  virtual void toggleAtCycle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c);
  virtual bool currentState();
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);
 private:
  bool speakerState;

  uint32_t mixerValue;
  uint8_t numMixed;
  uint32_t nextTransitionAt;

  FILE *f;
};

#endif
