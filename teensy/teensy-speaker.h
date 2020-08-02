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

  virtual void toggle(int64_t c);
  virtual void maintainSpeaker();
  virtual void maintainSpeaker(int64_t c, uint64_t microseconds);

  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);

 private:

   uint8_t mixerValue;
   bool toggleState;
};

#endif
