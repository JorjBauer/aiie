#include <stdio.h>
#include "sdl-paddles.h"

#include "sdl-display.h"

#include "globals.h"

SDLPaddles::SDLPaddles()
{
  p0 = p1 = 127;
}

SDLPaddles::~SDLPaddles()
{
}

void SDLPaddles::startReading()
{
  g_vm->triggerPaddleInCycles(0, 12 * p0);
  g_vm->triggerPaddleInCycles(1, 12 * p1);
}

uint8_t SDLPaddles::paddle0()
{
  return p0;
}

uint8_t SDLPaddles::paddle1()
{
  return p1;
}

void SDLPaddles::gotMouseMovement(uint16_t x, uint16_t y)
{
  // ***
  p0 = ((float)x / (float)800) * (float) 255.0;
  p1 = ((float)y / (float)480) * (float) 255.0;
}
