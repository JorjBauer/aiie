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

#include "serialize.h"

#include "globals.h"

#ifdef TEENSYDUINO
#include "teensy-clock.h"
#include "iocompat.h"
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
... plus 8 additional pages for the mouse ROM

= 147.75k (591 pages) stored off-chip (+ 8 more)

Current read page table [512 bytes] in real ram
Current write page table [512 bytes] in real ram

 */

// All the pages. Because we don't have enough RAM for both the
// display's DMA and the Apple's 148k (128k + ROM space), we're using
// an external SRAM for some of this. Anything that's accessed very
// often should be in the *low* pages, b/c those are in internal
// Teensy RAM. When we run out of preallocated RAM (cf. vmram.h), we
// fall over to an external 256kB SRAM (which is much slower).
//
// Zero page (and its alts) are the most used pages (the stack is in
// page 1).
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
  // Pages that can go to external SRAM:
  MP_2 = 140, // 0x02 - 0x03 * 2 variants = 4; 140..143
  MP_8 = 144, // 0x08 - 0x1F * 2 = 48; 144..191
  MP_60 = 192, // 0x60 - 0xBF * 2 = 192; 192..383

  MP_C1 = 384,   // start of 0xC1-0xC7 * 2 page variants = 14; 384-397
  MP_C8 = 398, // 0xc8 - 0xcf, 3 page variants = 24; 398 - 421
  MP_D0 = 422,  // start of 0xD0-0xDF * 5 page variants = 80; 422 - 501
  MP_E0 = 502, // start of 0xE0-0xFF * 3 page variants = 96; 502 - 597
  MP_C0 = 598
              // = 599 pages in all (149.75k)
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
  if (highByte <= 0xC7) {
    // 0xC1-0xC7
    return ((highByte - 0xC1) * 2 + variant + MP_C1);
  }
  if (highByte <= 0xCF) {
    // bank-switched ROM. 0 = built-in; 1 = 80-column (slot 3); 2 = mouse (slot 4)
    return ((highByte - 0xC8) * 3 + variant + MP_C8);
  }
  if (highByte <= 0xDF) {
    // 0xD0 - 0xDF 16 * 5 pages = 80 pages (20k)
    return ((highByte - 0xD0) * 5 + variant + MP_D0);
  }

  // 0xE0 - 0xFF 32 * 3 pages = 96 pages (24k)
  return ((highByte - 0xE0) * 3 + variant + MP_E0);
}

// The RAM page a slot's card ROM should be written to. For most slots that is
// variant 0 of the slot's $CsXX page. Slot 3 is special: its variant-0 bank
// holds the internal 80-column firmware, so a slot-3 card's ROM goes in variant
// 1 (the bank selected when slot3rom is set) to avoid clobbering 80 columns.
// A card with no ROM (e.g. the Uthernet) then leaves 80-column firmware intact.
static uint16_t _slotRomPageForSlot(uint8_t slotnum)
{
  if (slotnum == 3) return _pageNumberForRam(0xC3, 1);
  return _pageNumberForRam(0xC0 + slotnum, 0);
}

// THE ONE MACHINE. There is exactly one AppleMMU in a running program, the
// same way there is one g_cpu and one g_display, and a card that needs the
// floating bus has no pointer to it. Registered by the constructor rather
// than reached through g_vm because the disk benches run a DiskII with no
// VM at all, and answering 0 there is right where crashing is not.
static AppleMMU *g_theAppleMMU = NULL;

AppleMMU::AppleMMU(AppleDisplay *display)
{
  anyKeyDown = false;

  videoLogHead = 0;
  videoLogCount = 0;
  lastLoggedSwitches = 0xFFFF; // force the first logVideoState() to record

  g_theAppleMMU = this; // see appleFloatingBus()

  for (int8_t i=0; i<=7; i++) {
    slots[i] = NULL;
  }

  // RamWorks aux expansion starts unconfigured; setRamworksSize() below
  // allocates based on the user's preference.
  auxBank = 0;
  numAuxBanks = 1;
  auxExpansion = NULL;

  this->display = display;
  this->display->setSwitches(&switches);
  resetRAM(); // initialize RAM, load ROM

  setRamworksSize(g_ramworksSize);

#ifdef TEENSYDUINO
  clock = new TeensyClock((AppleMMU *)this);
#else
  clock = new NixClock((AppleMMU *)this);
#endif
}

AppleMMU::~AppleMMU()
{
  if (g_theAppleMMU == this) g_theAppleMMU = NULL;
  delete display;
  if (auxExpansion) {
#ifdef TEENSYDUINO
    extmem_free(auxExpansion);
#else
    free(auxExpansion);
#endif
    auxExpansion = NULL;
  }
}

bool AppleMMU::Serialize(int8_t fd)
{
  serializeMagic(MMUMAGIC);
  serialize16(switches);
  serialize8(auxRamRead ? 1 : 0);
  serialize8(auxRamWrite ? 1 : 0);
  serialize8(bank2 ? 1 : 0);
  serialize8(readbsr ? 1 : 0);
  serialize8(writebsr ? 1 : 0);
  serialize8(altzp ? 1 : 0);
  serialize8(intcxrom ? 1 : 0);
  serialize8(slot3rom ? 1 : 0);
  serialize8(slotLatch);
  serialize8(preWriteFlag ? 1 : 0);
  
  if (!g_ram.Serialize(fd)) {
    printf("Failed to serialize RAM\n");
    goto err;
  }

  // RamWorks aux-expansion state, including the contents of banks 1..N-1.
  serialize8(auxBank);
  serialize16(numAuxBanks);
  {
    uint32_t bytes = (numAuxBanks > 1 && auxExpansion) ?
      (uint32_t)(numAuxBanks - 1) * 0x10000 : 0;
    serialize32(bytes);
    if (bytes) {
      if ((uint32_t)g_filemanager->write(fd, auxExpansion, bytes) != bytes) {
        printf("Failed to serialize RamWorks banks\n");
        goto err;
      }
    }
  }

  // readPages & writePages don't need suspending, but we will need to
  // recalculate after resume

  // Not suspending/resuming slots b/c they're a fixed configuration
  // in this project. Should probably checksum them though. FIXME.

  serializeMagic(MMUMAGIC);
  return true;

 err:
  return false;
}

bool AppleMMU::Deserialize(int8_t fd)
{
  deserializeMagic(MMUMAGIC);

  deserialize16(switches);
  deserialize8(auxRamRead);
  deserialize8(auxRamWrite);
  deserialize8(bank2);
  deserialize8(readbsr);
  deserialize8(writebsr);
  deserialize8(altzp);
  deserialize8(intcxrom);
  deserialize8(slot3rom);
  deserialize8(slotLatch);
  deserialize8(preWriteFlag);

  if (!g_ram.Deserialize(fd)) {
    goto err;
  }

  // Restore RamWorks state and resize/refill the expansion buffer.
  {
    uint8_t savedBank;
    uint16_t savedNumBanks;
    uint32_t bytes;
    deserialize8(savedBank);
    deserialize16(savedNumBanks);
    deserialize32(bytes);

    // Size the expansion buffer to the saved bank count. When the size is
    // unchanged (the common case: restoring a snapshot taken on this same
    // configuration) reuse the buffer in place rather than free+malloc: under
    // WASM heap pressure that churn could fail, and the old silent fallback
    // to a single aux bank made "bank 1" (code) and "bank 3" (heap) alias
    // onto the same memory, corrupting relocated code as the heap grew.
    uint16_t newBanks = (savedNumBanks < 1) ? 1 : savedNumBanks;
    uint32_t want = (newBanks > 1) ? (uint32_t)(newBanks - 1) * 0x10000 : 0;
    uint32_t have = (auxExpansion && numAuxBanks > 1) ?
      (uint32_t)(numAuxBanks - 1) * 0x10000 : 0;
    if (want != have) {
      if (auxExpansion) {
#ifdef TEENSYDUINO
        extmem_free(auxExpansion);
#else
        free(auxExpansion);
#endif
        auxExpansion = NULL;
      }
      if (want) {
#ifdef TEENSYDUINO
        auxExpansion = (uint8_t *)extmem_malloc(want);
#else
        auxExpansion = (uint8_t *)malloc(want);
#endif
        if (!auxExpansion) {
          printf("RamWorks: could not allocate %u bytes on restore; falling back to stock aux\n",
                 (unsigned)want);
          newBanks = 1; // couldn't fit; fall back to stock aux
        }
      }
    }
    numAuxBanks = newBanks;

    if (bytes) {
      if (auxExpansion && numAuxBanks > 1 &&
          bytes == (uint32_t)(numAuxBanks - 1) * 0x10000) {
        if ((uint32_t)g_filemanager->read(fd, auxExpansion, bytes) != bytes) {
          goto err;
        }
      } else {
        // Configuration mismatch (or no buffer): consume the bytes so the
        // rest of the stream stays aligned.
        uint8_t tmp[256];
        uint32_t remaining = bytes;
        while (remaining) {
          uint32_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
          if ((uint32_t)g_filemanager->read(fd, tmp, chunk) != chunk) {
            goto err;
          }
          remaining -= chunk;
        }
      }
    }

    auxBank = (savedBank < numAuxBanks) ? savedBank : 0;
  }

  deserializeMagic(MMUMAGIC);

  // Reset readPages[] and writePages[] and the display
  resetDisplay();

  return true;

 err:
  return false;
}



void AppleMMU::Reset()
{
  resetRAM();
  resetDisplay(); // sets the switches properly
}

uint8_t AppleMMU::read(uint16_t address)
{
  uint8_t rv = readCore(address);
  if (g_debugger.watchCount) g_debugger.onAccess(address, rv, false);
  return rv;
}

uint8_t AppleMMU::readCore(uint16_t address)
{
  uint8_t rv = 0;
  if (handleNoSlotClock(address, &rv)) {
    return rv;
  }

  if (address >= 0xC000 &&
      address <= 0xC0FF) {
    uint8_t rv = readSwitches(address);
    logVideoState();  // capture any video-switch change this access made
    return rv;
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

  // Cards that intercept their slot ROM space get reads routed
  // directly rather than going through the preloaded ROM page.
  if (!intcxrom && address >= 0xC100 && address <= 0xC7FF) {
    uint8_t slotNum = (address >> 8) & 0x07;
    if (slots[slotNum] && slots[slotNum]->interceptsSlotRom()) {
      return slots[slotNum]->readSlotRom(address & 0xFF);
    }
  }

  // If we access CFFF, that unlatches slot ROM.
  if (address == 0xCFFF) {
    slotLatch = -1;
    updateMemoryPages();
  }

  // RamWorks: when a non-zero aux bank is selected, aux reads come from
  // the expansion buffer instead of the stock aux pages in g_ram.
  if (auxBank && auxExpansion && readPageIsAux[address >> 8]) {
    uint32_t ofs = address;
    if (bank2 && address >= 0xD000 && address <= 0xDFFF) {
      // The language-card second $D000 bank is stored in the otherwise
      // unused $C000-$CFFF region of each 64K bank image.
      ofs = address - 0x1000;
    }
    return auxExpansion[(uint32_t)(auxBank - 1) * 0x10000 + ofs];
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

// The debugger's view of memory. Resolves a (address, bank) pair to one
// of three places: a RamWorks expansion byte, a g_ram page, or nowhere
// (returns false: the $C0 page, or ROM space in a RAM bank that has no
// RAM there). A side-effect-free twin of the page tables read() and
// write() use, with the extra dimensions (which aux bank, which language
// card bank) that the CPU's current switches would otherwise pick.
bool AppleMMU::debugLocate(uint16_t address, const DebugBank &bank,
			   bool forWrite, uint8_t **ramworks, uint32_t *ramAddr)
{
  uint8_t hi = address >> 8;
  *ramworks = NULL;

  if (hi == 0xC0) return false;

  if (bank.kind == DBG_BANK_CPU) {
    const uint16_t *pages = forWrite ? writePages : readPages;
    const bool *isAux = forWrite ? writePageIsAux : readPageIsAux;
    if (auxBank && auxExpansion && isAux[hi]) {
      uint32_t ofs = address;
      if (bank2 && address >= 0xD000 && address <= 0xDFFF) ofs = address - 0x1000;
      *ramworks = &auxExpansion[(uint32_t)(auxBank - 1) * 0x10000 + ofs];
      return true;
    }
    *ramAddr = ((uint32_t)pages[hi] << 8) | (address & 0xFF);
    return true;
  }

  if (bank.kind == DBG_BANK_ROM) {
    if (hi < 0xC1) return false;
    // The internal ROM: variant 1 for $C100-$C7FF except $C300, which is
    // variant 0; variant 0 from $C800 up. See resetRAM().
    uint8_t variant = 0;
    if (hi >= 0xC1 && hi <= 0xC7 && hi != 0xC3) variant = 1;
    *ramAddr = ((uint32_t)_pageNumberForRam(hi, variant) << 8) | (address & 0xFF);
    return true;
  }

  // MAIN or AUX RAM.
  bool aux = (bank.kind == DBG_BANK_AUX);
  bool lc2 = (bank.lcBank == 2);
  if (hi >= 0xC1 && hi <= 0xCF) return false;   // no RAM behind the I/O ROM space

  if (aux && bank.auxBank) {
    if (!auxExpansion || bank.auxBank >= numAuxBanks) return false;
    uint32_t ofs = address;
    if (lc2 && address >= 0xD000 && address <= 0xDFFF) ofs = address - 0x1000;
    *ramworks = &auxExpansion[(uint32_t)(bank.auxBank - 1) * 0x10000 + ofs];
    return true;
  }

  uint8_t variant;
  if (hi < 0xC0)       variant = aux ? 1 : 0;
  else if (hi <= 0xDF) variant = aux ? (lc2 ? 4 : 3) : (lc2 ? 2 : 1);
  else                 variant = aux ? 2 : 1;
  *ramAddr = ((uint32_t)_pageNumberForRam(hi, variant) << 8) | (address & 0xFF);
  return true;
}

uint8_t AppleMMU::peek(uint16_t address, const DebugBank &bank)
{
  if ((address >> 8) == 0xC0) {
    uint8_t lo = address & 0xFF;
    uint32_t c0 = (uint32_t)readPages[0xC0] << 8;
    if (lo <= 0x0F) return g_ram.readByte(c0 | 0x10);   // keyboard latch
    switch (lo) {
    case 0x10: return anyKeyDown ? 0x80 : 0x00;
    case 0x11: return bank2 ? 0x80 : 0x00;
    case 0x12: return readbsr ? 0x80 : 0x00;
    case 0x13: return auxRamRead ? 0x80 : 0x00;
    case 0x14: return auxRamWrite ? 0x80 : 0x00;
    case 0x15: return intcxrom ? 0x80 : 0x00;
    case 0x16: return altzp ? 0x80 : 0x00;
    case 0x17: return slot3rom ? 0x80 : 0x00;
    case 0x18: return (switches & S_80STORE) ? 0x80 : 0x00;
    case 0x19: return (((g_cpu->cycles % 17030) / 65) < 192) ? 0x80 : 0x00;
    case 0x1A: return (switches & S_TEXT) ? 0x80 : 0x00;
    case 0x1B: return (switches & S_MIXED) ? 0x80 : 0x00;
    case 0x1C: return (switches & S_PAGE2) ? 0x80 : 0x00;
    case 0x1D: return (switches & S_HIRES) ? 0x80 : 0x00;
    case 0x1E: return (switches & S_ALTCH) ? 0x80 : 0x00;
    case 0x1F: return (switches & S_80COL) ? 0x80 : 0x00;
    case 0x61: case 0x62: case 0x63:
      return g_ram.readByte(c0 | lo);   // the Apple keys and shift are RAM in this model
    default:
      return 0;
    }
  }

  if (bank.kind == DBG_BANK_CPU && !intcxrom &&
      address >= 0xC100 && address <= 0xC7FF) {
    uint8_t slotNum = (address >> 8) & 0x07;
    if (slots[slotNum] && slots[slotNum]->interceptsSlotRom()) return 0;
  }

  uint8_t *rw; uint32_t ra = 0;
  if (!debugLocate(address, bank, false, &rw, &ra)) return 0;
  return rw ? *rw : g_ram.readByte(ra);
}

void AppleMMU::poke(uint16_t address, uint8_t v, const DebugBank &bank)
{
  if ((address >> 8) == 0xC0) return;
  if (bank.kind == DBG_BANK_CPU && !intcxrom &&
      address >= 0xC100 && address <= 0xC7FF) {
    uint8_t slotNum = (address >> 8) & 0x07;
    if (slots[slotNum] && slots[slotNum]->interceptsSlotRom()) return;
  }

  // The CPU's view honors the CPU's write rules: no writes into ROM. A
  // poke that means to patch ROM says so with DBG_BANK_ROM.
  if (bank.kind == DBG_BANK_CPU) {
    if (address >= 0xC100 && address <= 0xCFFF) return;
    if (address >= 0xD000 && !writebsr) return;
  }

  uint8_t *rw; uint32_t ra = 0;
  if (!debugLocate(address, bank, true, &rw, &ra)) return;
  if (rw) *rw = v; else g_ram.writeByte(ra, v);

  // A poke into a display page should show up on the screen.
  if (address < 0xC000) display->modeChange();
}

void AppleMMU::write(uint16_t address, uint8_t v)
{
  if (g_debugger.watchCount) g_debugger.onAccess(address, v, true);

  if (handleNoSlotClock(address, NULL)) {
    return;
  }

  if (address >= 0xC000 &&
      address <= 0xC0FF) {
    writeSwitches(address, v);
    logVideoState();  // capture any video-switch change this access made
    return;
  }

  // Cards that intercept their slot ROM space get writes routed.
  if (!intcxrom && address >= 0xC100 && address <= 0xC7FF) {
    uint8_t slotNum = (address >> 8) & 0x07;
    if (slots[slotNum] && slots[slotNum]->interceptsSlotRom()) {
      slots[slotNum]->writeSlotRom(address & 0xFF, v);
      return;
    }
  }

  // Don't allow writes to ROM
  // Hard ROM, I/O, slots, whatnot
  if (address >= 0xC100 && address <= 0xCFFF)
    return;
  // Bank-switched ROM/RAM areas
  if (address >= 0xD000 && address <= 0xFFFF && !writebsr) {
    return;
  }

  // RamWorks: aux writes to a non-zero bank go to the expansion buffer.
  // The video scanner only ever reads bank 0, so such writes are never
  // visible on screen and don't need to trigger a display update.
  if (auxBank && auxExpansion && writePageIsAux[address >> 8]) {
    uint32_t ofs = address;
    if (bank2 && address >= 0xD000 && address <= 0xDFFF) {
      ofs = address - 0x1000;
    }
    auxExpansion[(uint32_t)(auxBank - 1) * 0x10000 + ofs] = v;
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

// Record the current switches word with a cycle stamp, but only when it has
// actually changed since the last record. Called after every $C0xx access
// (see read()/write()), which is the one point all the video switches funnel
// through regardless of which handler (readSwitches / writeSwitches /
// handleMemorySwitches) did the toggling.
void AppleMMU::logVideoState()
{
  if (switches == lastLoggedSwitches)
    return;
  lastLoggedSwitches = switches;
  videoLog[videoLogHead].cyc = g_cpu->cycles;
  videoLog[videoLogHead].sw  = switches;
  videoLogHead = (videoLogHead + 1) % kVideoLogSize;
  if (videoLogCount < kVideoLogSize)
    videoLogCount++;
}

// What were the switches at (or most recently before) cycle 'cyc'? Walks the
// ring newest-to-oldest and returns the first entry at or before cyc. If the
// log has nothing that old (or is empty), fall back to the live switches:
// that is the pre-log behavior and is correct for a screen that has not
// changed mode within the window.
uint16_t AppleMMU::switchesAtCycle(int64_t cyc)
{
  int idx = videoLogHead;
  for (int n = 0; n < videoLogCount; n++) {
    idx = (idx == 0 ? kVideoLogSize : idx) - 1;
    if (videoLog[idx].cyc <= cyc)
      return videoLog[idx].sw;
  }
  return switches;
}

// THE FLOATING BUS: what the video scanner is fetching right now.
//
// The scanner and the 6502 take turns on the RAM, the scanner on one phase
// of the 1MHz clock and the CPU on the other. An address in $C0xx that
// drives nothing onto the data bus during the CPU's phase leaves the last
// thing the scanner put there, so reading such an address hands back the
// byte being displayed at that instant. Software uses it as a raster
// position sensor: spin on $C070 until a known byte shows up and you know
// exactly where the beam is, with no interrupt, no timer and no cycle
// counting. A demo that draws a graphical banner across the top of the
// screen above other graphics does exactly that, and against a constant it
// spins forever and never draws anything.
//
// The equations below are the scanner's own, from Sather, UNDERSTANDING
// THE APPLE IIe: the horizontal and vertical counter states (3-15, T3.2),
// the HIRES TIME term (5-7, P3), the SUM adder (5-9), and the address
// assembly (5-8, T5.1). They are spelled out bit by bit rather than folded
// into arithmetic because that is the form the book states them in, and
// because anyone who doubts one of these bits should be able to find it on
// the page.
//
// PHASE. Line = cycles/65 and frame = 17030 cycles here, which is the same
// mapping RDVBLBAR ($C019) and AppleDisplay::needsRedraw() use. All three
// have to agree: software that finds the raster with this and then flips a
// mode switch is relying on the renderer putting the switch where this
// said the beam was.
uint16_t AppleMMU::videoScannerAddress(int64_t cyc)
{
  const int kHClocks      =    65; // clocks per scan line, HBL included
  const int kScanLines    =   262; // scan lines per frame, VBL included
  const int kHClock0State =  0x18; // H[543210] = 011000 at clock 0
  const int kHPEClock     =    40; // clock at which HPE goes low
  const int kHPresetClock =    41; // clock at which the H state presets
  const int kVLine0State  = 0x100; // V[543210CBA] at line 0
  const int kVPresetLine  =   256; // line at which the V state presets

  int64_t nCycles = cyc % (int64_t)(kHClocks * kScanLines);
  if (nCycles < 0) nCycles += kHClocks * kScanLines; // a negative cycle count is not ours to judge

  // horizontal state. The 40 displayed bytes of a line are fetched in the
  // last 40 of its 65 clocks; the first 25 are HBL, where the scanner is
  // still fetching, just from addresses nobody displays.
  int nHClock = (int)((nCycles + kHPEClock) % kHClocks);
  int nHState = kHClock0State + nHClock;
  if (nHClock >= kHPresetClock)
    nHState -= 1; // correct for the preset: there are two 0 states

  int h_0 = (nHState >> 0) & 1;
  int h_1 = (nHState >> 1) & 1;
  int h_2 = (nHState >> 2) & 1;
  int h_3 = (nHState >> 3) & 1;
  int h_4 = (nHState >> 4) & 1;
  int h_5 = (nHState >> 5) & 1;

  // vertical state
  int nVLine = (int)(nCycles / kHClocks);
  int nVState = kVLine0State + nVLine;
  if (nVLine >= kVPresetLine)
    nVState -= kScanLines;

  int v_A = (nVState >> 0) & 1;
  int v_B = (nVState >> 1) & 1;
  int v_C = (nVState >> 2) & 1;
  int v_0 = (nVState >> 3) & 1;
  int v_1 = (nVState >> 4) & 1;
  int v_2 = (nVState >> 5) & 1;
  int v_3 = (nVState >> 6) & 1;
  int v_4 = (nVState >> 7) & 1;

  bool hires = (switches & S_HIRES) && !(switches & S_TEXT);

  // HIRES TIME (5-7, P3): in mixed mode the bottom four rows fetch out of
  // text memory even though HIRES is still on.
  if (hires && (switches & S_MIXED) && v_4 && v_2)
    hires = false;

  // the SUM adder (5-9)
  int addend0 = 0x0D;
  int addend1 =               (h_5 << 2) | (h_4 << 1) | (h_3 << 0);
  int addend2 = (v_4 << 3) | (v_3 << 2) | (v_4 << 1) | (v_3 << 0);
  int sum = (addend0 + addend1 + addend2) & 0x0F;

  uint16_t a = 0;
  a |= h_0 << 0;
  a |= h_1 << 1;
  a |= h_2 << 2;
  a |= sum << 3;   // a3..a6
  a |= v_0 << 7;
  a |= v_1 << 8;
  a |= v_2 << 9;

  // 80STORE MUST BE OFF FOR PAGE2 TO SELECT A PAGE. With it on, PAGE2 is a
  // main/aux steering bit for the CPU's own $0400-$07FF accesses and the
  // scanner stays on page 1. Same rule the renderers follow: see
  // AppleDisplay::redrawHires() and Apple //e Technical Note #3.
  bool page2 = (switches & S_PAGE2) && !(switches & S_80STORE);

  if (hires) {
    a |= v_A << 10;
    a |= v_B << 11;
    a |= v_C << 12;
    a |= (page2 ? 0 : 1) << 13;  // $2000
    a |= (page2 ? 1 : 0) << 14;  // $4000
  } else {
    a |= (page2 ? 0 : 1) << 10;  // $0400
    a |= (page2 ? 1 : 0) << 11;  // $0800
  }

  return a;
}

// The byte itself. readDirect() bypasses the switches and the soft-switch
// dispatch, so this cannot recurse back into readSwitches().
//
// MAIN MEMORY, always. In 80-column modes the real scanner fetches aux and
// main in the same clock and it is the main byte that is left on the bus
// for the CPU, so this is right there too; what it does not model is the
// IIgs, which is not this machine.
uint8_t AppleMMU::floatingBus()
{
  return readDirect(videoScannerAddress(g_cpu->cycles), 0);
}

uint8_t appleFloatingBus()
{
  if (!g_theAppleMMU || !g_cpu)
    return 0;
  return g_theAppleMMU->floatingBus();
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
	  return _FLOATINGBUS;
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

    return _FLOATINGBUS;

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
  case 0xC019: // RDVBLBAR -- bit 7 tracks the vertical-blanking signal
    // FRAME-ALIGNED, not a free-running approximation. NTSC //e video is
    // 65 cycles per scanline and 262 scanlines per frame (17030 cycles);
    // lines 0..191 are the visible raster and 192..261 are vertical
    // blanking (70 lines = 4550 cycles, which is the "4550 of 17030" the
    // hardware spec gives). Deriving the phase from the same cycle counter
    // the renderer maps to scanlines lets a demo poll this to land a mode
    // switch on a known line. Polarity matches the //e: bit 7 is 1 during
    // the active display and 0 during vertical blanking (RDVBLBAR). The
    // low seven bits are the floating bus on real hardware.
    {
      uint16_t line = (uint16_t)((g_cpu->cycles % 17030) / 65);
      // ...and the low seven bits really are the floating bus, now that
      // there is one. Software that polls this looks at bit 7 only.
      return ((line < 192) ? 0x80 : 0x00) | (floatingBus() & 0x7F);
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


  case 0xC030: // SPEAKER ($C030-$C03F all toggle the speaker)
  case 0xC031:
  case 0xC032:
  case 0xC033:
  case 0xC034:
  case 0xC035:
  case 0xC036:
  case 0xC037:
  case 0xC038:
  case 0xC039:
  case 0xC03A:
  case 0xC03B:
  case 0xC03C:
  case 0xC03D:
  case 0xC03E:
  case 0xC03F:
    g_speaker->toggle(g_cpu->cycles);
#ifndef SUPPRESSREALTIME
    g_cpu->realtime(); // cause the CPU to stop processing its outer
		       // loop b/c the speaker might need attention
		       // immediately
#endif
    return _FLOATINGBUS;

  case 0xC050: // CLRTEXT
    if (switches & S_TEXT) {
      switches &= ~S_TEXT;
      resetDisplay();
    }
    return _FLOATINGBUS;
  case 0xC051: // SETTEXT
    if (!(switches & S_TEXT)) {
      switches |= S_TEXT;
      resetDisplay();
    }
    return _FLOATINGBUS;
  case 0xC052: // CLRMIXED
    if (switches & S_MIXED) {
      switches &= ~S_MIXED;
      resetDisplay();
    }
    return _FLOATINGBUS;
  case 0xC053: // SETMIXED
    if (!(switches & S_MIXED)) {
      switches |= S_MIXED;
      resetDisplay();
    }
    return _FLOATINGBUS;

  case 0xC054: // PAGE1
    if (switches & S_PAGE2) {
      switches &= ~S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return _FLOATINGBUS;

  case 0xC055: // PAGE2
    if (!(switches & S_PAGE2)) {
      switches |= S_PAGE2;
      if (!(switches & S_80COL)) {
	resetDisplay();
      } else {
	updateMemoryPages();
      }
    }
    return _FLOATINGBUS;

  case 0xC056: // CLRHIRES
    if (switches & S_HIRES) {
      switches &= ~S_HIRES;
      resetDisplay();
    }
    return _FLOATINGBUS;
  case 0xC057: // SETHIRES
    if (!(switches & S_HIRES)) {
      switches |= S_HIRES;
      resetDisplay();
    }
    return _FLOATINGBUS;

  case 0xC05E: // DHIRES ON
    if (!(switches & S_DHIRES)) {
      switches |= S_DHIRES;
      resetDisplay();
    }
    return _FLOATINGBUS;

  case 0xC05F: // DHIRES OFF
    if (switches & S_DHIRES) {
      switches &= ~S_DHIRES;
      resetDisplay();
    }
    return _FLOATINGBUS;

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
    return _FLOATINGBUS;
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

  case 0xC030: // SPEAKER ($C030-$C03F all toggle the speaker)
  case 0xC031:
  case 0xC032:
  case 0xC033:
  case 0xC034:
  case 0xC035:
  case 0xC036:
  case 0xC037:
  case 0xC038:
  case 0xC039:
  case 0xC03A:
  case 0xC03B:
  case 0xC03C:
  case 0xC03D:
  case 0xC03E:
  case 0xC03F:
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

  case 0xC073: // RamWorks aux bank select
    if (numAuxBanks > 1) {
      // The written value is the bank number; out-of-range values wrap,
      // which is exactly what card-sizing routines rely on to count banks.
      auxBank = v % numAuxBanks;
    }
    return;

#ifndef TEENSYDUINO
  case 0xC074: // cycle beacon: report the cumulative cycle count on stderr,
	       // stamped with the written byte as a marker id. Unused on real
	       // hardware, so instrumented programs are safe everywhere.
    if (g_cycleBeacon) {
      fprintf(stderr, "[cycle-beacon %u] cycles=%lld  t1x=%.3fs\n",
	      v, (long long)g_cpu->cycles,
	      (double)g_cpu->cycles / 1023000.0);
    }
    break; // fall out of the switch so the write still shadows to RAM
#endif

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

// Deliver a keypress from scripted/automation input. Unlike keyboardInput(),
// this deliberately does not latch anyKeyDown: a queued keystroke is a
// momentary tap, so the any-key-down flag ($C010 bit 7 / AKD) must not stick
// on after the type-ahead queue drains. The strobe is set just as a real press
// does; the running program clears it by reading $C010.
void AppleMMU::injectKeypress(uint8_t v)
{
  g_ram.writeByte((writePages[0xC0] << 8) | 0x10,
		  (v & 0x7F) | 0x80);
}

// True while a keypress is latched but not yet consumed (bit 7 of the keyboard
// data cell). Used to pace type-ahead injection so no character is dropped or
// doubled regardless of how fast the emulated program polls the keyboard.
bool AppleMMU::keyboardStrobePending()
{
  return (g_ram.readByte((readPages[0xC0] << 8) | 0x10) & 0x80) != 0;
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

  // RamWorks selects bank 0 on reset so the 80-column firmware finds the
  // standard aux memory after a reboot. (The bank count/buffer are config
  // and are left intact.)
  auxBank = 0;

  g_ram.init();
  for (uint16_t i=0; i<0x100; i++) {
    readPages[i] = writePages[i] = _pageNumberForRam(i, 0);
  }

  // Load system ROM
  for (uint16_t i=0x80; i<=0xFF; i++) {
    uint16_t page0 = _pageNumberForRam(i, 0);
    uint16_t page1 = _pageNumberForRam(i, 1);

    for (uint16_t k=0; k<0x100; k++) {
      uint16_t idx = ((i-0x80) << 8) | k;
#ifdef TEENSYDUINO
      uint8_t v = pgm_read_byte(&romData[idx]);
#else
      uint8_t v = romData[idx];
#endif

      // The space from 0xc1 through 0xcf is ROM image territory. We
      // load the C3 ROM in to page 0, but not page 1; and then we
      // load c800.CFFF to both main ROM (page 0) and the C3 aux ROM
      // (page 1) to convince the VM that we've got 128k of RAM and an
      // 80-column card.

      if (i >= 0xc1 && i <= 0xcf) {
	if (i == 0xc3) {
	  // C300..C3FF => built-in ROM
	  g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
	}
	else if (i >= 0xc8) {
	  // C800..CFFF => built-in ROM and slot 3 extended ROM
	  g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
	  g_ram.writeByte((page1 << 8) | (k & 0xFF), v);
	}
	else {
	  // C000..C2FF and C400..c7FF are main ROM
	  g_ram.writeByte((page1 << 8) | (k & 0xFF), v);
	}
      } else {
	// Everything else goes in page 0.
	g_ram.writeByte((page0 << 8) | (k & 0xFF), v);
      }
    }
  }

  // have each slot load its ROM
  for (uint8_t slotnum = 1; slotnum <= 7; slotnum++) {
    uint16_t page0 = _slotRomPageForSlot(slotnum);
    if (slots[slotnum]) {
      // Load the primary ROM for this peripheral (0xCsXX..0xCsFF). An I/O-only
      // card supplies no ROM, so leave its slot's ROM space untouched.
      if (slots[slotnum]->hasRom()) {
	uint8_t tmpBuf[256];
	memset(tmpBuf, 0, sizeof(tmpBuf));
	slots[slotnum]->loadROM(tmpBuf);
	for (int i=0; i<256; i++) {
	  g_ram.writeByte( (page0 << 8) + i, tmpBuf[i] );
	}
      }

      // See if there's an extended 2k ROM for this peripheral (0xC800..0xCFFF)
      if (slots[slotnum]->hasExtendedRom()) {
	for (int j=0; j<8; j++) {
	  // Load each of the 256 byte chunks separately to its own VMRam page
	  uint16_t slotPage = 0;
	  if (slotnum == 4) {
	    slotPage = _pageNumberForRam(0xC8 + j, 2);
	  } else {
#ifndef TEENSYDUINO
	    fprintf(stderr, "ERROR: unsupported extended ROM peripheral in slot %d\n", slotnum);
	    exit(1);
#endif
	  }
	  if (slotPage) {
	    uint8_t *p = g_ram.memPtr(slotPage << 8);
	    slots[slotnum]->loadExtendedRom(p, j * 256);
	  }
	}
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
  if (slots[slotnum] && slots[slotnum]->hasRom()) {
    uint16_t page0 = _slotRomPageForSlot(slotnum);
    uint8_t tmpBuf[256];
    memset(tmpBuf, 0, sizeof(tmpBuf));
    slots[slotnum]->loadROM(tmpBuf);
    for (int i=0; i<256; i++) {
      g_ram.writeByte( (page0 << 8) + i, tmpBuf[i] );
    }
  }
}

void AppleMMU::clearSlotRom(int8_t slotnum)
{
  if (slotnum >= 1 && slotnum <= 7) {
    uint16_t page0 = _slotRomPageForSlot(slotnum);
    for (int i = 0; i < 256; i++) {
      g_ram.writeByte((page0 << 8) + i, 0);
    }
  }
}

void AppleMMU::setRamworksSize(uint8_t megabytes)
{
  // Map the requested total aux size to a 64K bank count. Bank 0 is the
  // stock aux memory (held in g_ram); banks 1..N-1 live in auxExpansion.
  uint16_t banks;
  switch (megabytes) {
  case 1:  banks = 16;  break; // 1MB  = 16 * 64K
  case 3:  banks = 48;  break; // 3MB  = 48 * 64K
  case 16: banks = 256; break; // 16MB = 256 * 64K (full $C073 range)
  default: banks = 1;   break; // none / unrecognized -> stock 80-col card
  }

#ifdef TEENSYDUINO
  // Embedded builds have limited PSRAM; cap RamWorks at 1MB (16 banks).
  if (banks > 16) banks = 16;
#endif

  // Release any previous expansion buffer.
  if (auxExpansion) {
#ifdef TEENSYDUINO
    extmem_free(auxExpansion);
#else
    free(auxExpansion);
#endif
    auxExpansion = NULL;
  }

  numAuxBanks = banks;
  auxBank = 0;

  if (numAuxBanks > 1) {
    uint32_t bytes = (uint32_t)(numAuxBanks - 1) * 0x10000;
#ifdef TEENSYDUINO
    auxExpansion = (uint8_t *)extmem_malloc(bytes);
#else
    auxExpansion = (uint8_t *)malloc(bytes);
#endif
    if (auxExpansion) {
      memset(auxExpansion, 0, bytes);
    } else {
      // Allocation failed: fall back to stock aux so the machine still runs.
      numAuxBanks = 1;
    }
  }
}

void AppleMMU::updateMemoryPages()
{
  // Default: no page is aux. The blocks below mark the ones that are, so
  // RamWorks banking in read()/write() knows which accesses to redirect.
  for (uint16_t i = 0; i < 0x100; i++) {
    readPageIsAux[i] = writePageIsAux[i] = false;
  }

  for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
    readPages[idx] = _pageNumberForRam(idx, auxRamRead ? 1 : 0);
    readPageIsAux[idx] = auxRamRead;
  }

  for (uint8_t idx = 0x02; idx < 0xc0; idx++) {
    writePages[idx] = _pageNumberForRam(idx, auxRamWrite ? 1 : 0);
    writePageIsAux[idx] = auxRamWrite;
  }

  if (switches & S_80STORE) {
    // Per UTA2E: when 80STORE is on, text page 1 ($0400-$07FF) always
    // follows PAGE2 (overriding RAMRD/RAMWRT). HGR page 1 ($2000-$3FFF)
    // only follows PAGE2 when HIRES is ALSO on; otherwise $2000-$3FFF
    // continues to follow RAMRD/RAMWRT normally (set above).
    //
    // Wings of Fury depends on this: it turns on 80STORE + PAGE1 with
    // HIRES off, then uses RAMWRT/RAMRD to load its demo code into AUX
    // $2000+. The old code forced $2000-$3FFF to MAIN whenever 80STORE
    // was on, which corrupted AUX access and crashed the game on
    // JMP $2000.
    uint8_t textBank = (switches & S_PAGE2) ? 1 : 0;
    for (uint8_t idx = 0x04; idx < 0x08; idx++) {
      readPages[idx] = writePages[idx] = _pageNumberForRam(idx, textBank);
      readPageIsAux[idx] = writePageIsAux[idx] = (textBank == 1);
    }
    if (switches & S_HIRES) {
      uint8_t hgrBank = (switches & S_PAGE2) ? 1 : 0;
      for (uint8_t idx = 0x20; idx < 0x40; idx++) {
        readPages[idx] = writePages[idx] = _pageNumberForRam(idx, hgrBank);
        readPageIsAux[idx] = writePageIsAux[idx] = (hgrBank == 1);
      }
    }
    // else: $2000-$3FFF already set by the RAMRD/RAMWRT block above.
  }

  if (intcxrom) {
    for (uint8_t idx = 0xc1; idx < 0xd0; idx++) {
      readPages[idx] = _pageNumberForRam(idx, 1);
    }
    // $C300 is special: its internal firmware lives in variant 0 (variant 1
    // holds an external slot-3 card's ROM), the reverse of the other slots.
    // With INTCXROM on the //e shows the internal ROM at $C300, so $C3 must
    // map to variant 0. Otherwise the IRQ/BRK handler entry at $C3FA -- which
    // runs after STA $C007 forces INTCXROM on -- reads the empty slot-3 page
    // ($00 = BRK) and spins in an interrupt loop that overflows the stack.
    readPages[0xc3] = _pageNumberForRam(0xc3, 0);
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
    // FIXME: this is a hacky mess. Slot 3 (the 80-col card) is
    // supported, as page "1"; and Slot 4 (the mouse card) is
    // supported as page "2".
    if (slotLatch == 3) {
      for (int i=0xc8; i <= 0xcf; i++) {
	readPages[i] = _pageNumberForRam(i, 1);
      }
    } else if (slotLatch == 4) {
      for (int i=0xc8; i <= 0xcf; i++) {
	readPages[i] = _pageNumberForRam(i, 2);
      }
    }
  }

  // set zero-page & stack pages based on altzp flag
  if (altzp) {
    for (uint8_t idx = 0x00; idx < 0x02; idx++) {
      readPages[idx] = writePages[idx] = _pageNumberForRam(idx, 1);
      readPageIsAux[idx] = writePageIsAux[idx] = true;
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

  // High RAM ($D000-$FFFF) is aux exactly when ALTZP selects the aux
  // language card; ROM reads (readbsr false) are never aux.
  for (uint16_t idx = 0xd0; idx < 0x100; idx++) {
    readPageIsAux[idx] = (readbsr && altzp);
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

  for (uint16_t idx = 0xd0; idx < 0x100; idx++) {
    writePageIsAux[idx] = (writebsr && altzp);
  }
}

void AppleMMU::setAppleKey(int8_t which, bool isDown)
{
  assert(which <= 1);
  g_ram.writeByte((writePages[0xC0] << 8) | (0x61 + which), isDown ? 0x80 : 0x00);
}
