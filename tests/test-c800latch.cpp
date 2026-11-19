// test-c800latch: who owns $C800-$CFFF.
//
// The //e has two kinds of latch for that space (Sather, UNDERSTANDING THE
// APPLE IIe, p. 5-28). INTC8ROM is the //e's own: any access to $C3xx with
// SLOTC3ROM off sets it, even with INTCXROM on, and it puts the internal
// ROM at $C800. Each card has its own latch too, set by an access to its
// $Cn00 page (only seen with INTCXROM off). Any access to $CFFF, read or
// write, clears INTC8ROM; the cards see it through I/O STROBE, which the
// MMU holds off while the internal ROM is at $C800 ("if INTCXROM OR
// INTC8ROM is configured for internal response, then access to
// $C800-$CFFF results in ROMEN1' low (active) and I/O STROBE' high
// (inactive)"), so a card misses a $CFFF made while the internal ROM is
// showing.
//
// aiie used to set the latch on reads only, never from $C3xx with INTCXROM
// on, kept a card latched through a later $C3xx access, and cleared every
// latch on any $CFFF.
//
// A fake card in slot 4 supplies a $C800 ROM full of $A5, so reading
// $C800 shows whose ROM is there.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include "cpu.h"
#include "applemmu.h"
#include "slot.h"
#include "appledisplay.h"
#include "font.h"
#include "physicalspeaker.h"
#include "physicalpaddles.h"
#include "physicaldisplay.h"
#include "physicalmouse.h"
#include "filemanager.h"
#include "vmram.h"
#include "vm.h"
#include "vmui.h"
#include "nix-filemanager.h"
#include "applevm.h"
#include "globals.h"

// see the same note in test-video7.cpp
const char *AppleVM::DiskName(uint8_t) { return ""; }
const char *AppleVM::HDName(uint8_t) { return ""; }

class NullDisplay : public PhysicalDisplay {
public:
  virtual ~NullDisplay() {}
  virtual void blit() {}
  virtual void flush() {}
  virtual void drawUIImage(uint8_t) {}
  virtual void drawDriveActivity(int8_t, int8_t, int8_t, int8_t) {}
  virtual void drawImageOfSizeAt(const uint8_t *, uint16_t, uint16_t,
                                 uint16_t, uint16_t) {}
  virtual void drawPixel(uint16_t, uint16_t, uint16_t) {}
  virtual void clrScr(uint8_t) {}
  virtual void cacheDoubleWidePixel(uint16_t, uint16_t, uint8_t) {}
  virtual void cachePixel(uint16_t, uint16_t, uint8_t) {}
};

class NullSpeaker : public PhysicalSpeaker {
public:
  virtual ~NullSpeaker() {}
  virtual void begin() {}
  virtual void reset() {}
  virtual void toggle(int64_t) {}
  virtual void maintainSpeaker(int64_t, uint64_t) {}
  virtual void beginMixing() {}
  virtual void mixOutput(uint8_t) {}
};

class NullPaddles : public PhysicalPaddles {
public:
  virtual ~NullPaddles() {}
  virtual void startReading() {}
  virtual uint8_t paddle0() { return 0xFF; }
  virtual uint8_t paddle1() { return 0xFF; }
};

class NullUI : public VMui {
public:
  virtual void drawStaticUIElement(uint8_t) {}
  virtual void drawOnOffUIElement(uint8_t, bool) {}
  virtual void drawPercentageUIElement(uint8_t, uint8_t) {}
  virtual void blit() {}
};

FileManager *g_filemanager = NULL;
Cpu *g_cpu = NULL;
VMui *g_ui = NULL;
VMRam g_ram;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalMouse *g_mouse = NULL;
PhysicalDisplay *g_display = NULL;
VM *g_vm = NULL;
uint8_t g_ramworksSize = 0;
bool g_cycleBeacon = false;
uint8_t g_slotDiskII = 0;
uint8_t g_slotHD32 = 0;
uint8_t g_displayType = m_blackAndWhite;
uint8_t g_luminanceCutoff = 0;
bool g_video7 = false;

static Cpu cpu;
static AppleMMU *mmu;
static AppleDisplay *disp;

static int failures = 0;

static void fail(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  printf("  FAIL: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  failures++;
}

#define MARK 0xA5

class FakeCard : public Slot {
public:
  virtual ~FakeCard() {}
  virtual bool Serialize(int8_t) { return true; }
  virtual bool Deserialize(int8_t) { return true; }
  virtual void Reset() {}
  virtual uint8_t readSwitches(uint8_t) { return 0; }
  virtual void writeSwitches(uint8_t, uint8_t) {}
  virtual void loadROM(uint8_t *p) { memset(p, 0x5A, 256); }
  virtual bool hasExtendedRom() { return true; }
  virtual void loadExtendedRom(uint8_t *p, uint16_t) { memset(p, MARK, 256); }
};

static uint8_t internalC800;

static void expect(const char *what, bool wantCard)
{
  uint8_t got = mmu->read(0xC800);
  uint8_t want = wantCard ? MARK : internalC800;
  if (got == want)
    printf("  ok: %s\n", what);
  else
    fail("%s: $C800 reads $%02X, expected $%02X (%s)", what, got, want,
         wantCard ? "the card's ROM" : "the internal ROM");
}

int main(int argc, char **argv)
{
  g_filemanager = new NixFileManager();
  g_speaker = new NullSpeaker();
  g_paddles = new NullPaddles();
  g_ui = new NullUI();
  g_display = new NullDisplay();
  g_cpu = &cpu;

  disp = new AppleDisplay();
  mmu = new AppleMMU(disp);
  disp->SetMMU(mmu);
  cpu.SetMMU(mmu);
  mmu->setSlot(4, new FakeCard());
  mmu->Reset();

  printf("$C800 latch\n");

  // The internal ROM's byte at $C800, with INTCXROM on
  mmu->write(0xC007, 0);
  internalC800 = mmu->read(0xC800);
  mmu->write(0xC006, 0);
  mmu->read(0xCFFF);
  if (internalC800 == MARK) {
    printf("  FAIL: the internal ROM has $%02X at $C800 too; pick another marker\n", MARK);
    return 1;
  }

  // A card latches its ROM from its own page
  mmu->read(0xC400);
  expect("slot 4 latched by a read of $C400", true);

  // $CFFF clears it, and so does a write
  mmu->read(0xCFFF);
  expect("a read of $CFFF clears the card's latch", false);
  mmu->write(0xC400, 0);
  expect("slot 4 latched by a write to $C400", true);
  mmu->write(0xCFFF, 0);
  expect("a write to $CFFF clears the card's latch", false);

  // INTC8ROM wins over a latched card
  mmu->read(0xC400);
  mmu->read(0xC300);
  expect("$C300 with SLOTC3ROM off takes $C800 from the card", false);
  mmu->read(0xC400);
  expect("and a card can't take it back before $CFFF", false);
  mmu->read(0xCFFF);
  mmu->read(0xC400);
  expect("until $CFFF", true);
  mmu->read(0xCFFF);

  // The cards only see $CFFF through I/O STROBE, which the MMU holds off
  // while the internal ROM is at $C800. A card latched before INTC8ROM
  // keeps its latch through that $CFFF, and shows up once INTC8ROM is gone.
  mmu->read(0xC400);
  mmu->read(0xC300);
  mmu->read(0xCFFF);
  expect("$CFFF under INTC8ROM clears INTC8ROM but not the card", true);
  mmu->read(0xCFFF);
  expect("and a second $CFFF, which the card sees, clears it", false);

  // Same under INTCXROM
  mmu->read(0xC400);
  mmu->write(0xC007, 0);
  mmu->read(0xCFFF);
  mmu->write(0xC006, 0);
  expect("$CFFF under INTCXROM doesn't reach the card", true);
  mmu->read(0xCFFF);

  // INTC8ROM is set with INTCXROM on, and stays set after INTCXROM goes off
  mmu->write(0xC007, 0);
  mmu->read(0xC300);
  mmu->write(0xC006, 0);
  mmu->read(0xC400);
  expect("INTC8ROM set under INTCXROM holds $C800 after INTCXROM goes off", false);
  // (The card latched under INTC8ROM, so it takes two $CFFFs to clear)
  mmu->read(0xCFFF);
  mmu->read(0xCFFF);

  // With INTCXROM on no card sees its page
  mmu->write(0xC007, 0);
  mmu->read(0xC400);
  mmu->write(0xC006, 0);
  expect("$C400 under INTCXROM latches no card", false);
  mmu->read(0xC400);
  expect("but it does once INTCXROM is off", true);

  // INTCXROM shows the internal ROM even over a latched card
  mmu->write(0xC007, 0);
  expect("INTCXROM on hides the latched card", false);
  mmu->write(0xC006, 0);
  expect("and INTCXROM off shows it again", true);
  mmu->read(0xCFFF);

  // SLOTC3ROM on: $C300 is the slot, so no INTC8ROM
  mmu->write(0xC00B, 0);
  mmu->read(0xC300);
  mmu->read(0xC400);
  expect("$C300 with SLOTC3ROM on doesn't set INTC8ROM", true);
  mmu->read(0xCFFF);
  mmu->write(0xC00A, 0);

  // A suspend file keeps both latches: INTC8ROM over a latched card
  {
    const char *fn = "tests/test-c800latch.state";
    mmu->read(0xC400);
    mmu->read(0xC300);
    int8_t fd = g_filemanager->openFile(fn);
    bool ok = fd != -1 && mmu->Serialize(fd);
    if (fd != -1) g_filemanager->closeFile(fd);
    mmu->read(0xCFFF);
    mmu->read(0xCFFF);
    fd = g_filemanager->openFile(fn);
    ok = ok && fd != -1 && mmu->Deserialize(fd);
    if (fd != -1) g_filemanager->closeFile(fd);
    unlink(fn);
    if (!ok) fail("couldn't save and restore the MMU");
    expect("after a restore, INTC8ROM holds $C800", false);
    mmu->read(0xCFFF);
    expect("and the restored card latch shows after one $CFFF", true);
    mmu->read(0xCFFF);
  }

  if (failures) {
    printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("all passed\n");
  return 0;
}
