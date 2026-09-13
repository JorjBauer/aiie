// test-vsplit: mid-frame ("VBL-timed") video mode changes, rendered as
// horizontal bands.
//
// WHY THIS EXISTS. A real //e has no frame buffer: the scanner reads the
// soft switches as it goes, so a program that flips TEXT part way down
// the frame gets graphics above the flip and text below it. Aiie renders
// whole frames, so for its whole life it drew the frame in whatever mode
// the switches happened to be in when the 30Hz redraw ran: one mode for
// all 192 lines, and a split screen came out as whichever half won the
// race.
//
// apple/applemmu.cpp logs every video-switch transition with a cycle
// stamp; apple/appledisplay.cpp needsRedraw() replays that log across the
// 192 scanlines of the last complete frame and renders each run of lines
// that share a switch state as its own band. This bench drives that
// through the real MMU and the real AppleDisplay and asserts the bands
// land on the right rows.
//
// IT TESTS THE POSITIVE, and it is built so the obvious failures cannot
// pass it. The screen is set up so the two modes paint OPPOSITE colors
// from DIFFERENT memory: hires page 1 is filled solid white and text page
// 1 is filled with blanks (solid black). So:
//   - a renderer that ignored the log and drew one whole-frame mode gives
//     an all-white or an all-black screen, and both fail;
//   - a renderer that split at the wrong line fails on the rows between
//     the real boundary and its own;
//   - a renderer that painted nothing leaves the 0xEE fill, which is
//     neither white nor black, and fails.
// The uniform-frame cases at the end prove the same bench passes ordinary
// software, so a "split everything" renderer could not pass either.

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

// see the same note in test-video7.cpp: the linker wants this symbol and
// pulling applevm.cpp in would drag the whole disk stack along with it.
const char *AppleVM::DiskName(uint8_t) { return ""; }

// ---------------------------------------------------------------------
// THE RECORDING DISPLAY, 560 pixels wide because hires uses the full
// resolution. cacheDoubleWidePixel() is the 280-wide emitter (text and
// lores), so it paints two columns, exactly as SDLDisplay does in its
// 8875 path.

#define PIXW 560
#define PIXH 192
#define UNPAINTED 0xEE

static uint8_t framebuf[PIXH][PIXW];

class RecordingDisplay : public PhysicalDisplay {
public:
  virtual ~RecordingDisplay() {}
  virtual void blit() {}
  virtual void flush() {}
  virtual void drawUIImage(uint8_t) {}
  virtual void drawDriveActivity(bool, bool, bool) {}
  virtual void drawImageOfSizeAt(const uint8_t *, uint16_t, uint16_t,
                                 uint16_t, uint16_t) {}
  virtual void drawPixel(uint16_t, uint16_t, uint16_t) {}
  virtual void clrScr(uint8_t) { memset(framebuf, UNPAINTED, sizeof(framebuf)); }
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x < PIXW/2 && y < PIXH) {
      framebuf[y][x*2] = color;
      framebuf[y][x*2+1] = color;
    }
  }
  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x < PIXW && y < PIXH) framebuf[y][x] = color;
  }
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

// ---------------------------------------------------------------------
// The machine, driven through the MMU the way the 6502 would drive it.
// Nothing here writes the switch word directly: logVideoState() hangs off
// the $C0xx access, so a bench that reached around it would be testing
// nothing.

#define GRON      0xC050   // TEXT off
#define TEXTON    0xC051
#define MIXOFF    0xC052
#define PAGE2OFF  0xC054
#define HIRESON   0xC057
#define COL80OFF  0xC00C
#define ST80OFF   0xC000
#define AN3ON     0xC05F   // DHIRES off

// NTSC //e video: 65 cycles per scanline, 262 lines per frame. These are
// the same constants needsRedraw() and RDVBLBAR ($C019) use.
static const int64_t kLine = 65;
static const int64_t kFrame = 17030;

// Park the CPU at a named line of a named frame, so a switch written here
// is stamped at exactly that point in the raster.
static void atLine(int64_t frame, int line)
{
  cpu.cycles = frame * kFrame + (int64_t)line * kLine;
}

static void setGraphics()
{
  mmu->write(GRON, 0);
  mmu->write(HIRESON, 0);
  mmu->write(MIXOFF, 0);
}

static void setText()
{
  mmu->write(TEXTON, 0);
}

// Render the last COMPLETE frame before 'frame': that is what
// needsRedraw() picks, so put the clock early in the following frame.
static void renderFrame(int64_t frame)
{
  cpu.cycles = (frame + 1) * kFrame + 100;
  memset(framebuf, UNPAINTED, sizeof(framebuf));
  disp->needsRedraw();
  disp->didRedraw();
}

// ---------------------------------------------------------------------
// Assertions. A band is checked pixel for pixel over its whole width and
// height: "the split is at the right line" is only meaningful if the last
// row above it and the first row below it are both right.

static const char *colorName(uint8_t c)
{
  if (c == c_white) return "white";
  if (c == c_black) return "black";
  if (c == UNPAINTED) return "UNPAINTED";
  return "other";
}

static void expectBand(const char *what, int top, int bottom, uint8_t want)
{
  int wrong = 0;
  int firstRow = -1, firstCol = -1;
  uint8_t firstGot = 0;

  for (int y = top; y < bottom; y++) {
    for (int x = 0; x < PIXW; x++) {
      if (framebuf[y][x] != want) {
        if (!wrong) { firstRow = y; firstCol = x; firstGot = framebuf[y][x]; }
        wrong++;
      }
    }
  }

  if (!wrong) {
    printf("  ok: %s -> rows %d..%d all %s (%d pixels)\n",
           what, top, bottom-1, colorName(want), (bottom-top)*PIXW);
    return;
  }
  printf("  FAIL: %s: %d of %d pixels in rows %d..%d are not %s"
         " (first at row %d col %d, got %s)\n",
         what, wrong, (bottom-top)*PIXW, top, bottom-1, colorName(want),
         firstRow, firstCol, colorName(firstGot));
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

  printf("mid-frame video mode switching (scanline bands)\n");

  // A plain //e: 40 columns, no 80STORE, no double hires, page 1.
  mmu->write(COL80OFF, 0);
  mmu->write(ST80OFF, 0);
  mmu->write(PAGE2OFF, 0);
  mmu->read(AN3ON);

  // TWO PICTURES IN TWO DIFFERENT PLACES. Hires page 1 solid $7F is every
  // pixel on, which in black-and-white mode is solid white. Text page 1
  // full of $A0 (normal space) paints its background everywhere, which is
  // solid black. Neither can be mistaken for the other, and neither can be
  // mistaken for the 0xEE fill.
  for (uint16_t a = 0x2000; a <= 0x3FFF; a++) mmu->write(a, 0x7F);
  for (uint16_t a = 0x0400; a <= 0x07FF; a++) mmu->write(a, 0xA0);

  // ---- 1. GRAPHICS ON TOP, TEXT BELOW. The switch lands at line 96, the
  // middle of the visible raster and a character-row boundary.
  printf(" split at line 96: hires above, text below\n");
  atLine(100, 0);   setGraphics();
  atLine(100, 96);  setText();
  renderFrame(100);
  expectBand("hires band", 0, 96, c_white);
  expectBand("text band", 96, 192, c_black);

  // ---- 2. THE OTHER WAY ROUND, so nothing about the order is baked in.
  printf(" split at line 96: text above, hires below\n");
  atLine(102, 0);   setText();
  atLine(102, 96);  setGraphics();
  renderFrame(102);
  expectBand("text band", 0, 96, c_black);
  expectBand("hires band", 96, 192, c_white);

  // ---- 3. A SPLIT THAT IS NOT IN THE MIDDLE, at a line that is not a
  // character-row boundary either. Text rows are 8 pixels tall, so a text
  // band starting at 100 has its first character row cut in half: the
  // clip must be by PIXEL row, not by character row. Line 100 is inside
  // character row 12 (96..103).
  printf(" split at line 100 (mid-character-row)\n");
  atLine(104, 0);   setGraphics();
  atLine(104, 100); setText();
  renderFrame(104);
  expectBand("hires band", 0, 100, c_white);
  expectBand("text band", 100, 192, c_black);

  // ---- 4. THREE BANDS IN ONE FRAME. A demo that flips per scanline makes
  // many; two flips is enough to prove the walk is a loop and not an
  // if/else.
  printf(" three bands: 0..63 hires, 64..127 text, 128..191 hires\n");
  atLine(106, 0);   setGraphics();
  atLine(106, 64);  setText();
  atLine(106, 128); setGraphics();
  renderFrame(106);
  expectBand("top hires", 0, 64, c_white);
  expectBand("middle text", 64, 128, c_black);
  expectBand("bottom hires", 128, 192, c_white);

  // ---- 5. EVERY SCANLINE. The case the whole thing is for: 192 bands.
  // Even lines graphics, odd lines text, so the screen is a comb.
  printf(" alternating every scanline\n");
  for (int y = 0; y < 192; y++) {
    atLine(108, y);
    if (y & 1) setText(); else setGraphics();
  }
  renderFrame(108);
  {
    int wrong = 0, firstRow = -1;
    for (int y = 0; y < 192; y++) {
      uint8_t want = (y & 1) ? c_black : c_white;
      for (int x = 0; x < PIXW; x++) {
        if (framebuf[y][x] != want) {
          if (!wrong) firstRow = y;
          wrong++;
        }
      }
    }
    if (!wrong) {
      printf("  ok: 192 alternating scanlines, all %d pixels\n", PIXH*PIXW);
    } else {
      printf("  FAIL: %d pixels wrong in the comb (first bad row %d)\n",
             wrong, firstRow);
      failures++;
    }
  }

  // ---- 6. ORDINARY SOFTWARE IS UNTOUCHED. One mode for the whole frame
  // must still give one whole-frame render. Both ways round, so this is
  // not passing by accident on a stuck screen.
  printf(" uniform frames (the common case)\n");
  atLine(110, 0); setGraphics();
  renderFrame(110);
  expectBand("all hires", 0, 192, c_white);

  atLine(112, 0); setText();
  renderFrame(112);
  expectBand("all text", 0, 192, c_black);

  if (failures) {
    printf("vsplit check FAILED (%d)\n", failures);
    return 1;
  }
  printf("vsplit check passed.\n");
  return 0;
}
