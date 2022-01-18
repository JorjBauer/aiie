#include "noslotclock.h"

#define initSequence 0x5CA33AC55CA33AC5LL

/* The no-slot clock works like this...
 *
 * The NSC is installed in some bank of ROM memory. For our instance,
 * it's 0xC300 or 0xC800.  When a read or write occurs in these pages
 * - and ROM isn't switched out or whatever - then this driver is
 * invoked.
 *
 * (It's the job of applemmu to decide if the right address is being
 * called to invoke the read or write here.)
 *
 * To get the clock to respond, we need to first pass it the init
 * Sequence (above). The NSC gets one bit at a time from watching the
 * transactions on the bus. So first the driver reads or writes to
 * memory address (e.g.) 0xC800 or 0xC801, depending on whether it
 * wants to send a 0 or 1 bit; and then, if the NSC sees all of the
 * correct bits for the init sequence, it allows responses when
 * reading from memory (e.g.) 0xC804. These responses are, again, one
 * bit at a time of the current date and time.
 */

NoSlotClock::NoSlotClock(AppleMMU *mmu)
{
  this->mmu = mmu;

  compareReg = initSequence;
  clockReg = 0x00LL;
  clockRegPtr = compareRegPtr = 0;

  regEnabled = false;
  writeEnabled = true;
}

NoSlotClock::~NoSlotClock()
{
}

bool NoSlotClock::read(uint8_t s, uint8_t *d)
{
  if (s & 0x04) {
    return doRead(d);
  }
  else {
    doWrite(s);
    return false;
  }
}

void NoSlotClock::write(uint8_t s)
{
  if (s & 0x04) {
    doRead(0);
  } else {
    doWrite(s);
  }
}

bool NoSlotClock::doRead(uint8_t *d)
{
  if (!regEnabled) {
    compareReg = initSequence;
    compareRegPtr = 0;
    writeEnabled = true;
    return false;
  }

  if (d) {
    *d = (clockReg & 0x01) ?
      ((*d) | 1) :
      ((*d) & ~1);
  }
  clockRegPtr++;
  clockReg >>= 1;
  if (clockRegPtr == 64) {
    regEnabled = false;
    clockRegPtr = 0;
  }
  return true;
}

void NoSlotClock::doWrite(uint8_t address)
{
  if (!writeEnabled) {
    return;
  }

  if (!regEnabled) {
    if ((compareReg & 0x01) == (address & 0x01)) {
      compareRegPtr++;
      compareReg >>= 1;
      if (compareRegPtr == 64) {
	regEnabled = true;

	compareRegPtr = 0;
	compareReg = initSequence;

	populateClockRegister();
      }
    } else {
      writeEnabled = false;
    }
  } else {
    // The NSC driver is writing a new clock time to the clock...
    clockRegPtr++;
    clockReg >>= 1;
    if (address & 0x01) {
      clockReg |= 0x8000000000000000LL;
    }
    if (clockRegPtr == 64) {
      regEnabled = false;

      // The clockReg should now contain a BCD4 packed date like
      // 0x1708071521140200
      //   ... 2017, August 07, <day of week?>; 21:14:02.00
      // where that <day of week> is clearly suspect. Probably because 2017 
      // was too far in the future when this driver was written...

      clockRegPtr = 0;

      updateClockFromRegister();
    }
  }
}

void NoSlotClock::writeNibble(uint8_t n)
{
  for (uint8_t i=0; i<4; i++) {
    clockReg <<= 1;
    if (n & 0x08) {
      clockReg |= 1;
    }
    n <<= 1;
  }
}
