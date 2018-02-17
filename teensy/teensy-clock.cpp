#include <string.h> // memset
#include <TimeLib.h>

#include "noslotclock.h"

#include "teensy-clock.h"

TeensyClock::TeensyClock(AppleMMU *mmu) : NoSlotClock(mmu)
{
}

TeensyClock::~TeensyClock()
{
}

void TeensyClock::populateClockRegister()
{
  tmElements_t tm;
  breakTime(now(), tm);

  tm.Year %= 100; // 00-99
  tm.Month++; // 1-12
  tm.Wday++; // 1-7, where 1 = Sunday

  writeNibble(tm.Year / 10); // 00-99
  writeNibble(tm.Year % 10);
  writeNibble(tm.Month / 10); // 1-12
  writeNibble(tm.Month % 10);
  writeNibble(tm.Day / 10); // 1-31
  writeNibble(tm.Day % 10); 
  writeNibble(0);
  writeNibble(tm.Wday); // 1-7, where 1 = Sunday
  writeNibble(tm.Hour / 10);
  writeNibble(tm.Hour % 10);
  writeNibble(tm.Minute / 10);
  writeNibble(tm.Minute % 10);
  writeNibble(tm.Second / 10);
  writeNibble(tm.Second % 10);
  writeNibble(0); // 00-99 milliseconds
  writeNibble(0);
}

static uint8_t bcdToDecimal(uint8_t v)
{
  return ((v & 0x0F) + (((v & 0xF0) >> 4) * 10));
}


void TeensyClock::updateClockFromRegister()
{
  uint8_t hours, minutes, seconds, days, months;
  uint16_t years;

  years   = bcdToDecimal(clockReg & 0xFF00000000000000LL >> 56) + 2000;
  months  = bcdToDecimal(clockReg & 0x00FF000000000000LL >> 48) - 1;
  days    = bcdToDecimal(clockReg & 0x0000FF0000000000LL >> 40);
  hours   = bcdToDecimal(clockReg & 0x00000000FF000000LL >> 24);
  minutes = bcdToDecimal(clockReg & 0x0000000000FF0000LL >> 16);
  seconds = bcdToDecimal(clockReg & 0x000000000000FF00LL >> 8);

  setTime(hours, minutes, seconds, days, months, years);
}
