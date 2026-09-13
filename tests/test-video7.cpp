// test-video7: the Video7 / A2DVI foreground-background color text mode.
//
// WHY THIS EXISTS. The mode lives entirely in the renderer, and the
// renderer's output is a window nobody can read from a script. So this
// links the REAL AppleDisplay and AppleMMU against a PhysicalDisplay
// that records every pixel it is handed, sets the machine up exactly as
// K2's fbtext does, and asserts the colors that come out cell by cell.
//
// It tests the positive in both directions: the mode must produce the
// requested foreground and background, AND every gate that is supposed
// to refuse the mode must actually refuse it. A test that only checked
// "the pixels are not white on black" would pass on a renderer that had
// broken in some other way.
//
// See video7.md for the mode itself.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

// PhysicalDisplay::redraw() paints the drive lamps, which reaches into
// AppleVM. Nothing here has a VM (g_vm is NULL and redraw() is never
// called), but the linker still wants the symbol; pulling in applevm.cpp
// would drag the disk stack, the mouse and the network in with it.
const char *AppleVM::DiskName(uint8_t) { return ""; }

// ---------------------------------------------------------------------
// THE RECORDING DISPLAY. AppleDisplay draws 40-column text through
// cacheDoubleWidePixel(), so that is the one method with anything in
// it; the rest exist because PhysicalDisplay is abstract.

#define PIXW 280
#define PIXH 192

static uint8_t framebuf[PIXH][PIXW];

class RecordingDisplay : public PhysicalDisplay {
public:
  virtual ~RecordingDisplay() {}
  virtual void blit() {}
  virtual void flush() {}
  virtual void drawUIImage(uint8_t) {}
  virtual void drawDriveActivity(int8_t, int8_t, int8_t, int8_t) {}
  virtual void drawImageOfSizeAt(const uint8_t *, uint16_t, uint16_t,
                                 uint16_t, uint16_t) {}
  virtual void drawPixel(uint16_t, uint16_t, uint16_t) {}
  virtual void clrScr(uint8_t) { memset(framebuf, 0xEE, sizeof(framebuf)); }
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x < PIXW && y < PIXH) framebuf[y][x] = color;
  }
  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x < PIXW && y < PIXH) framebuf[y][x] = color;
  }
};

// ---------------------------------------------------------------------
// stubs: the MMU wants these to exist, not to do anything

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
uint8_t g_displayType = m_perfectcolor;
uint8_t g_luminanceCutoff = 0;
bool g_video7 = false;

static Cpu cpu;
static AppleMMU *mmu;
static AppleDisplay *disp;

static int failures = 0;

// ---------------------------------------------------------------------
// THE MACHINE, driven through the MMU exactly as the 6502 would.
//
// Nothing here reaches around the MMU to poke the switch words directly:
// the point is that the soft-switch sequence a program actually issues
// produces the mode. A bench that set the switch bits itself would prove
// nothing about the switches. Reads and writes are used where K2 uses
// each ($C05E is a read in PLASMA's `^AN3OFF`, $C001 is a write).

#define TEXTON    0xC051
#define AN3OFF    0xC05E   // DHIRES on
#define AN3ON     0xC05F   // DHIRES off
#define COL80OFF  0xC00C
#define COL80ON   0xC00D
#define ST80ON    0xC001
#define ST80OFF   0xC000
#define COLORSTORE 0xC055  // PAGE2 on:  $0400-$07FF writes land in aux
#define TEXTSTORE  0xC054  // PAGE2 off: ...and in main

// K2's fbInit, minus the 80-column firmware call (which is about
// MouseText, not about color).
static void enterFbText()
{
  mmu->read(AN3OFF);
  mmu->read(TEXTON);
  mmu->write(COL80OFF, 0);
  mmu->write(ST80ON, 0);
}

// Put one character with one attribute in the cell at (col,row), the
// way fbPutc does: attribute into aux, then character into main.
static void poke(uint8_t row, uint8_t col, uint8_t ch, uint8_t attr)
{
  static const uint16_t rowbase[24] = {
    0x400,0x480,0x500,0x580,0x600,0x680,0x700,0x780,
    0x428,0x4A8,0x528,0x5A8,0x628,0x6A8,0x728,0x7A8,
    0x450,0x4D0,0x550,0x5D0,0x650,0x6D0,0x750,0x7D0 };
  uint16_t a = rowbase[row] + col;
  mmu->write(COLORSTORE, 0);
  mmu->write(a, attr);
  mmu->write(TEXTSTORE, 0);
  mmu->write(a, ch);
}

static void render()
{
  memset(framebuf, 0xEE, sizeof(framebuf));
  disp->modeChange();
  disp->needsRedraw();
  disp->didRedraw();
}

// CHECK ALL 56 PIXELS OF A CELL, not just which two colors appeared.
//
// The set-of-colors form of this check was written first and it was too
// weak: swapping the two attribute nibbles in the renderer still passed
// it, because {orange, white} is {white, orange}. So the expectation is
// built from the glyph itself. Every character this bench prints is 'A'
// -- $C1 normal and $01 inverse both resolve to ucase_glyphs[8] -- so
// the ink pattern is known here without asking the renderer for it, and
// which color lands on the ink is what gets asserted.
#define GLYPH_A (&ucase_glyphs[8])

static void expectCell(const char *what, uint8_t row, uint8_t col,
                       bool inverse, uint8_t wantFg, uint8_t wantBg)
{
  int wrong = 0, inked = 0;
  uint8_t sawWrong = 0, wantedThere = 0;

  for (uint8_t y = 0; y < 8; y++) {
    uint8_t bits = GLYPH_A[y];
    for (uint8_t x = 0; x < 7; x++) {
      bool on = (bits >> x) & 1;
      if (on) inked++;
      // ink takes the foreground, background the rest; inverse video
      // exchanges the two.
      uint8_t want = (on != inverse) ? wantFg : wantBg;
      uint8_t got = framebuf[row*8+y][col*7+x];
      if (got != want) {
	if (!wrong) { sawWrong = got; wantedThere = want; }
	wrong++;
      }
    }
  }

  if (!wrong && inked) {
    printf("  ok: %s -> fg %d on bg %d, all 56 pixels\n",
	   what, wantFg, wantBg);
    return;
  }
  printf("  FAIL: %s: %d of 56 pixels wrong (first: got %d, wanted %d);"
	 " expected fg %d on bg %d%s\n",
	 what, wrong, sawWrong, wantedThere, wantFg, wantBg,
	 inked ? "" : " -- GLYPH HAS NO INK, the bench is broken");
  failures++;
}

// A REFUSED MODE MEANS NOTHING ON SCREEN CAME OUT OF THE PALETTE.
//
// Checking one cell is not enough for the gates: with 80COL on it is the
// 80-column renderer that runs, and it draws different glyphs in
// different places, so a per-cell glyph comparison would be comparing
// against the wrong picture. What must be true whatever renderer ran is
// that the whole screen is white and black, the way a //e with no RGB
// card is.
static void expectMono(const char *what)
{
  int colored = 0;
  uint8_t first = 0;
  for (uint16_t y = 0; y < PIXH; y++) {
    for (uint16_t x = 0; x < PIXW; x++) {
      uint8_t v = framebuf[y][x];
      if (v != c_white && v != c_black) {
	if (!colored) first = v;
	colored++;
      }
    }
  }
  if (!colored) {
    printf("  ok: %s -> monochrome screen\n", what);
    return;
  }
  printf("  FAIL: %s: %d of %d pixels are colored (first is index %d)\n",
	 what, colored, PIXW*PIXH, first);
  failures++;
}

int main(int argc, char **argv)
{
  g_filemanager = new NixFileManager();
  g_speaker = new NullSpeaker();
  g_paddles = new NullPaddles();
  g_ui = new NullUI();
  g_display = new RecordingDisplay();
  g_cpu = &cpu;

  disp = new AppleDisplay();
  mmu = new AppleMMU(disp);
  disp->SetMMU(mmu);
  cpu.SetMMU(mmu);
  mmu->Reset();

  // 0x9F: orange on white. 0x2C: dark blue on green. Both are from the
  // ordinary lores palette, which is what the attribute nibbles index.
  const uint8_t ATTR1 = 0x9F, FG1 = c_orange, BG1 = c_white;
  const uint8_t ATTR2 = 0x2C, FG2 = c_darkblue, BG2 = c_green;

  printf("Video7 color text\n");

  enterFbText();
  poke(0, 0, 0xC1, ATTR1);   // 'A', normal (high bit set)
  poke(5, 7, 0xC1, ATTR2);
  poke(23, 39, 0xC1, ATTR1); // the last cell on the screen

  // 1. OFF BY DEFAULT. g_video7 is still false here, so the same screen
  //    must come out white on black: a machine with no card.
  render();
  printf(" gate: card absent (g_video7 false)\n");
  expectCell("cell 0,0", 0, 0, false, c_white, c_black);
  expectCell("cell 5,7", 5, 7, false, c_white, c_black);
  expectMono("whole screen");

  // 2. THE MODE ITSELF.
  g_video7 = true;
  render();
  printf(" mode: TEXT + 80STORE + DHIRES, 80COL off\n");
  expectCell("cell 0,0 (attr $9F)", 0, 0, false, FG1, BG1);
  expectCell("cell 5,7 (attr $2C)", 5, 7, false, FG2, BG2);
  expectCell("cell 23,39 (attr $9F)", 23, 39, false, FG1, BG1);

  // 3. A CELL NOBODY WROTE keeps whatever the attribute plane holds,
  //    which after reset is zero: black on black. Not a bug, and worth
  //    pinning: it is why K2 fills the whole plane before printing.
  render();
  {
    // whatever glyph $00 is, black ink on black paper is 56 black pixels
    int notBlack = 0;
    for (uint8_t y = 0; y < 8; y++)
      for (uint8_t x = 0; x < 7; x++)
	if (framebuf[1*8+y][1*7+x] != c_black) notBlack++;
    if (!notBlack) {
      printf("  ok: untouched cell is black on black\n");
    } else {
      printf("  FAIL: untouched cell has %d non-black pixels\n", notBlack);
      failures++;
    }
  }

  // 4. INVERSE SWAPS THE CELL'S OWN COLORS rather than swapping black
  //    and white: an inverse character in an orange-on-white cell is
  //    white on orange, not black on white.
  poke(2, 2, 0x01, ATTR1);   // $01 = inverse 'A'
  render();
  printf(" inverse video\n");
  expectCell("inverse cell 2,2", 2, 2, true, FG1, BG1);
  {
    // ...and specifically the other way round from the normal cell,
    // pixel for pixel. Both cells hold the same 'A' glyph with the same
    // attribute, one normal ($C1) and one inverse ($01), so every pixel
    // of the inverse cell must be the OTHER color from the same pixel of
    // the normal one. Comparing the whole cell rather than one sampled
    // pixel keeps the check from depending on where the glyph's ink is.
    int mismatched = 0, inked = 0;
    for (uint8_t y = 0; y < 8; y++) {
      for (uint8_t x = 0; x < 7; x++) {
	uint8_t norm = framebuf[0*8+y][0*7+x];
	uint8_t inv  = framebuf[2*8+y][2*7+x];
	if (norm == FG1) inked++;
	if (inv != (norm == FG1 ? BG1 : FG1)) mismatched++;
      }
    }
    if (!mismatched && inked) {
      printf("  ok: inverse exchanges the cell's fg and bg in all 56 pixels"
	     " (%d inked)\n", inked);
    } else {
      printf("  FAIL: %d of 56 pixels not exchanged (%d inked)\n",
             mismatched, inked);
      failures++;
    }
  }

  // 5. EVERY GATE REFUSES. Each of these is a real machine state that
  //    must NOT produce color, checked one at a time so a renderer that
  //    dropped a term from the condition cannot pass.
  printf(" gates that must refuse\n");

  // 80COL on: the mode is 40-column only. Note what this does and does
  // not prove. It proves 80-column text stays monochrome, which is the
  // behavior that matters. It does NOT prove the !80COL term inside
  // redraw40ColumnText(), because needsRedraw() already routes 80COL to
  // redraw80ColumnText() and that function never reaches the term; a
  // build with the term deleted passes this check.
  mmu->write(COL80ON, 0);
  render();
  expectMono("80COL on (80-column renderer)");
  mmu->write(COL80OFF, 0);

  mmu->read(AN3ON);               // DHIRES off: no signal to the card
  render();
  expectMono("DHIRES off");
  mmu->read(AN3OFF);

  mmu->write(ST80OFF, 0);         // 80STORE off: no aux plane to read
  render();
  expectMono("80STORE off");
  mmu->write(ST80ON, 0);

  g_displayType = m_monochrome;   // a green screen is still a green screen
  render();
  expectMono("mono display");
  g_displayType = m_blackAndWhite;
  render();
  expectMono("B&W display");
  g_displayType = m_ntsclike;
  render();
  expectCell("NTSC-like display", 0, 0, false, FG1, BG1);
  g_displayType = m_perfectcolor;

  g_video7 = false;               // the BIOS setting, last
  render();
  expectMono("Video7 setting off");
  g_video7 = true;

  // 6. AND IT STILL WORKS AFTERWARDS, so the sweep above left nothing
  //    latched.
  render();
  printf(" after the sweep\n");
  expectCell("cell 0,0", 0, 0, false, FG1, BG1);

  if (failures) {
    printf("video7 check FAILED (%d)\n", failures);
    return 1;
  }
  printf("video7 check passed.\n");
  return 0;
}
