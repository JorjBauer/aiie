#include "teensy-paddles.h"

/* C061: Open Apple (Paddle 0 button pressed if &= 0x80)
 * C062: Closed Apple (Paddle 1 button pressed if &= 0x80)
 * C064: PADDLE0 (sets bit 0x80 when value reached, increments of 11 us)
 * C065: PADDLE1 (sets bit 0x80 when value reached, increments of 11 us)
 * C070: "start reading paddle data" - "may take up to 3 milliseconds"
 */

#include "globals.h"

TeensyPaddles::TeensyPaddles(uint8_t p0pin, uint8_t p1pin, bool p0rev, bool p1rev)
{
  this->p0pin = p0pin;
  this->p1pin = p1pin;
  this->p0rev = p0rev;
  this->p1rev = p1rev;
  
  pinMode(p0pin, INPUT);
  pinMode(p1pin, INPUT); 
}

TeensyPaddles::~TeensyPaddles()
{
}

uint8_t TeensyPaddles::paddle0()
{
  uint8_t raw = analogRead(p0pin);
  if (p0rev) {
    raw = 255 - raw;
  }
  return raw;
}

uint8_t TeensyPaddles::paddle1()
{
  uint8_t raw = analogRead(p1pin);
  if (p1rev) {
    raw = 255 - raw;
  }
  return raw;
}

void TeensyPaddles::startReading()
{
  g_vm->triggerPaddleInCycles(0, 12 * paddle0());
  g_vm->triggerPaddleInCycles(1, 12 * paddle1());
}

void TeensyPaddles::setRev(bool p0rev, bool p1rev)
{
  this->p0rev = p0rev;
  this->p1rev = p1rev;
}
