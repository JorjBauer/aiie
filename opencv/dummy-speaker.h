#ifndef __DUMMYSPEAKER_H
#define __DUMMYSPEAKER_H

#include <stdio.h>
#include <stdint.h>
#include "physicalspeaker.h"

class DummySpeaker : public PhysicalSpeaker {
 public:
  DummySpeaker();
  virtual ~DummySpeaker();

  virtual void toggle();
  virtual void maintainSpeaker(uint32_t c);
  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);
};

#endif
