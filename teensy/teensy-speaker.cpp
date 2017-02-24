#include <Arduino.h>
#include "teensy-speaker.h"

extern int16_t g_volume;

TeensySpeaker::TeensySpeaker(uint8_t pinNum) : PhysicalSpeaker()
{
  speakerState = false;
  speakerPin = pinNum;
  pinMode(speakerPin, OUTPUT); // analog speaker output, used as digital volume control
  nextTransitionAt = 0;
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::toggleAtCycle(uint32_t c)
{
  // FIXME: could tell here if we dropped something - is nextTransitionAt already set? If so, we missed a toggle :(
  nextTransitionAt = c;
}

void TeensySpeaker::maintainSpeaker(uint32_t c)
{
  if (nextTransitionAt && c >= nextTransitionAt) {
    nextTransitionAt = 0;

    speakerState = !speakerState;
    // FIXME: glad it's DAC0 and all, but... how does that relate to the pin passed in the constructor?
    analogWriteDAC0(speakerState ? g_volume : 0); // max: 4095
  }
}

bool TeensySpeaker::currentState()
{
  return speakerState;
}
