#include "parallelcard.h"
#include <string.h>

#include "parallel-rom.h"
#include "fx80.h"

ParallelCard::ParallelCard()
{
  fx80 = new Fx80();
}

ParallelCard::~ParallelCard()
{
}

void ParallelCard::Reset()
{
}

uint8_t ParallelCard::readSwitches(uint8_t s)
{
  return 0xFF;
}

void ParallelCard::writeSwitches(uint8_t s, uint8_t v)
{
  if (s == 0x00) {
    fx80->input(v);
  } else {
    //    printf("unknown switch 0x%X\n", s);
  }
}

void ParallelCard::loadROM(uint8_t *toWhere)
{
#ifdef TEENSYDUINO
  Serial.println("loading parallel slot rom");
  for (uint16_t i=0; i<=0xFF; i++) {
    toWhere[i] = pgm_read_byte(&romData[i]);
  }
#else
  printf("loading parallel slot rom\n");
  memcpy(toWhere, romData, 256);
#endif
}

void ParallelCard::update()
{
  fx80->update();
}
