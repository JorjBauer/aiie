#ifdef TEENSYDUINO
#include <Arduino.h>
#define assert(x)
#else
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#endif

#include "applemmu.h"
#include "applemmu-rom.h"
#include "physicalspeaker.h"
#include "cpu.h"

#include "globals.h"

#ifdef TEENSYDUINO
#include "teensy-clock.h"
#else
#include "nix-clock.h"
#endif

// Serializing token for MMU data
#define MMUMAGIC 'M'

// apple //e memory map

/*
page 0x00: zero page (straight ram)
page 0x01: stack (straight ram)
page 0x02: 
page 0x03: 
text/lores page 1: 0x0400 - 0x7FF
text/lores page 2: 0x0800 - 0xBFF
pages 0x0C - 0x1F: straight ram
hires page 1: pages 0x20 - 0x3F
hires page 2: pages 0x40 - 0x5F
pages 0x60 - 0xBF: straight ram
page 0xc0: I/O switches (some store 1-byte state)
pages 0xc1 - 0xcf: slot ROMs
pages 0xd0 - 0xdf: Basic ROM
pages 0xe0 - 0xff: monitor ROM
*/

/* 

   The memory model for this is...

page 0-1    4 pages (1k) [altzp]
2-0xBF      190 * 2 pages = 380 pages = 95k [auxRamRead/Write, S_HIRES, S_80STORE, S_PAGE2]
0xC0        1 page (256 bytes) (1-byte state for virtual I/O switches)
0xC1-0xCF   15 * 2 pages = 30 pages (7.5k) [intcxrom, slotLatch]
0xD0 - 0xDF 16 * 5 pages = 80 pages (20k) [altzp, bank2, {r,w}bsr]
0xE0 - 0xFF 32 * 3 pages = 96 pages (24k) [altzp, {r,w}bsr]

= 147.75k (591 pages) stored off-chip

Current read page table [512 bytes] in real ram
Current write page table [512 bytes] in real ram

 */

// This has a split memory model so more often addressed pages can be
// in internal memory, and an external memory can hold others.
//
// We want the video display pages in real RAM as much as possible,
// since blits wind up touching so much of it. If we can keep that in
// main RAM, then the blits won't try to read the external SRAM while
// the CPU is writing to it.
//
//
// After that it's all a guess. Should it be slot ROMs?
// extended RAM? Hires RAM? FIXME: do some analysis of common memory
// hotspots...

enum {
  // Pages we want to fall to internal RAM:
  MP_ZP  = 0,   // page 0/1 * 2 page variants = 4; 0..3
  MP_4 = 4, // 0x04 - 0x07 (text display pages) * 2 variants = 8; 4..11
  MP_20 = 12,   // 0x20 - 0x5F * 2 variants = 128; 12..139
  // Pages that can go to external RAM if needed:
  MP_2 = 140, // 0x02 - 0x03 * 2 variants = 4; 140..143
  MP_8 = 144, // 0x08 - 0x1F * 2 = 48; 144..191
  MP_60 = 192, // 0x60 - 0xBF * 2 = 192; 192..383

  MP_C1 = 384,   // start of 0xC1-0xCF * 2 page variants = 30; 384-413
  MP_D0 = 414,  // start of 0xD0-0xDF * 5 page variants = 80; 414-493
  MP_E0 = 493, // start of 0xE0-0xFF * 3 page variants = 96; 494-589
  MP_C0 = 590 
              // = 591 pages in all (147.75k)
};


static uint16_t _pageNumberForRam(uint8_t highByte, uint8_t variant)
{
  if (highByte <= 1) {
    // zero page.
    return highByte + (variant*2) + MP_ZP;
  }
  if (highByte <= 3) {
    return ((highByte - 2) * 2 + variant + MP_2);
  }
  if (highByte <= 7) {
    return ((highByte - 4) * 2 + variant + MP_4);
  }
  if (highByte <= 0x1f) {
    return ((highByte - 8) * 2 + variant + MP_8);
  }
  if (highByte <= 0x5f) {
    return ((highByte - 0x20) * 2 + variant + MP_20);
  }
  if (highByte <= 0xbf) {
    return ((highByte - 0x60) * 2 + variant + MP_60);
  }
  if (highByte == 0xc0) {
    return MP_C0;
  }
  if (highByte <= 0xCF) {
    // 0xC1-0xCF   15 * 2 pages = 30 pages (7.5k)
    return ((highByte - 0xC1) * 2 + variant + MP_C1);
  }

  if (highByte <= 0xDF) {
    // 0xD0 - 0xDF 16 * 5 pages = 80 pages (20k)
    return ((highByte - 0xD0) * 5 + variant + MP_D0);
  }

  // 0xE0 - 0xFF 32 * 3 pages = 96 pages (24k)
  return ((highByte - 0xE0) * 3 + variant + MP_E0);
}

AppleMMU::AppleMMU(AppleDisplay *display)
{
  anyKeyDown = false;

  for (int8_t i=0; i<=7; i++) {
    slots[i] = NULL;
  }

  this->display = display;
  this->display->setSwitches(&switches);
  resetRAM(); // initialize RAM, load ROM

#ifdef TEENSYDUINO
  clock = new TeensyClock((AppleMMU *)this);
#else
  clock = new NixClock((AppleMMU *)this);
#endif
}

AppleMMU::~AppleMMU()
{
  delete display;
}

bool AppleMMU::Serialize(int8_t fd)
{
  uint8_t buf[13] = { MMUMAGIC,
		      (switches >> 8) & 0xFF,
		      (switches     ) & 0xFF,
		      auxRamRead ? 1 : 0,
		      auxRamWrite ? 1 : 0,
		      bank2 ? 1 : 0,
		      readbsr ? 1 : 0,
		      writebsr ? 1 : 0,
		      altzp ? 1 : 0,
		      intcxrom ? 1 : 0,
		      slot3rom ? 1 : 0,
		      slotLatch,
		      preWriteFlag ? 1 : 0 };
  if (g_filemanager->write(fd, buf, 13) != 13)
    return false;
  
  if (!g_ram.Serialize(fd))
    return false;

  // readPages & writePages don't need suspending, but we will need to
  // recalculate after resume

  // Not suspending/resuming slots b/c they're a fixed configuration
  // in this project. Should probably checksum them though. FIXME.

  if (g_filemanager->write(fd, buf, 1) != 1)
    return false;
  
  return true;
}

bool AppleMMU::Deserialize(int8_t fd)
{
  uint8_t buf[13];

  if (g_filemanager->read(fd, buf, 13) != 13)
    return false;

  if (buf[0] != MMUMAGIC)
    return false;

  switches = (buf[1] << 8) | buf[2];
  auxRamRead = buf[3];
  auxRamWrite = buf[4];
  bank2 = buf[5];
  readbsr = buf[6];
  writebsr = buf[7];
  altzp = buf[8];
  intcxrom = buf[9];
  slot3rom = buf[10];
  slotLatch = buf[11];
  preWriteFlag = buf[12];
  
  if (!g_ram.Deserialize(fd)) {
    return false;
  }

  if (g_filemanager->read(fd, buf, 1) != 1)
    return false;
  if (buf[0] != MMUMAGIC)
    return false;

  // Reset readPages[] and writePages[] and the display
  resetDisplay();

  return true;
}

void AppleMMU::Reset()
{
  resetRAM();
  resetDisplay(); // sets the switches properly
}

uint8_t AppleMMU::read(uint16_t address)
{
  uint8_t rv = 0;
  if (handleNoSlotClock(address, &rv)) {
    return rv;
  }

  if (address >= 0xC000 &&
      address <= 0xC0FF) {
    return readSwitches(address);
  }

  // If C800-CFFF isn't latched to a slot ROM, and we try to 
  // access a slot's memory space from C100-C7FF, then we need 
  // to latch in the slot's ROM.
  if (slotLatch == -1 && address >= 0xc100 && address <= 0xc7ff) {
    slotLatch = (address >> 8) & 0x07;
    if (slotLatch == 3 && slot3rom) {
      // Back off: UTA2E p. 5-28: don't latch in slot 3 ROM while 
      // the slot3rom flag is enabled
      // fixme
      slotLatch = 3;
    } else {
      updateMemoryPages();
    }
  }

  // If we access CFFF, that unlatches slot ROM.
  if (address == 0xCFFF) {
    slotLatch = -1;
    updateMemoryPages();
  }

  uint8_t res = g_ram.readByte((readPages[address >> 8] << 8) | (address & 0xFF));
  return res;
}

// Bypass MMU and read directly from a given page - also bypasses switches
uint8_t AppleMMU::readDirect(uint16_t address, uint8_t fromPage)
{
  uint16_t page = _pageNumberForRam(address >> 8, fromPage);

  return g_ram.readByte((page << 8) | (address & 0xFF));
}

void AppleMMU::write(uint16_t address, uint8_t v)
{
  if (handleNoSlotClock(address, NULL)) {
    return;
  }

  if (address >= 0xC000 &&
      address <= 0xC0FF) {
    return writeSwitches(address, v);
  }

  // Don't allow writes to ROM
  // Hard ROM, I/O, slots, whatnot
  if (address >= 0xC100 && address <= 0xCFFF)
    return;
  // Bank-switched ROM/RAM areas
  if (address >= 0xD000 && address <= 0xFFFF && !writebsr) {
    return;
  }

  g_ram.writeByte((writePages[address >> 8] << 8) | (address & 0xFF), v);

  if (address >= 0x400 &&
      address <= 0x7FF) {

    // If it's text mode, or mixed mode, or lores graphics mode, then update.
    if ((switches & S_TEXT) || (switches & S_MIXED) || (!(switches & S_HIRES))) {
      // Force a redraw
      display->modeChange();
    }
    return;
  }

  if (address >= 0x2000 &&
      address <= 0x5FFF) {
    if (switches & S_HIRES) {
      // Force a redraw
      display->modeChange();
    }
  }
}

bool AppleMMU::handleNoSlotClock(uint16_t address, uint8_t *rv)
{
  uint8_t ah = address >> 8;
  if ( ((!intcxrom || !slot3rom) && (ah == 0xc3)) ||
       (ah == 0xc8) ) {
    if (rv) {
      // It's a read attempt - we want a return value.
      *rv = 0;
      if (clock->read(address, rv)) {
        return true;
      }
    } else {
      clock->write(address);
      return true;
    }
    
  }
  return false;
}

// FIXME: this is no longer "MMU", is it?
void AppleMMU::resetDisplay()
{
  updateMemoryPages();
  display->modeChange();
}

void AppleMMU::handleMemorySwitches(uint16_t address, uint16_t lastSwitch)
{
  // many of these are spelled out here: 
  // http://apple2.org.za/gswv/a2zine/faqs/csa2pfaq.html
  switch (address) {

  case 0xC000: // CLR80STORE
    switches &= ~S_80STORE;
    break;
  case 0xC001: // SET80STORE
    switches |= S_80STORE;
    break;
  case 0xC002: // CLRAUXRD read from main 48k RAM
    auxRamRead = false;
    break;
  case 0xC003: // SETAUXRD read from aux/alt 48k
    auxRamRead = true;
    break;
  case 0xC004: // CLRAUXWR write to main 48k RAM
    auxRamWrite = false;
    break;
  case 0xC005: // SETAUXWR write to aux/alt 48k
    auxRamWrite = true;
    break;
  case 0xC006: // CLRCXROM use ROM on cards
    intcxrom = false;
    break;
  case 0xC007: // SETCXROM use internal ROM
    intcxrom = true;
    break;
  case 0xC008: // CLRAUXZP use main zero page, stack, LC
    altzp = false;
    break;
  case 0xC009: // SETAUXZP use alt zero page, stack, LC
    altzp = true;
    break;
  case 0xC00A: // CLRC3ROM use internal slot 3 ROM
    slot3rom = false;
    break;
  case 0xC00B: // SETC3ROM use external slot 3 ROM
    slot3rom = true;
    break;

    // Registers C080 - C08F control bank switching.
  case 0xC080:
  case 0xC081:
  case 0xC082:
  case 0xC083:
  case 0xC084:
  case 0xC085:
  case 0xC086:
  case 0xC087:
  case 0xC088:
  case 0xC089:
  case 0xC08A:
  case 0xC08B:
  case 0xC08C:
  case 0xC08D:
  case 0xC08E:
  case 0xC08F:

    // Per ITA2E, p. 286:
    // (address & 0x08) controls whether or not we are selecting from bank2. Per table 8-2,
    // bank2 is active if address & 0x08 is zero. So if the bit is on, it's bank 1.
    bank2 = (address & 0x08) ? false : true;

    // (address & 0x04) is unused.

    // (address & 0x02) is read-select: if it is set the same as
    // (address & 0x01) then readbsr is true.
    readbsr = ((address & 0x02) >> 1) == (address & 0x01);

    // (address & 0x01) is write-select: if 1, we write BSR RAM; if 0, we write ROM.
    // But it's a little more complicated than readbsr.
    // Per UTA2E p. 5-23:
    //   "Writing to high RAM is enabled when the HRAMWRT' soft switch
    //   is reset. ... It is reset by even read access or any write
    //   access in the $C08X range. HRAMWRT' is reset by odd read
    //   access in the $C08X range when PRE-WRITE is set. It is set by
    //   even access in the CC08X range. Any other type of access
    //   causes HRAMWRT' to hold its current state."

    if (address & 0x01) {
      if (preWriteFlag)
	writebsr = 1;
      // Per UTA2E, p. 5-23: any other preWriteFlag leaves writebsr unchanged.
    } else {
      writebsr = false;
    }

    break;
  }

  updateMemoryPages();
}

// many (most? all?) switches are documented here:
//   http://apple2.org.za/gswv/a2zine/faqs/csa2pfaq.html

uint8_t AppleMMU::readSwitches(uint16_t address)
{
  static uint16_t lastReadSwitch = 0x0000;
  static uint16_t thisReadSwitch = 0x0000;

  lastReadSwitch = thisReadSwitch;
  thisReadSwitch = address;

  // If this is a read for any of the slot switches, and we have
  // hardware in that slot, then return its result.
  if (address >= 0xC090 && address <= 0xC0FF) {
    for (uint8_t i=1; i<=7; i++) {
      if (address >= (0xC080 | (i << 4)) &&
	  address <= (0xC08F | (i << 4))) {
	if (slots[i]) {
	  return slots[i]->readSwitches(address & ~(0xC080 | (i<<4)));
	}
	else
	  return FLOATING;
      }
    }
  }

  switch (address) {
  case 0xC010:
    // consume the keyboard strobe flag
    g_ram.writeByte((writePages[0xC0] << 8) | 0x10, 
		    g_ram.readByte((readPages[0xC0] << 8) | 0x10) & 0x7F);
    return (anyKeyDown ? 0x80 :  0x00);

  case 0xC080:
  case 0xC081:
  case 0xC082:
  case 0xC083:
  case 0xC084:
  case 0xC085:
  case 0xC086:
  case 0xC087:
  case 0xC088:
  case 0xC089:
  case 0xC08A:
  case 0xC08B:
  case 0xC08C:
  case 0xC08D:
  case 0xC08E:
  case 0xC08F:
    // but read does affect these, same as write
    handleMemorySwitches(address, lastReadSwitch);

    // UTA2E, p. 5-23: preWrite is set by odd read access, and reset
    // by even read access
    preWriteFlag = (address & 0x01);

    return FLOATING;

  case 0xC00C: // CLR80VID disable 80-col video mode
    if (switches & S_80COL) {
      switches &= ~S_80COL;
      resetDisplay();
    }
    break; // fall through
  case 0xC00D: // SET80VID enable 80-col video mode
    if (!(switches & S_80COL)) {
      switches |= S_80COL;
      resetDisplay();
    }
    break; // fall through

  case 0xC00E: // CLRALTCH use main char set - norm LC, flash UC
    switches &= ~S_ALTCH;
    break; // fall through
  case 0xC00F: // SETALTCH use alt char set - norm inverse, LC; no flash
    switches |= S_ALTCH;
    break; // fall through


  case 0xC011: // RDLCBNK2
    return bank2 ? 0x80 : 0x00;
  case 0xC012: // RDLCRAM
    return readbsr ? 0x80 : 0x00;
  case 0xC013: // RDRAMRD
    return auxRamRead ? 0x80 : 0x00;
  case 0xC014: // RDRAMWR
    return auxRamWrite ? 0x80 : 0x00;
  case 0xC015: // RDCXROM
    return intcxrom ? 0x80 : 0x00;
  case 0xC016: // RDAUXZP
    return altzp ? 0x80 : 0x00;
  case 0xC017: // RDC3ROM
    return slot3rom ? 0x80 : 0x00;

  case 0xC018: // RD80COL
    return (switches & S_80STORE) ? 0x80 : 0x00;
  case 0xC019: // RDVBLBAR -- vertical blanking, for 4550 cycles of every 17030
    // Should return 0 for 4550 of 17030 cycles. Since we're not really 
    // running full speed video, instead, I'm returning 0 for 4096 (2^12)
    // of every 16384 (2^14) cycles; the math is easier.
    if ((g_cpu->cycles & 0x3000) == 0x3000) {
      return 0x00;
    } else {
      return 0xFF; // FIXME: is 0xFF correct? Or 0x80?
    }
  case 0xC01A: // RDTEXT
    return ( (switches & S_TEXT) ? 0x80 : 0x00 );
  case 0xC01B: // RDMIXED
    return ( (switches & S_MIXED) ? 0x80 : 0x00 );
  case 0xC01C: // RDPAGE2
    return ( (switches & S_PAGE2) ? 0x80 : 0x00 );
  case 0xC01D: // RDHIRES
    return ( (switches & S_HIRES) ? 0x80 : 0x00 );
  case 0xC01E: // RDALTCH
    return ( (switches & S_ALTCH) ? 0x80 : 0x00 );
  case 0xC01F: // RD80VID
    return ( (switches & S_80COL) ? 0x80 : 0x00 );


  case 0xC030: // SPEAKER
    g_speaker->toggle(g_cpu->cycles);
#ifndef SUPPRESSREALTIME
    g_cpu->realtime(); // cause the CPU to stop processing its outer
		       // loop b/c the speaker might need attention
		       // immediately
#endif
    return FLOATING;

  case 0xC050: // CLRTEXT
    if (switches & S_TEXT) {
      switches &= ~S_TEXT;
      resetDisplay();
    }
    return FLOATING;
  case 0xC051: // SETTEXT
    if (!(switches & S_TEXT)) {
      switches |= S_TEXT;
      resetDisplay();
    }
    return FLOATING;
  case 0xC052: // CLRMIXED
    if (switches & S_MIXED) {
      switches &= ~S_MIXED;
      resetDisplay();
    }
    return FLOATING;
  case 0xC053: // SETMIXED
    if (!(switches & S_MIXED)) {
      switches |= S_MIXED;
      resetDisplay();
    }
    return FLOATING;

  case 0xC054: // PAGE1
    if (switches & S_PAGE2) {
      switches &= ~S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return FLOATING;

  case 0xC055: // PAGE2
    if (!(switches & S_PAGE2)) {
      switches |= S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return FLOATING;

  case 0xC056: // CLRHIRES
    if (switches & S_HIRES) {
      switches &= ~S_HIRES;
      resetDisplay();
    }
    return FLOATING;
  case 0xC057: // SETHIRES
    if (!(switches & S_HIRES)) {
      switches |= S_HIRES;
      resetDisplay();
    }
    return FLOATING;

  case 0xC05E: // DHIRES ON
    if (!(switches & S_DHIRES)) {
      switches |= S_DHIRES;
      resetDisplay();
    }
    return FLOATING;

  case 0xC05F: // DHIRES OFF
    if (switches & S_DHIRES) {
      switches &= ~S_DHIRES;
      resetDisplay();
    }
    return FLOATING;

    // paddles
    /* Fall through for apple keys; they're just RAM in this model 
  case 0xC061: // OPNAPPLE
    return isOpenApplePressed ? 0x80 : 0x00;
  case 0xC062: // CLSAPPLE
    return isClosedApplePressed ? 0x80 : 0x00;
*/
    
  case 0xC070: // PDLTRIG
    // It doesn't matter if we update readPages or writePages, because 0xC0 
    // has only one page.
    g_ram.writeByte((writePages[0xC0] << 8) | 0x64, 0xFF);
    g_ram.writeByte((writePages[0xC0] << 8) | 0x65, 0xFF);
    g_paddles->startReading();
    return FLOATING;
  }

  if (address >= 0xc000 && address <= 0xc00f) {
    // This is the keyboardStrobe support referenced in the switch statement above.
    return g_ram.readByte((readPages[0xC0] << 8) | 0x10);
  }

  /* *** FIXME: 
SETIOUDIS= $C07E ;enable DHIRES & disable $C058-5F (W) 
CLRIOUDIS= $C07E ;disable DHIRES & enable $C058-5F (W) 
0xC05e and 0xc05f should fall through if that IOUDIS is not activated

need to see if that's a toggle, or if it's a typo (c07f, maybe?)
   */

  return g_ram.readByte((readPages[address >> 8] << 8) | (address & 0xFF));
}

void AppleMMU::writeSwitches(uint16_t address, uint8_t v)
{
  // fixme: combine these with the last read switch
  static uint16_t lastWriteSwitch = 0x0000;
  static uint16_t thisWriteSwitch = 0x0000;
  lastWriteSwitch = thisWriteSwitch;
  thisWriteSwitch = address;

  // If this is a write for any of the slot switches, and we have
  // hardware in that slot, then return its result.
  if (address >= 0xC090 && address <= 0xC0FF) {
    for (uint8_t i=1; i<=7; i++) {
      if (address >= (0xC080 | (i << 4)) &&
	  address <= (0xC08F | (i << 4))) {
	if (slots[i]) {
	  slots[i]->writeSwitches(address & ~(0xC080 | (i<<4)), v);
	}
      }
    }
  }

  switch (address) {
  case 0xC010:
  case 0xC011: // Per Understanding the Apple //e, p. 7-3:
  case 0xC012: //   a write to any $C01x address causes 
  case 0xC013: //   a clear of the keyboard strobe.
  case 0xC014:
  case 0xC015:
  case 0xC016:
  case 0xC017:
  case 0xC018:
  case 0xC019:
  case 0xC01A:
  case 0xC01B:
  case 0xC01C:
  case 0xC01D:
  case 0xC01E:
  case 0xC01F:
    // Consume keyboard strobe
    g_ram.writeByte((writePages[0xC0] << 8) | 0x10, 
		    g_ram.readByte((readPages[0xC0] << 8) | 0x10) & 0x7F);
    return;

  case 0xC030: // SPEAKER
    // Writes toggle the speaker twice
    g_speaker->toggle(g_cpu->cycles);
    g_speaker->toggle(g_cpu->cycles);
#ifndef SUPPRESSREALTIME
    g_cpu->realtime(); // cause the CPU to stop processing its outer
		       // loop b/c the speaker might need attention
		       // immediately
#endif
    return;

  case 0xC050: // graphics mode
    if (switches & S_TEXT) {
      switches &= ~S_TEXT;
      resetDisplay();
    }
    return;
     
  case 0xC051:
    if (!(switches & S_TEXT)) {
      switches |= S_TEXT;
      resetDisplay();
    }
    return;

  case 0xC052: // "no mixed"
    if (switches & S_MIXED) {
      switches &= ~S_MIXED;
      resetDisplay();
    }
    return;

  case 0xC053: // "mixed"
    if (!(switches & S_MIXED)) {
      switches |= S_MIXED;
      resetDisplay();
    }
    return;

  case 0xC054: // page2 off
    if (switches & S_PAGE2) {
      switches &= ~S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return;

  case 0xC055: // page2 on
    if (!(switches & S_PAGE2)) {
      switches |= S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return;

  case 0xC056: // hires off
    if (switches & S_HIRES) {
      switches &= ~S_HIRES;
      resetDisplay();
    }
    return;

  case 0xC057: // hires on
    if (!(switches & S_HIRES)) {
      switches |= S_HIRES;
      resetDisplay();
    }
    return;

  case 0xC05E: // DHIRES ON
    if (!(switches & S_DHIRES)) {
      switches |= S_DHIRES;
      resetDisplay();
    }
    return;

  case 0xC05F: // DHIRES OFF
    if (switches & S_DHIRES) {
      switches &= ~S_DHIRES;
      resetDisplay();
    }
    return;

    // paddles
  case 0xC070:
    g_paddles->startReading();
    g_ram.writeByte((writePages[0xC0] << 8) | 0x64, 0xFF);
    g_ram.writeByte((writePages[0xC0] << 8) | 0x65, 0xFF);
    return;

  case 0xC080:
  case 0xC081:
  case 0xC082:
  case 0xC083:
  case 0xC084:
  case 0xC085:
  case 0xC086:
  case 0xC087:
  case 0xC088:
  case 0xC089:
  case 0xC08A:
  case 0xC08B:
  case 0xC08C:
  case 0xC08D:
  case 0xC08E:
  case 0xC08F:
    // UTA2E, p. 5-23: preWrite is reset by any write access to these
    preWriteFlag = 0;
    // fall through...
  case 0xC000:
  case 0xC001:
  case 0xC002:
  case 0xC003:
  case 0xC004:
  case 0xC005:
  case 0xC006:
  case 0xC007:
  case 0xC008:
  case 0xC009:
  case 0xC00A:
  case 0xC00B:
    handleMemorySwitches(address, lastWriteSwitch);
    return;

  case 0xC00C: // CLR80VID disable 80-col video mode
    if (switches & S_80COL) {
      switches &= ~S_80COL;
      resetDisplay();
    }
    return;

  case 0xC00D: // SET80VID enable 80-col video mode
    if (!(switches & S_80COL)) {
      switches |= S_80COL;
      resetDisplay();
    }
    return;

  case 0xC00E: // CLRALTCH use main char set - norm LC, flash UC
    switches &= ~S_ALTCH;
    return;
  case 0xC00F: // SETALTCH use alt char set - norm inverse, LC; no flash
    switches |= S_ALTCH;
    return;
  }

  // Anything that falls through gets written to RAM.
  g_ram.writeByte((writePages[0xC0] << 8) | (address & 0xFF),
		  v);
}

void AppleMMU::keyboardInput(uint8_t v)
{
  // Set keyboard strobe
  g_ram.writeByte((writePages[0xC0] << 8) | 0x10, 
		  v | 0x80);
  anyKeyDown = true;
}

void AppleMMU::setKeyDown(bool isTrue)
{
  anyKeyDown = isTrue;
}

void AppleMMU::triggerPaddleTimer(uint8_t paddle)
{
  g_ram.writeByte((writePages[0xC0] << 8) | (0x64 + paddle), 0);
}

void AppleMMU::resetRAM()
{
  switches = S_TEXT;

  // Per UTA2E, p. 5-23:
  // When a system reset occurs, all MMU soft switches are reset (turned off).
  bank2 = false;
  auxRamRead = auxRamWrite = false;
  readbsr = writebsr = false;
  altzp = false;

  intcxrom = false;
  slot3rom = false;

  slotLatch = -1;

  preWriteFlag = false;

  g_ram.init();
  for (uint16_t i=0; i<0x100; i++) {
    readPages[i] = writePages[i] = _pageNumberForRam(i, 0);
  }

  // Load system ROM
  for (uint16_t i=0x80; i<=0xFF; i++) {
    for (uint16_t k=0; k<0x100; k++) {
      uint16_t idx = ((i-0x80) << 8) | k;
#ifdef TEENSYDUINO
      uint8_t v = pgm_read_byte(&romData[idx]);
#else
      uint8_t v = romData[idx];
#endif
      for (int j=0; j<5; j++) {
	// For the ROM section from 0xc100 .. 0xcfff, we load in to 
	// an alternate page space (INTCXROM).

	uint16_t page0 = _pageNumberForRam(i, 0);

	if (i >= 0xc1 && i <= 0xcf) {
	  // If we want to convince the VM we've got 128k of RAM, we 
	  // need to load C3 ROM in page 0 (but not 1, meaning there's 
	  // a board installed); and C800.CFFF in both page [0] and [1]
	  // (meaning there's an extended 80-column ROM available, 
	  // that is also physically in the slot).
	  // Everything else goes in page [1].

	  uint16_t page1 = _pageNumberForRam(i, 1);

	  if (i == 0xc3) {
	    g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
	  }
	  else if (i >= 0xc8) {
	    g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
	    g_ram.writeByte((page1 << 8) | (k & 0xFF), v);
	  }
	  else {
	    g_ram.writeByte((page1 << 8) | (k & 0xFF), v);
	  }
	} else {
	  // Everything else goes in page 0.
	  g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
	}
      }
    }
  }

  // have each slot load its ROM
  for (uint8_t slotnum = 1; slotnum <= 7; slotnum++) {
    uint16_t page0 = _pageNumberForRam(0xC0 + slotnum, 0);
    if (slots[slotnum]) {
      uint8_t tmpBuf[256];
      memset(tmpBuf, 0, sizeof(tmpBuf));
      slots[slotnum]->loadROM(tmpBuf);
      for (int i=0; i<256; i++) {
	g_ram.writeByte( (page0 << 8) + i, tmpBuf[i] );
      }
    }
  }

  // update the memory read/write flags &c. Not strictly necessary, if
  // we're really setting all the RAM flags to the right default
  // settings above - but better safe than sorry?
  updateMemoryPages();
}

void AppleMMU::setSlot(int8_t slotnum, Slot *peripheral)
{
  if (slots[slotnum]) {
    delete slots[slotnum];
  }

  slots[slotnum] = peripheral;
  if (slots[slotnum]) {
    uint16_t page0 = _pageNumberForRam(0xC0 + slotnum, 0);
    uint8_t tmpBuf[256];
    memset(tmpBuf, 0, sizeof(tmpBuf));
    slots[slotnum]->loadROM(tmpBuf);
    for (int i=0; i<256; i++) {
      g_ram.writeByte( (page0 << 8) + i, tmpBuf[i] );
    }
  }
}

void AppleMMU::updateMemoryPages()
{
  if (auxRamRead) {
    for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 1);
    }
  } else {
    for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 0);
    }
  }

  if (auxRamWrite) {
    for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
      writePages[idx] = _pageNumberForRam(idx, 1);
    }
  } else {
    for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
      writePages[idx] = _pageNumberForRam(idx, 0);
    }
  }

  if (switches & S_80STORE) {
    // When S_80STORE is on, we switch 400-800 and 2000-4000 based on S_PAGE2.
    // The behavior is different based on whether HIRESON/OFF is set.
    if (switches & S_PAGE2) {
      // Regardless of HIRESON/OFF, pages 0x400-0x7ff are switched on S_PAGE2
      for (uint8_t idx = 0x04; idx < 0x08; idx++) {
	readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 1);
      }

      // but 2000-3fff switches based on S_PAGE2 only if HIRES is on.

    // HIRESOFF: 400-7ff doesn't switch based on read/write flags
    //           b/c it switches based on S_PAGE2 instead
    // HIRESON: 400-800, 2000-3fff doesn't switch
    //          b/c they switch based on S_PAGE2 instead

      // If HIRES is on, then we honor the PAGE2 setting; otherwise, we don't
      for (uint8_t idx = 0x20; idx < 0x40; idx++) {
	readPages[idx] = writePages[idx] = _pageNumberForRam(idx, (switches & S_HIRES) ? 1 : 0);
      }
    } else {
      for (uint8_t idx = 0x04; idx < 0x08; idx++) {
	readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 0);
      }
      for (uint8_t idx = 0x20; idx < 0x40; idx++) {
	readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 0);
      }
    }
  }

  if (intcxrom) {
    for (uint8_t idx = 0xc1; idx < 0xd0; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 1);
    }
  } else {
    for (uint8_t idx = 0xc1; idx < 0xd0; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 0);
    }
    if (slot3rom) {
      readPages[0xc3] = _pageNumberForRam(0xc3, 1);
      for (int i=0xc8; i<=0xcf; i++) {
      readPages[i] = _pageNumberForRam(i, 1);
      }
    }
  }

  // If slotLatch is set (!= -1), then we are mapping 2k of ROM
  // for a given peripheral to C800..CFFF.
  if (slotLatch != -1) {
    // FIXME: the only peripheral we support this with right now is 
    // the 80-column card.
    if (slotLatch == 3) {
      for (int i=0xc8; i <= 0xcf; i++) {
	readPages[i] = _pageNumberForRam(i, 1);
      }
    }
  }

  // set zero-page & stack pages based on altzp flag
  if (altzp) {
    for (uint8_t idx = 0x00; idx < 0x02; idx++) {
      readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 1);
    }
  } else {
    for (uint8_t idx = 0x00; idx < 0x02; idx++) {
      readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 0);
    }
  }

  // Set bank-switched ram reading from readbsr & bank2
  if (readbsr) {
    // 0xD0 - 0xE0 has 4 possible banks:
    if (!bank2) {
      // Bank 1 RAM: either in main RAM (1) or in the extended memory
      // card (3):
      for (uint8_t idx = 0xd0; idx < 0xe0; idx++) {
	readPages[idx] = _pageNumberForRam(idx, altzp ? 3 : 1);
      }
    } else {
      // Bank 2 RAM: either in main RAM (2) or in the extended memory
      // card (4):
      for (uint8_t idx = 0xd0; idx < 0xe0; idx++) {
	readPages[idx] = _pageNumberForRam(idx, altzp ? 4 : 2);
      }
    }
    // ... but 0xE0 - 0xFF has just the motherboard RAM (1) and
    // extended memory card RAM (2):
    for (uint16_t idx = 0xe0; idx < 0x100; idx++) {
      readPages[idx] = _pageNumberForRam(idx, altzp ? 2 : 1);
    }
  } else {
    // Built-in ROM
    for (uint16_t idx = 0xd0; idx < 0x100; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 0);
    }
  }

  if (writebsr) {
    if (!bank2) {
      for (uint8_t idx = 0xd0; idx < 0xe0; idx++) {
	writePages[idx] = _pageNumberForRam(idx, altzp ? 3 : 1);
      }
    } else {
      for (uint8_t idx = 0xd0; idx < 0xe0; idx++) {
	writePages[idx] = _pageNumberForRam(idx, altzp ? 4 : 2);
      }
    }
    for (uint16_t idx = 0xe0; idx < 0x100; idx++) {
      writePages[idx] = _pageNumberForRam(idx, altzp ? 2 : 1);
    }
  } else {
    for (uint16_t idx = 0xd0; idx < 0x100; idx++) {
      writePages[idx] = _pageNumberForRam(idx, 0);
    }
  }
}

void AppleMMU::setAppleKey(int8_t which, bool isDown)
{
  assert(which <= 1);
  g_ram.writeByte((writePages[0xC0] << 8) | (0x61 + which), isDown ? 0x80 : 0x00);
}
