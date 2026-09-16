// test-parallelrom: the parallel printer card's slot ROM
// (apple/parallelrom.s), entered by the real CPU, in several slots.
//
// WHY THIS EXISTS. PR#n hands every printed character to $Cn00 or $Cn05
// and expects it at the printer, on the screen while echo is on, with the
// card's own line endings and its Control-I commands honored. None of
// that is visible from a full run except as paper. This links the real
// Cpu, AppleMMU and ParallelCard, replaces the printer with a recorder,
// and requires the positive result for each path: the exact bytes the
// printer received, the exact screen cell the echo landed in, the
// settings each command must leave behind, and the registers coming back
// untouched.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "cpu.h"
#include "applemmu.h"
#include "appledisplay.h"
#include "parallelcard.h"
#include "physicalspeaker.h"
#include "physicalpaddles.h"
#include "physicaldisplay.h"
#include "physicalmouse.h"
#include "physicalprinter.h"
#include "filemanager.h"
#include "vmram.h"
#include "vm.h"
#include "vmui.h"
#include "nix-filemanager.h"
#include "applevm.h"
#include "globals.h"

const char *AppleVM::DiskName(uint8_t) { return ""; }
const char *AppleVM::HDName(uint8_t) { return ""; }

// ---------------------------------------------------------------------
// stubs

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

class NullPrinter : public PhysicalPrinter {
public:
  virtual void addLine(uint8_t *) {}
  virtual void update() {}
  virtual void moveDownPixels(uint8_t) {}
};

// The card with the real ROM loader, but every byte written to the data
// register is recorded instead of printed.
class RecordingCard : public ParallelCard {
public:
  uint8_t bytes[1024]; int n; int otherWrites;
  RecordingCard() { n = 0; otherWrites = 0; }
  virtual void writeSwitches(uint8_t s, uint8_t v) {
    if (s == 0) { if (n < 1024) bytes[n] = v; n++; }
    else otherWrites++;
  }
  void clear() { n = 0; otherWrites = 0; }
};

FileManager *g_filemanager = NULL;
Cpu *g_cpu = NULL;
VMui *g_ui = NULL;
VMRam g_ram;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalMouse *g_mouse = NULL;
PhysicalDisplay *g_display = NULL;
PhysicalPrinter *g_printer = NULL;
VM *g_vm = NULL;
uint8_t g_ramworksSize = 0;
bool g_cycleBeacon = false;
uint8_t g_slotDiskII = 0;
uint8_t g_slotHD32 = 0;
uint8_t g_slotParallel = 0;
uint8_t g_displayType = m_blackAndWhite;
uint8_t g_luminanceCutoff = 0;
bool g_video7 = false;

static Cpu cpu;
static AppleMMU *mmu;
static AppleDisplay *disp;
static RecordingCard *card;
static int failures = 0;
static const char *slotName;

static void fail(const char *what, const char *fmt, ...)
{
  failures++;
  printf("  FAIL (%s): %s: ", slotName, what);
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  printf("\n");
}

static void expectByte(const char *what, uint16_t addr, uint8_t want)
{
  uint8_t got = mmu->read(addr);
  if (got != want) fail(what, "$%04X is $%02X, expected $%02X", addr, got, want);
}

// ---------------------------------------------------------------------
// entering the ROM

static uint16_t romBase;
static const uint16_t RET = 0x0300;

static bool runFrom(uint16_t pc, int limit)
{
  mmu->write(0x01FF, (RET - 1) >> 8);
  mmu->write(0x01FE, (RET - 1) & 0xFF);
  cpu.sp = 0xFD;
  cpu.pc = pc;
  for (int i = 0; i < limit; i++) {
    if (cpu.pc == RET) return true;
    cpu.step();
  }
  return false;
}

// One character through the output hook, the way COUT calls it: A = the
// character with the high bit set. Registers must come back as they went.
static void print(uint8_t ch, bool first = false)
{
  cpu.a = ch | 0x80; cpu.x = 0x11; cpu.y = 0x22; cpu.flags = F_I;
  bool ok = runFrom(first ? romBase : romBase + 2, 2000);
  if (!ok) fail("output hook", "never returned for $%02X (PC=$%04X)", ch, cpu.pc);
  if (cpu.a != (ch | 0x80) || cpu.x != 0x11 || cpu.y != 0x22)
    fail("output hook", "A=$%02X X=$%02X Y=$%02X after $%02X, expected $%02X/$11/$22", cpu.a, cpu.x, cpu.y, ch, ch | 0x80);
  if (cpu.sp != 0xFF) fail("output hook", "stack pointer $%02X after $%02X", cpu.sp, ch);
}

static void prints(const char *s) { while (*s) print(*s++); }

// what the printer received
static void expectBytes(const char *what, const char *want)
{
  size_t len = strlen(want);
  bool same = ((size_t)card->n == len);
  for (size_t i = 0; same && i < len; i++) same = (card->bytes[i] == (uint8_t)want[i]);
  if (!same) {
    char got[256]; int p = 0;
    for (int i = 0; i < card->n && p < 240; i++) p += snprintf(got + p, sizeof(got) - p, "%02X ", card->bytes[i]);
    char exp[256]; p = 0;
    for (size_t i = 0; i < len && p < 240; i++) p += snprintf(exp + p, sizeof(exp) - p, "%02X ", (uint8_t)want[i]);
    fail(what, "printer got [%s], expected [%s]", got, exp);
  }
  if (card->otherWrites) fail(what, "%d writes to registers other than the data register", card->otherWrites);
  card->clear();
}

// the screen holes for this slot
static uint16_t hole(uint16_t base) { return base + g_slotParallel; }
#define LEN   hole(0x578)
#define NUM   hole(0x5F8)
#define COL   hole(0x678)
#define FLAGS hole(0x6F8)   // bit 7 echo, bit 6 line feed, bit 0 command pending
#define CMDCH hole(0x778)   // the command character

// The screen echo goes through the monitor's COUT1, which writes at
// (BASL),CH. Line 0, column 0 of text page 1 is $0400.
static void homeCursor()
{
  mmu->write(0x20, 0);       // WNDLFT
  mmu->write(0x21, 40);      // WNDWDTH: a zero-width window scrolls the echo away
  mmu->write(0x22, 0);       // WNDTOP
  mmu->write(0x23, 24);      // WNDBTM
  mmu->write(0x24, 0);       // CH
  mmu->write(0x25, 0);       // CV
  mmu->write(0x28, 0x00);    // BASL
  mmu->write(0x29, 0x04);    // BASH
  mmu->write(0x32, 0xFF);    // INVFLG: normal video
  for (int i = 0; i < 40; i++) mmu->write(0x400 + i, 0xA0);
}

static void placeCard(uint8_t slot)
{
  if (g_slotParallel) {
    mmu->setSlot(g_slotParallel, NULL);
    mmu->clearSlotRom(g_slotParallel);
  }
  g_slotParallel = slot;
  card = new RecordingCard();
  mmu->setSlot(slot, card);
  romBase = 0xC000 + (slot << 8);
}

// PR#n: CSW points at $Cn00 and the next character arrives there
static void prNum()
{
  mmu->write(0x36, 0x00);
  mmu->write(0x37, 0xC0 + g_slotParallel);
}

// ---------------------------------------------------------------------

static void runSuite()
{
  uint8_t slot = g_slotParallel;

  // ---- the slot bytes the loader filled in
  expectByte("slot byte X", romBase + 0x06, 0xA2);
  expectByte("slot byte X", romBase + 0x07, 0xC0 + slot);
  expectByte("slot byte Y", romBase + 0x08, 0xA0);
  expectByte("slot byte Y", romBase + 0x09, slot << 4);

  // ---- PR#n: the first character resets the settings, moves CSW past
  // the reset, reaches the printer, and is echoed to the screen
  prNum();
  homeCursor();
  card->clear();
  mmu->write(LEN, 0xEE); mmu->write(FLAGS, 0x01); mmu->write(COL, 0xEE);
  print('A', true);
  expectBytes("PR#n first character", "\xC1");
  expectByte("PR#n: CSW moved to $Cn02", 0x36, 0x02);
  expectByte("PR#n: command character", CMDCH, 0x09);
  expectByte("PR#n: CSW page", 0x37, 0xC0 + slot);
  expectByte("PR#n: width 40", LEN, 40);
  expectByte("PR#n: echo and line feed on", FLAGS, 0xC0);
  expectByte("PR#n: column 1", COL, 1);
  expectByte("PR#n: echoed to the screen", 0x0400, 0xC1);
  expectByte("PR#n: cursor advanced", 0x24, 1);

  // ---- a plain line: bytes through, CR gets the automatic LF
  prints("BC\r");
  expectBytes("a line", "\xC2\xC3\x8D\x8A");
  expectByte("column back to 0 after CR", COL, 0);
  expectByte("echo of the line", 0x0401, 0xC2);
  expectByte("echo of the line", 0x0402, 0xC3);

  // ---- control characters go through unchanged and take no column
  homeCursor();
  card->clear();
  prints("\x1B" "E\x0F");
  expectBytes("escape and SI", "\x9B\xC5\x8F");
  expectByte("control characters take no column", COL, 1);   // only the E

  // ---- ^I 80N: width 80, echo off, line feed still on; nothing printed
  homeCursor();
  card->clear();
  print(0x09); prints("80N");
  expectBytes("^I 80N prints nothing", "");
  expectByte("^I 80N: width", LEN, 80);
  expectByte("^I 80N: echo off, LF on", FLAGS, 0x40);
  print('Z');
  expectBytes("after ^I 80N", "\xDA");
  expectByte("no echo after ^I 80N", 0x0400, 0xA0);
  prints("\r");
  expectBytes("CR still gets LF after N", "\x8D\x8A");

  // ---- a width under 40 is ignored
  print(0x09); prints("20N");
  expectByte("^I 20N keeps the width", LEN, 80);

  // ---- ^I K: echo on at 40, line feed toggled off
  print(0x09); print('K');
  expectByte("^I K: width", LEN, 40);
  expectByte("^I K: echo on, LF off", FLAGS, 0x80);
  card->clear();
  prints("Q\r");
  expectBytes("CR alone after ^I K", "\xD1\x8D");
  expectByte("echo back after ^I K", 0x0400, 0xD1);
  print(0x09); print('O');
  expectByte("^I O toggles LF back", FLAGS, 0xC0);

  // ---- ^I 132L: echo and line feed off, width 132
  print(0x09); prints("132L");
  expectByte("^I 132L: width", LEN, 132);
  expectByte("^I 132L: echo off, LF off", FLAGS, 0x00);
  print(0x09); print('H');
  expectByte("^I H alone keeps the width", LEN, 132);
  print(0x09); prints("50J");
  expectByte("^I 50J: width", LEN, 50);
  expectByte("^I 50J: LF stays off", FLAGS, 0x00);

  // ---- ^I I: everything back to the defaults
  print(0x09); print('I');
  expectByte("^I I: width", LEN, 40);
  expectByte("^I I: echo and LF", FLAGS, 0xC0);
  print(0x09); print('M');
  expectByte("^I M: width", LEN, 40);

  // ---- a letter that is no command is swallowed with its ^I, and the
  // command is over
  homeCursor();
  card->clear();
  print(0x09); print('X');
  expectBytes("^I X prints nothing", "");
  print('Y');
  expectBytes("after ^I X", "\xD9");

  // ---- the automatic line ending at the width
  print(0x09); prints("40N");
  card->clear();
  prints("\r");
  card->clear();
  char forty[41]; memset(forty, 'w', 40); forty[40] = 0;
  prints(forty);
  char want[48]; memset(want, 0xF7, 40); want[40] = 0x8D; want[41] = 0x8A; want[42] = 0;
  expectBytes("40 characters end the line", want);
  expectByte("column 0 after the automatic line end", COL, 0);
  prints("ab");
  expectBytes("the next line starts fresh", "\xE1\xE2");
  expectByte("column 2", COL, 2);
  print(0x09); print('I');

  // ---- CSW not ours: $Cn00 still resets but leaves CSW alone
  mmu->write(0x36, 0x1B); mmu->write(0x37, 0xFD);
  homeCursor();
  card->clear();
  print('R', true);
  expectBytes("$Cn00 with CSW elsewhere", "\xD2");
  expectByte("CSW untouched", 0x36, 0x1B);
  expectByte("but the settings were reset", LEN, 40);

  // ---- cursor synchronization, echo off: the ROM moves CH itself
  print(0x09); prints("80N");
  homeCursor();
  card->clear();
  prints("AB");
  expectBytes("echo off: bytes", "\xC1\xC2");
  expectByte("echo off: CH follows the column", 0x24, 2);
  expectByte("echo off: nothing on the screen", 0x0400, 0xA0);
  prints("\x1B");
  expectByte("echo off: a control character leaves CH alone", 0x24, 2);
  prints("\r");
  expectByte("echo off: CR zeroes CH", 0x24, 0);
  expectByte("echo off: CR zeroes the column", COL, 0);
  card->clear();
  // Applesoft's comma and HTAB move CH without printing anything
  mmu->write(0x24, 10);
  print('Z');
  expectBytes("echo off: the printer is padded out to CH", "\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xDA");
  expectByte("echo off: column after the padding", COL, 11);
  expectByte("echo off: CH after the padding", 0x24, 11);

  // ---- cursor synchronization, echo on: COUT1 moves CH, the printer follows
  print(0x09); print('I');
  prints("\r");
  homeCursor();
  card->clear();
  mmu->write(0x24, 3);              // HTAB 4
  print('Q');
  expectBytes("echo on: padded to CH", "\xA0\xA0\xA0\xD1");
  expectByte("echo on: the screen cell COUT1 chose", 0x0403, 0xD1);
  expectByte("echo on: CH from COUT1", 0x24, 4);
  expectByte("echo on: column", COL, 4);
  prints("\r");
  expectByte("echo on: CR", 0x24, 0);
  card->clear();

  // ---- the command character can be changed to another control character
  print(0x09); print(0x01);         // ^I ^A
  expectBytes("^I ^A prints nothing", "");
  expectByte("^I ^A: command character", CMDCH, 0x01);
  expectByte("^I ^A: command over", FLAGS, 0xC0);
  print(0x09);
  expectBytes("^I is data now", "\x89");
  print(0x01); prints("80N");
  expectBytes("^A 80N prints nothing", "");
  expectByte("^A 80N: width", LEN, 80);
  expectByte("^A 80N: echo off", FLAGS, 0x40);
  print(0x01); print(0x09);         // ^A ^I: back to ^I
  expectByte("^A ^I: command character", CMDCH, 0x09);
  print(0x09); print('I');
  expectByte("^I I works again", LEN, 40);

  // ---- a return after the command character is not a command character:
  // it cancels the command and is printed
  card->clear();
  print(0x09); prints("\r");
  expectBytes("^I CR: the return is printed", "\x8D\x8A");
  expectByte("^I CR: command character unchanged", CMDCH, 0x09);
  expectByte("^I CR: command over", FLAGS, 0xC0);
  prints("A");
  expectBytes("after ^I CR", "\xC1");
}

int main(int argc, char **argv)
{
  g_filemanager = new NixFileManager();
  g_speaker = new NullSpeaker();
  g_paddles = new NullPaddles();
  g_ui = new NullUI();
  g_display = new NullDisplay();
  g_printer = new NullPrinter();
  g_cpu = &cpu;

  disp = new AppleDisplay();
  mmu = new AppleMMU(disp);
  disp->SetMMU(mmu);
  cpu.SetMMU(mmu);
  mmu->Reset();

  printf("parallel printer slot ROM\n");
  static const uint8_t slots[] = { 1, 2, 5, 7 };
  for (size_t i = 0; i < sizeof(slots); i++) {
    char name[16];
    snprintf(name, sizeof(name), "slot %d", slots[i]);
    slotName = name;
    printf(" %s\n", name);
    placeCard(slots[i]);
    runSuite();
  }

  if (failures) {
    printf("%d FAILURE%s\n", failures, failures == 1 ? "" : "S");
    return 1;
  }
  printf("all passed\n");
  return 0;
}
