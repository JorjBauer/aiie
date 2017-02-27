#include <Arduino.h>
#include "teensy-speaker.h"

#include "globals.h"

TeensySpeaker::TeensySpeaker(uint8_t pinNum) : PhysicalSpeaker()
{
  toggleState = false;
  needsToggle = false;
  speakerPin = pinNum;
  pinMode(speakerPin, OUTPUT); // analog speaker output, used as digital volume control
  mixerValue = numMixed = 0;
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::toggle()
{
  needsToggle = true;
}

void TeensySpeaker::maintainSpeaker(uint32_t c)
{
  if (needsToggle) {
    toggleState = !toggleState;
    needsToggle = false;
  }

  mixerValue += (toggleState ? 0xFFF : 0x00);
  // FIXME: Temporarily disabling mixer
  /*  numMixed += 2;
    
  if (numMixed > 1) {
    mixerValue /= numMixed;
    }*/

  // FIXME: glad it's DAC0 and all, but... how does that relate to the pin passed in the constructor?

  mixerValue >>= (16-g_volume);

  analogWriteDAC0(mixerValue);
}

void TeensySpeaker::beginMixing()
{
  mixerValue = 0;
  numMixed = 0;
}

void TeensySpeaker::mixOutput(uint8_t v)
{
  mixerValue += v;
  numMixed++;
}

