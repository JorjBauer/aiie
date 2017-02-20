#ifndef __TEENSY_SPEAKER_H
#define __TEENSY_SPEAKER_H

#include "physicalspeaker.h"

class TeensySpeaker : public PhysicalSpeaker {
 public:
  TeensySpeaker(uint8_t pinNum);
  virtual ~TeensySpeaker();

  virtual void toggleAtCycle(uint32_t c);
  virtual void maintainSpeaker(uint32_t c);

 private:
  bool speakerState;
  uint8_t speakerPin;

  uint32_t nextTransitionAt;

};

#endif
