#include "sdl-mouse.h"

#include "globals.h"

SDLMouse::SDLMouse() : PhysicalMouse()
{
  xpos = ypos = 0;
  button = false;
}

SDLMouse::~SDLMouse()
{
}

void SDLMouse::gotMouseEvent(uint32_t buttonState, int32_t x, int32_t y)
{
  xpos += x; ypos += y;

  if (xpos < lowClamp[XCLAMP]) xpos=lowClamp[XCLAMP];
  if (xpos > highClamp[XCLAMP]) xpos=highClamp[XCLAMP];
  if (ypos < lowClamp[YCLAMP]) ypos = lowClamp[YCLAMP];
  if (ypos > highClamp[YCLAMP]) ypos = highClamp[YCLAMP];
}

void SDLMouse::mouseButtonEvent(bool state)
{
  button = state;
}

void SDLMouse::maintainMouse()
{
}

void SDLMouse::setPosition(uint16_t x, uint16_t y)
{
  xpos = x;
  ypos = y;
  
  if (xpos < lowClamp[XCLAMP]) xpos=lowClamp[XCLAMP];
  if (xpos > highClamp[XCLAMP]) xpos=highClamp[XCLAMP];
  if (ypos < lowClamp[YCLAMP]) ypos = lowClamp[YCLAMP];
  if (ypos > highClamp[YCLAMP]) ypos = highClamp[YCLAMP];
}

void SDLMouse::getPosition(uint16_t *x, uint16_t *y)
{
  if (xpos < lowClamp[XCLAMP]) xpos=lowClamp[XCLAMP];
  if (xpos > highClamp[XCLAMP]) xpos=highClamp[XCLAMP];
  if (ypos < lowClamp[YCLAMP]) ypos = lowClamp[YCLAMP];
  if (ypos > highClamp[YCLAMP]) ypos = highClamp[YCLAMP];

  uint16_t outx = xpos;
  uint16_t outy = ypos;

  *x = outx;
  *y = outy;
}

bool SDLMouse::getButton()
{
  return button;
}
