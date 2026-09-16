// test-monovideo: the two monochrome display types must be monochrome.
//
// WHY THIS EXISTS. A2DeskTop in "Mono" showed purple, blue and orange
// fringes on every edge. The double-hires renderer forced white only for
// "B&W" and let its 16-color values through for "Mono", and nothing
// downstream converted them. A monochrome monitor never saw the fringes,
// so neither may a monochrome setting: every pixel a graphics renderer
// emits on those two display types must be white or black, at the full
// 560-pixel width. This links the real AppleDisplay and AppleMMU against
// a display that records every pixel and checks exactly that, for double
// hires and single hires, in both monochrome modes. It also requires the
// same frame to carry color in RGB mode, so a detector that had stopped
// seeing color would be caught.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cpu.h"
#include "applemmu.h"
#include "appledisplay.h"
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

const char *AppleVM::DiskName(uint8_t) { return ""; }
const char *AppleVM::HDName(uint8_t) { return ""; }

#define PIXW 560
#define PIXH 192

static uint8_t framebuf[PIXH][PIXW];
static const uint8_t NOPIXEL = 0xEE;

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
  virtual void clrScr(uint8_t) { memset(framebuf, NOPIXEL, sizeof(framebuf)); }
  // double-wide: one Apple pixel is two of the 560
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x * 2 + 1 < PIXW && y < PIXH) { framebuf[y][x * 2] = color; framebuf[y][x * 2 + 1] = color; }
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
uint8_t g_displayType = m_perfectcolor;
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
#define PAGE2OFF  0xC054
#define PAGE2ON   0xC055
#define HIRESON   0xC057
#define AN3OFF    0xC05E   // double hires on
#define AN3ON     0xC05F
#define ST80OFF   0xC000
#define ST80ON    0xC001
#define COL80OFF  0xC00C
#define COL80ON   0xC00D

static void render()
{
  memset(framebuf, NOPIXEL, sizeof(framebuf));
  disp->modeChange();
  disp->needsRedraw();
  disp->didRedraw();
}

// A double-hires screen with plenty of isolated bits, which is what the
// color renderer paints in color: alternating $55 / $2A bytes in main
// and aux, so single bits and pairs both occur.
static void fillDoubleHires()
{
  mmu->write(ST80ON, 0);
  mmu->write(HIRESON, 0);
  for (uint16_t a = 0x2000; a < 0x4000; a++) {
    mmu->write(PAGE2OFF, 0);
    mmu->write(a, (a & 1) ? 0x55 : 0x2A);
    mmu->write(PAGE2ON, 0);
    mmu->write(a, (a & 1) ? 0x2A : 0x55);
  }
  mmu->write(PAGE2OFF, 0);
}

static void enterDoubleHires()
{
  mmu->write(GRON, 0);
  mmu->write(MIXOFF, 0);
  mmu->write(HIRESON, 0);
  mmu->write(ST80ON, 0);
  mmu->write(COL80ON, 0);
  mmu->read(AN3OFF);
  mmu->write(PAGE2OFF, 0);
}

static void enterHires()
{
  mmu->write(GRON, 0);
  mmu->write(MIXOFF, 0);
  mmu->write(HIRESON, 0);
  mmu->write(ST80OFF, 0);
  mmu->write(COL80OFF, 0);
  mmu->read(AN3ON);
  mmu->write(PAGE2OFF, 0);
}

// Count what the frame holds.
struct Tally { int white, black, other, unset; };

static Tally tally()
{
  Tally t = { 0, 0, 0, 0 };
  for (int y = 0; y < PIXH; y++)
    for (int x = 0; x < PIXW; x++) {
      uint8_t c = framebuf[y][x];
      if (c == NOPIXEL) t.unset++;
      else if (c == c_white) t.white++;
      else if (c == c_black) t.black++;
      else t.other++;
    }
  return t;
}

static void expectMono(const char *what)
{
  Tally t = tally();
  if (t.other) { failures++; printf("  FAIL: %s: %d colored pixels in a monochrome frame\n", what, t.other); }
  if (!t.white || !t.black) { failures++; printf("  FAIL: %s: frame is not a picture (%d white, %d black)\n", what, t.white, t.black); }
  if (t.unset) { failures++; printf("  FAIL: %s: %d pixels never painted\n", what, t.unset); }
}

static void expectColor(const char *what)
{
  Tally t = tally();
  if (!t.other) { failures++; printf("  FAIL: %s: no colored pixel in a color frame, so the detector proves nothing\n", what); }
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

  printf("monochrome display types\n");
  fillDoubleHires();

  printf(" double hires\n");
  enterDoubleHires();
  g_displayType = m_perfectcolor; disp->displayTypeChanged(); render();
  expectColor("double hires, RGB");
  g_displayType = m_monochrome; disp->displayTypeChanged(); render();
  expectMono("double hires, Mono");
  g_displayType = m_blackAndWhite; disp->displayTypeChanged(); render();
  expectMono("double hires, B&W");

  printf(" hires\n");
  enterHires();
  g_displayType = m_perfectcolor; disp->displayTypeChanged(); render();
  expectColor("hires, RGB");
  g_displayType = m_monochrome; disp->displayTypeChanged(); render();
  expectMono("hires, Mono");
  g_displayType = m_blackAndWhite; disp->displayTypeChanged(); render();
  expectMono("hires, B&W");

  if (failures) {
    printf("%d FAILURE%s\n", failures, failures == 1 ? "" : "S");
    return 1;
  }
  printf("monochrome check passed.\n");
  return 0;
}
