#include <stdio.h>
#include "sdl-paddles.h"

// FIXME: abstract this somewhere

#define WINDOWHEIGHT (240*2)
#define WINDOWWIDTH (320*2)

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
  p0 = ((float)x / (float)WINDOWWIDTH) * (float) 255.0;
  p1 = ((float)y / (float)WINDOWHEIGHT) * (float) 255.0;
}
