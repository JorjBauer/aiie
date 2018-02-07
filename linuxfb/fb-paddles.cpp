#include <stdio.h>
#include "fb-paddles.h"

#include "globals.h"

FBPaddles::FBPaddles()
{
  p0 = p1 = 127;
}

FBPaddles::~FBPaddles()
{
}

void FBPaddles::startReading()
{
  g_vm->triggerPaddleInCycles(0, 12 * p0);
  g_vm->triggerPaddleInCycles(1, 12 * p1);
}

uint8_t FBPaddles::paddle0()
{
  return p0;
}

uint8_t FBPaddles::paddle1()
{
  return p1;
}
