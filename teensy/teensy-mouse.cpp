#include <Arduino.h>
#include "teensy-mouse.h"

#include "globals.h"

TeensyMouse::TeensyMouse() : PhysicalMouse()
{
  xpos = ypos = 0;
  button = false;
}

TeensyMouse::~TeensyMouse()
{
}

void TeensyMouse::gotMouseEvent(uint32_t buttonState, int32_t x, int32_t y)
{
  xpos += x; ypos += y;

  if (xpos < lowClamp[XCLAMP]) xpos=lowClamp[XCLAMP];
  if (xpos > highClamp[XCLAMP]) xpos=highClamp[XCLAMP];
  if (ypos < lowClamp[YCLAMP]) ypos = lowClamp[YCLAMP];
  if (ypos > highClamp[YCLAMP]) ypos = highClamp[YCLAMP];
}

void TeensyMouse::mouseButtonEvent(bool state)
{
  button = state;
}

void TeensyMouse::maintainMouse()
{
  // FIXME: only do this if the mouse card is enabled, so we're not incurring
  // analogRead delays constantly
  uint8_t paddle0 = g_paddles->paddle0();
  uint8_t paddle1 = g_paddles->paddle1();
  int16_t dx=0, dy=0;
  if (paddle0 <= 25) {
    dx = -1;
  } else if (paddle0 >= 245) {
    dx = 1;
  }
  if (paddle1 <= 25) {
    dy = -1;
  } else if (paddle1 >= 245) {
    dy = 1;
  }
  if (dx || dy) {
    gotMouseEvent(button, dx, dy);
  }
}

void TeensyMouse::setPosition(uint16_t x, uint16_t y)
{
  xpos = x;
  ypos = y;
  
  if (xpos < lowClamp[XCLAMP]) xpos=lowClamp[XCLAMP];
  if (xpos > highClamp[XCLAMP]) xpos=highClamp[XCLAMP];
  if (ypos < lowClamp[YCLAMP]) ypos = lowClamp[YCLAMP];
  if (ypos > highClamp[YCLAMP]) ypos = highClamp[YCLAMP];
}

void TeensyMouse::getPosition(uint16_t *x, uint16_t *y)
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

bool TeensyMouse::getButton()
{
  return button;
}
