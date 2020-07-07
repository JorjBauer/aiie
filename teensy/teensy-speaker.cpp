#include <Arduino.h>
#include "teensy-speaker.h"

#include "globals.h"

TeensySpeaker::TeensySpeaker(uint8_t pinNum) : PhysicalSpeaker()
{
  toggleState = false;
  speakerPin = pinNum;
  pinMode(speakerPin, OUTPUT); // analog speaker output, used as digital volume control
  mixerValue = numMixed = 0;
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::toggle(uint32_t c)
{
  toggleState = !toggleState;

  mixerValue = (toggleState ? 0x1FF : 0x00);
  mixerValue >>= (16-g_volume);
  
  analogWrite(speakerPin, mixerValue);
}

void TeensySpeaker::maintainSpeaker(uint32_t c, uint64_t runtimeInMicros)
{
  // Nothing to do here. We can't run the speaker async, b/c not
  // enough CPU time. So we run the CPU close to sync and hope that
  // the direct pulsing of the speaker is reasonably close to on-time.
}

void TeensySpeaker::beginMixing()
{
  // unused
}

void TeensySpeaker::mixOutput(uint8_t v)
{
  // unused
}

