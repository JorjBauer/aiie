#include <Arduino.h>
#include "teensy-speaker.h"

#include "globals.h"

TeensySpeaker::TeensySpeaker(uint8_t pinNum) : PhysicalSpeaker()
{
  toggleState = false;
  speakerPin = pinNum;
  pinMode(speakerPin, OUTPUT); // analog speaker output, used as digital volume control
  mixerValue = numMixed = 0;

  toggleCount = toggleReadPtr = toggleWritePtr = 0;
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::toggle(uint32_t c)
{
  toggleTimes[toggleWritePtr] = c;
  if (toggleCount < SPEAKERQUEUESIZE-1) {
    toggleWritePtr++;
    if (toggleWritePtr >= SPEAKERQUEUESIZE)
      toggleWritePtr = 0;
    toggleCount++;
  } else {
    // speaker overflow
    Serial.println("spkr overflow");
  }
}

void TeensySpeaker::maintainSpeaker(uint32_t c)
{
  bool didChange = false;

  while (toggleCount && c >= toggleTimes[toggleReadPtr]) {
    toggleState = !toggleState;
    toggleCount--;
    toggleReadPtr++;
    if (toggleReadPtr >= SPEAKERQUEUESIZE)
      toggleReadPtr = 0;
    didChange = true;
  }

  if (didChange) {
    mixerValue = (toggleState ? 0x1FF : 0x00);
    mixerValue >>= (16-g_volume);
    
    // FIXME: glad it's DAC0 and all, but... how does that relate to the pin passed in the constructor?
    analogWriteDAC0(mixerValue);
  }
}

void TeensySpeaker::beginMixing()
{
  // unused
}

void TeensySpeaker::mixOutput(uint8_t v)
{
  // unused
}

