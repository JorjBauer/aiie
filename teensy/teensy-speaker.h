#ifndef __TEENSY_SPEAKER_H
#define __TEENSY_SPEAKER_H

#include <AudioStream.h>
#include "physicalspeaker.h"

class TeensyAudio : public AudioStream {
 public:
  TeensyAudio(void) : AudioStream(0, NULL) {}

  virtual void update(void);
};

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

  uint32_t mixerValue;
  uint8_t numMixed;
};

#endif
