#ifndef __TEENSY_SPEAKER_H
#define __TEENSY_SPEAKER_H

#include "physicalspeaker.h"

// FIXME: 64 enough?
#define SPEAKERQUEUESIZE 64

class TeensySpeaker : public PhysicalSpeaker {
 public:
  TeensySpeaker(uint8_t pinNum);
  virtual ~TeensySpeaker();

  virtual void toggle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c);

  virtual void beginMixing();
  virtual void mixOutput(uint8_t v);

 private:
  uint8_t speakerPin;

  bool toggleState;

  uint32_t mixerValue;
  uint8_t numMixed;

  uint32_t toggleTimes[SPEAKERQUEUESIZE]; 
  uint8_t toggleCount;    // # of entries still in queue
  uint8_t toggleReadPtr;  // ring buffer pointer in queue
  uint8_t toggleWritePtr; // ring buffer pointer in queue
};

#endif
