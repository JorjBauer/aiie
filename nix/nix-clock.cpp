#include <string.h> // memset
#include <time.h>

#include "noslotclock.h"

#include "nix-clock.h"

NixClock::NixClock(AppleMMU *mmu) : NoSlotClock(mmu)
{
}

NixClock::~NixClock()
{
}

void NixClock::populateClockRegister()
{
  time_t lt;
  time(&lt);
  struct tm *ct = localtime(&lt);

  clockReg = 0x0;

  // BCD, 4 bits per digit.

  ct->tm_year %= 100; // must be 00-99
  writeNibble(ct->tm_year / 10);
  writeNibble(ct->tm_year % 10);

  ct->tm_mon++; // 1 = January
  writeNibble(ct->tm_mon / 10);
  writeNibble(ct->tm_mon % 10);

  writeNibble(ct->tm_mday / 10);
  writeNibble(ct->tm_mday % 10);// day of month, 1-31

  writeNibble(0);
  writeNibble(ct->tm_wday + 1); // day of week, 1-7

  writeNibble(ct->tm_hour / 10);
  writeNibble(ct->tm_hour % 10);

  writeNibble(ct->tm_min / 10);
  writeNibble(ct->tm_min % 10);

  writeNibble(ct->tm_sec / 10); // tens of seconds
  writeNibble(ct->tm_sec % 10); // ones of seconds, 00-99

  writeNibble(0); // ones of milliseconds, 00-99
  writeNibble(0); // tens of milliseconds
}

void NixClock::updateClockFromRegister()
{
  // The clockReg should now contain a BCD4 packed date like                
  // 0x1708071521140200                                                     
  //   ... 2017, August 07, <day of week?>; 21:14:02.00                     
  // where that <day of week> is clearly suspect. Probably because 2017     
  // was too far in the future when this driver was written...              

  printf(">> Got a request to set clock: 0x%llX\n", clockReg);
}
