// test-floatingbus: the //e's video scanner address, and the floating bus
// that exposes it.
//
// WHY THIS EXISTS. The scanner and the 6502 share the RAM on opposite
// phases of the clock. An address in $C0xx that drives nothing onto the
// data bus during the CPU's phase leaves whatever the scanner last put
// there, so reading one hands back the byte being displayed at that
// instant. Software uses it as a raster position sensor: spin on $C070
// until a known byte appears and you know exactly where the beam is,
// with no interrupt, no timer and no cycle counting.
//
// aiie returned a constant 0 here for its whole life, and a program that
// spins waiting for a specific byte then spins forever. That is not a
// cosmetic difference: it is a hang, and the code that would have drawn
// the screen never runs.
//
// WHAT IS ASSERTED. The address equations come from Sather, UNDERSTANDING
// THE APPLE IIe (3-15 T3.2, 5-7 P3, 5-9, 5-8 T5.1), so the bench checks
// them against what that hardware is documented to do: where the 40
// displayed bytes of a line live, that HBL still fetches (from addresses
// nobody displays), which page each mode scans, that 80STORE pins the
// scanner to page 1, and that mixed mode fetches text memory for the
// bottom four rows even with HIRES on.
//
// AND IT TESTS THE POSITIVE AT THE TOP: the last case is the demo's own
// technique, a bounded spin waiting for a known byte to appear on the
// bus. It has to actually terminate, at the right raster position. That
// case fails against the old constant-0 behavior, which is the whole
// point of it.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "cpu.h"
#include "applemmu.h"
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

// soft switches, driven the way a program drives them
#define GRON      0xC050
#define TEXTON    0xC051
#define MIXOFF    0xC052
#define MIXON     0xC053
#define PAGE2OFF  0xC054
#define PAGE2ON   0xC055
#define HIRESOFF  0xC056
#define HIRESON   0xC057
#define COL80OFF  0xC00C
#define ST80OFF   0xC000
#define ST80ON    0xC001
#define AN3ON     0xC05F   // DHIRES off
#define PTRIG     0xC070   // the address the demos actually read

static const int64_t kLine = 65;
static const int64_t kFrame = 17030;
static const int kHBL = 25;    // clocks of horizontal blanking per line

static void ok(const char *fmt, ...)
{
  va_list ap; va_start(ap, fmt);
  printf("  ok: "); vprintf(fmt, ap); printf("\n"); va_end(ap);
}

static void fail(const char *fmt, ...)
{
  va_list ap; va_start(ap, fmt);
  printf("  FAIL: "); vprintf(fmt, ap); printf("\n"); va_end(ap);
  failures++;
}

static void expectAddr(const char *what, int64_t cyc, uint16_t want)
{
  uint16_t got = mmu->videoScannerAddress(cyc);
  if (got == want) ok("%s -> $%04X", what, got);
  else fail("%s: scanner address is $%04X, expected $%04X", what, got, want);
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
  mmu->Reset();

  printf("video scanner address / floating bus\n");

  // a plain //e in 40-column text, page 1
  mmu->write(COL80OFF, 0);
  mmu->write(ST80OFF, 0);
  mmu->write(PAGE2OFF, 0);
  mmu->read(AN3ON);
  mmu->write(TEXTON, 0);
  mmu->write(MIXOFF, 0);
  mmu->write(HIRESOFF, 0);

  // ---- 1. THE 40 DISPLAYED BYTES OF A LINE. A scan line is 65 clocks:
  // 25 of horizontal blanking and then the 40 bytes that are shown. So on
  // line 0 of a frame, clock 25 fetches $0400 and clock 64 fetches $0427.
  printf(" text page 1, line 0: the displayed run is $0400..$0427\n");
  expectAddr("line 0, clock 25 (first displayed)", kHBL, 0x0400);
  expectAddr("line 0, clock 26", kHBL + 1, 0x0401);
  expectAddr("line 0, clock 64 (last displayed)", 64, 0x0427);
  {
    int bad = 0;
    for (int c = 0; c < 40; c++) {
      uint16_t got = mmu->videoScannerAddress(kHBL + c);
      if (got != (uint16_t)(0x0400 + c)) bad++;
    }
    if (!bad) ok("all 40 displayed clocks of line 0 are consecutive");
    else fail("%d of 40 displayed clocks of line 0 are wrong", bad);
  }

  // ---- 2. THE TEXT ROWS ARE INTERLEAVED the way the //e interleaves
  // them: rows 0..7 at $0400 + row*$80, and row 8 restarts at $0428.
  // Character row r covers scan lines r*8 .. r*8+7.
  printf(" text row bases follow the //e interleave\n");
  {
    static const uint16_t rowbase[24] = {
      0x400,0x480,0x500,0x580,0x600,0x680,0x700,0x780,
      0x428,0x4A8,0x528,0x5A8,0x628,0x6A8,0x728,0x7A8,
      0x450,0x4D0,0x550,0x5D0,0x650,0x6D0,0x750,0x7D0 };
    int bad = 0; int firstBad = -1;
    for (int row = 0; row < 24; row++) {
      // every one of the 8 scan lines of a character row fetches the same
      // 40 bytes: that is what makes a character 8 pixels tall.
      for (int sub = 0; sub < 8; sub++) {
        int64_t cyc = (int64_t)(row*8 + sub) * kLine + kHBL;
        if (mmu->videoScannerAddress(cyc) != rowbase[row]) {
          if (firstBad < 0) firstBad = row;
          bad++;
        }
      }
    }
    if (!bad) ok("all 24 rows x 8 scan lines hit the right row base");
    else fail("%d row/subline combinations wrong (first bad row %d)", bad, firstBad);
  }

  // ---- 3. HBL STILL FETCHES. The scanner never stops; during the 25
  // blanking clocks it reads addresses that are simply not displayed.
  // What must be true is that they are NOT in the 40 displayed bytes of
  // the line, or the floating bus would be useless for finding the beam.
  printf(" horizontal blanking fetches outside the displayed run\n");
  {
    int inside = 0;
    for (int c = 0; c < kHBL; c++) {
      uint16_t a = mmu->videoScannerAddress(c);
      if (a >= 0x0400 && a <= 0x0427) inside++;
    }
    if (!inside) ok("none of the 25 HBL clocks of line 0 land in $0400..$0427");
    else fail("%d of 25 HBL clocks land inside the displayed run", inside);
  }

  // ---- 4. EACH MODE SCANS ITS OWN PAGE.
  printf(" mode and page selection\n");
  mmu->write(PAGE2ON, 0);
  expectAddr("text page 2", kHBL, 0x0800);
  mmu->write(PAGE2OFF, 0);

  mmu->write(GRON, 0);      // TEXT off
  mmu->write(HIRESON, 0);
  expectAddr("hires page 1", kHBL, 0x2000);
  mmu->write(PAGE2ON, 0);
  expectAddr("hires page 2", kHBL, 0x4000);

  // 80STORE MUST BE OFF FOR PAGE2 TO SELECT A PAGE (Apple //e Technical
  // Note #3): with it on, PAGE2 steers the CPU's own accesses between
  // main and aux and the scanner stays on page 1. Same rule the renderers
  // follow, and the scanner has to agree with them.
  mmu->write(ST80ON, 0);
  expectAddr("hires, PAGE2 on but 80STORE on", kHBL, 0x2000);
  mmu->write(ST80OFF, 0);
  mmu->write(PAGE2OFF, 0);

  // ---- 5. MIXED MODE FETCHES TEXT FOR THE BOTTOM FOUR ROWS even though
  // HIRES is still on: the HIRES TIME signal drops there (UTA2E 5-7, P3).
  // Rows 20..23 are scan lines 160..191.
  printf(" mixed mode: the bottom four rows fetch text memory\n");
  mmu->write(MIXON, 0);
  {
    uint16_t top = mmu->videoScannerAddress(100 * kLine + kHBL);
    uint16_t bot = mmu->videoScannerAddress(170 * kLine + kHBL);
    if (top >= 0x2000 && top <= 0x3FFF) ok("scan line 100 fetches hires ($%04X)", top);
    else fail("scan line 100 fetches $%04X, expected hires $2000..$3FFF", top);
    if (bot >= 0x0400 && bot <= 0x07FF) ok("scan line 170 fetches text ($%04X)", bot);
    else fail("scan line 170 fetches $%04X, expected text $0400..$07FF", bot);
  }
  mmu->write(MIXOFF, 0);

  // ---- 6. THE BUS CARRIES THE BYTE. Put a known value at a known place
  // and read it back off $C070 at the cycle the scanner is there.
  printf(" the byte on the bus is the byte in memory\n");
  mmu->write(HIRESOFF, 0);
  mmu->write(TEXTON, 0);
  {
    mmu->write(0x0400 + 7, 0x5A);
    cpu.cycles = kHBL + 7;
    uint8_t got = mmu->read(PTRIG);
    if (got == 0x5A) ok("$C070 at the cycle scanning $0407 returns $5A");
    else fail("$C070 returned $%02X, expected $5A", got);
  }

  // ---- 7. THE DEMO'S OWN TECHNIQUE, which is the case that matters:
  //
  //     loop: LDA $C070
  //           CMP #$AE
  //           BNE loop
  //
  // Spin until a known byte shows up on the bus. This must TERMINATE, and
  // it must terminate where that byte actually is on screen. Against the
  // old constant-0 floating bus it never terminates at all, which is
  // exactly the hang that kept a demo's scrolling banner from ever being
  // drawn.
  printf(" spin-until-byte: a raster sync that has to finish\n");
  {
    // clear the screen, then put the marker at one cell: row 5, column 11.
    for (uint16_t a = 0x0400; a <= 0x07FF; a++) mmu->write(a, 0x00);
    const uint16_t marker = 0x0680 + 11;   // row 5 base $0680
    mmu->write(marker, 0xAE);

    cpu.cycles = 0;
    const int64_t budget = kFrame * 2;     // two frames is plenty for one
    int64_t spun = 0;
    bool found = false;
    while (spun < budget) {
      if (mmu->read(PTRIG) == 0xAE) { found = true; break; }
      cpu.cycles += 4;                     // LDA abs + CMP + BNE, near enough
      spun += 4;
    }
    if (!found) {
      fail("the spin never saw $AE in %lld cycles: THIS IS THE HANG",
           (long long)budget);
    } else {
      // and it stopped where that byte really is: scan line 40..47
      // (character row 5) at the marker's column.
      int64_t line = (cpu.cycles % kFrame) / kLine;
      uint16_t at = mmu->videoScannerAddress(cpu.cycles);
      if (at == marker && line >= 40 && line <= 47) {
        ok("stopped after %lld cycles, scanning $%04X on line %lld",
           (long long)spun, at, (long long)line);
      } else {
        fail("stopped scanning $%04X on line %lld, expected $%04X on line 40..47",
             at, (long long)line, marker);
      }
    }
  }

  // ---- 8. AND IT IS NOT A CONSTANT. A bus that returned one value would
  // pass nothing above except by accident; say so directly.
  printf(" the bus is not a constant\n");
  {
    uint8_t seen[256]; memset(seen, 0, sizeof(seen));
    int distinct = 0;
    for (uint16_t a = 0x0400; a <= 0x07FF; a++) mmu->write(a, (uint8_t)(a & 0xFF));
    for (int64_t c = 0; c < kFrame; c += 7) {
      cpu.cycles = c;
      uint8_t v = mmu->read(PTRIG);
      if (!seen[v]) { seen[v] = 1; distinct++; }
    }
    if (distinct > 16) ok("%d distinct values across one frame", distinct);
    else fail("only %d distinct values across one frame", distinct);
  }

  if (failures) {
    printf("floating bus check FAILED (%d)\n", failures);
    return 1;
  }
  printf("floating bus check passed.\n");
  return 0;
}
