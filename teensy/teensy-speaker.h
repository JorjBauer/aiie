#ifndef __TEENSY_SPEAKER_H
#define __TEENSY_SPEAKER_H

#include "physicalspeaker.h"
#include <MCP492X.h>

#define SAMPLERATE 8000

class TeensySpeaker : public PhysicalSpeaker {
 public:
  TeensySpeaker(uint8_t sda, uint8_t scl);
  virtual ~TeensySpeaker();

  virtual void begin();

  virtual void toggle(uint32_t c);
  virtual void maintainSpeaker();
  virtual void maintainSpeaker(uint32_t c, uint64_t microseconds);

  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);

 private:
  bool toggleState;

  uint32_t mixerValue;
  uint8_t numMixed;
};

#endif
