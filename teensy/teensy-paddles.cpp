#include "teensy-paddles.h"

/* C061: Open Apple (Paddle 0 button pressed if &= 0x80)
 * C062: Closed Apple (Paddle 1 button pressed if &= 0x80)
 * C064: PADDLE0 (sets bit 0x80 when value reached, increments of 11 us)
 * C065: PADDLE1 (sets bit 0x80 when value reached, increments of 11 us)
 * C070: "start reading paddle data" - "may take up to 3 milliseconds"
 */

#define PADDLE0 A24
#define PADDLE1 A23

#include "globals.h"

TeensyPaddles::TeensyPaddles()
{
  pinMode(PADDLE0, INPUT);
  pinMode(PADDLE1, INPUT);
}

TeensyPaddles::~TeensyPaddles()
{
}

uint8_t TeensyPaddles::paddle0()
{
  uint8_t raw = 255 - analogRead(PADDLE0);
  return raw;

  // 40 .. 200 on the old joystick
  if (raw >200) raw = 200;
  if (raw < 40) raw = 40;

  return map(raw, 40, 200, 0, 255);
}

uint8_t TeensyPaddles::paddle1()
{
  uint8_t raw = analogRead(PADDLE1);
  return raw;

  // 60..200 on the old joystick
    if (raw >200) raw = 200;
    if (raw < 60) raw = 60;

  return map(raw, 60, 200, 0, 255);
}

void TeensyPaddles::startReading()
{
  g_vm->triggerPaddleInCycles(0, 12 * paddle0());
  g_vm->triggerPaddleInCycles(1, 12 * paddle1());
}
