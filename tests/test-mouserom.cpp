// test-mouserom: the mouse card's slot ROM (apple/mouserom.s), called by
// the real CPU, in more than one slot.
//
// WHY THIS EXISTS. The ROM used to work only in slot 4: eight of its
// stores named slot 4's registers outright and one of its loads named
// slot 4's screen hole. The rewrite is slot-independent, and the only
// way to know that is to run the same calls with the card in every
// slot and require the same answers from each, delivered through THAT
// slot's registers and screen holes. Every routine in the entry table is
// called the way the protocol says (X = $Cn, Y = $n0, JSR through the
// table), and both hooks are entered the way PR#n and IN#n enter them.
// Each check requires the positive result: the bytes the card wrote, the
// clamp the physical mouse was given, the flag the caller reads.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "cpu.h"
#include "applemmu.h"
#include "appledisplay.h"
#include "mouse.h"
#include "physicalspeaker.h"
#include "physicalpaddles.h"
#include "physicaldisplay.h"
#include "physicalmouse.h"
#include "filemanager.h"
#include "vmram.h"
#include "vm.h"
#include "vmui.h"
#include "vmkeyboard.h"
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

// The card reaches the MMU through the VM; this VM is nothing but that.
class TestVM : public VM {
public:
  virtual ~TestVM() { mmu = NULL; }   // the test owns the MMU
  virtual bool Suspend(const char *) { return false; }
  virtual bool Resume(const char *) { return false; }
  virtual VMKeyboard *getKeyboard() { return NULL; }
  virtual void Reset() {}
  virtual void triggerPaddleInCycles(uint8_t, uint16_t) {}
};

// The physical mouse: the test sets where it is, and records what the
// card told it.
class TestMouse : public PhysicalMouse {
public:
  uint16_t x, y; bool button;
  int clampCalls; uint8_t lastClampDir; uint16_t lastClampLo, lastClampHi;
  int positionCalls;
  uint16_t lo(uint8_t d) { return lowClamp[d]; }
  uint16_t hi(uint8_t d) { return highClamp[d]; }
  TestMouse() { x = y = 0; button = false; clampCalls = positionCalls = 0; lastClampDir = 0xFF; lastClampLo = lastClampHi = 0; }
  virtual void maintainMouse() {}
  virtual void setClamp(uint8_t d, uint16_t lo, uint16_t hi) {
    PhysicalMouse::setClamp(d, lo, hi);
    clampCalls++; lastClampDir = d; lastClampLo = lo; lastClampHi = hi;
  }
  virtual void setPosition(uint16_t nx, uint16_t ny) { x = nx; y = ny; positionCalls++; }
  virtual void getPosition(uint16_t *px, uint16_t *py) { *px = x; *py = y; }
  virtual bool getButton() { return button; }
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
uint8_t g_slotMouse = 0;
uint8_t g_displayType = m_blackAndWhite;
uint8_t g_luminanceCutoff = 0;
bool g_video7 = false;

static Cpu cpu;
static AppleMMU *mmu;
static AppleDisplay *disp;
static TestMouse *pm;
static Mouse *card;
static int failures = 0;
static const char *slotName;   // "slot 5" etc., prefixed on every failure

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

static uint16_t romBase;   // $Cn00

// JSR-equivalent: the return address goes on the stack by hand, and the
// run stops when the RTS lands on it.
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

struct Result { uint8_t a, x, y, sp; bool c, i; bool returned; };

static Result capture(bool returned)
{
  Result r;
  r.returned = returned;
  r.a = cpu.a; r.x = cpu.x; r.y = cpu.y; r.sp = cpu.sp;
  r.c = cpu.flags & F_C; r.i = cpu.flags & F_I;
  return r;
}

enum { SETMOUSE = 0x12, SERVEMOUSE, READMOUSE, CLEARMOUSE, POSMOUSE, CLAMPMOUSE, HOMEMOUSE, INITMOUSE };

// An API call: X = $Cn, Y = $n0, A = the argument, through the table.
// C is set going in, so a routine that forgets to clear it is caught.
// With bogusXY the registers hold junk instead, for the one routine that
// must work without them.
static Result apiCall(uint8_t entry, uint8_t a, bool intsOff = true, bool bogusXY = false)
{
  uint8_t slot = g_slotMouse;
  cpu.a = a;
  cpu.x = bogusXY ? 0x3C : 0xC0 + slot;
  cpu.y = bogusXY ? 0xB1 : slot << 4;
  cpu.flags = F_C | (intsOff ? F_I : 0);
  uint16_t target = romBase + mmu->read(romBase + entry);
  return capture(runFrom(target, 500));
}

// the success contract: returned, C clear, X and Y untouched
static void expectOk(const char *what, const Result &r)
{
  uint8_t slot = g_slotMouse;
  if (!r.returned) { fail(what, "never returned (PC=$%04X)", cpu.pc); return; }
  if (r.c) fail(what, "carry set");
  if (r.x != 0xC0 + slot || r.y != (slot << 4))
    fail(what, "X=$%02X Y=$%02X on return, expected $%02X/$%02X", r.x, r.y, 0xC0 + slot, slot << 4);
  if (r.sp != 0xFF) fail(what, "stack pointer $%02X on return", r.sp);
}

static void expectErr(const char *what, const Result &r)
{
  if (!r.returned) { fail(what, "never returned (PC=$%04X)", cpu.pc); return; }
  if (!r.c) fail(what, "carry clear, expected the error flag");
  if (r.sp != 0xFF) fail(what, "stack pointer $%02X on return", r.sp);
}

// A screen hole for this slot: $0478+n and friends.
static uint16_t hole(uint16_t base) { return base + g_slotMouse; }

// Put the card in a slot, or take it out (slot 0).
static void placeMouse(uint8_t slot)
{
  if (g_slotMouse) {
    mmu->setSlot(g_slotMouse, NULL);   // deletes the card
    mmu->clearSlotRom(g_slotMouse);
  }
  g_slotMouse = slot;
  card = NULL;
  if (slot) {
    card = new Mouse();
    mmu->setSlot(slot, card);
    romBase = 0xC000 + (slot << 8);
  }
}

// ---------------------------------------------------------------------
// the suite, run once per slot

static void runSuite()
{
  uint8_t slot = g_slotMouse;

  // ---- ID bytes and the slot bytes the loader filled in
  expectByte("ID byte", romBase + 0x05, 0x38);
  expectByte("ID byte", romBase + 0x07, 0x18);
  expectByte("ID byte", romBase + 0x0B, 0x01);
  expectByte("ID byte", romBase + 0x0C, 0x20);
  expectByte("ID byte", romBase + 0xFB, 0xD6);
  expectByte("version", romBase + 0xFF, 0x01);
  expectByte("slot byte X", romBase + 0x23, 0xA2);
  expectByte("slot byte X", romBase + 0x24, 0xC0 + slot);
  expectByte("slot byte Y", romBase + 0x25, 0xA0);
  expectByte("slot byte Y", romBase + 0x26, slot << 4);
  expectByte("SERVEMOUSE slot byte X", romBase + 0x65, 0xA2);
  expectByte("SERVEMOUSE slot byte X", romBase + 0x66, 0xC0 + slot);
  expectByte("SERVEMOUSE slot byte Y", romBase + 0x67, 0xA0);
  expectByte("SERVEMOUSE slot byte Y", romBase + 0x68, slot << 4);
  for (int e = SETMOUSE; e <= INITMOUSE; e++) {
    uint8_t off = mmu->read(romBase + e);
    if (off < 0x28 || off >= 0xFB) fail("entry table", "entry $%02X points at $Cn%02X, outside the code", e, off);
  }

  // ---- INITMOUSE: clamps to 0..1023 in both axes, and says so in the
  // slot-0 holes
  pm->clampCalls = 0;
  Result r = apiCall(INITMOUSE, 0);
  expectOk("INITMOUSE", r);
  if (pm->clampCalls != 2) fail("INITMOUSE", "%d clamp calls, expected 2", pm->clampCalls);
  if (pm->lo(XCLAMP) != 0 || pm->hi(XCLAMP) != 1023 ||
      pm->lo(YCLAMP) != 0 || pm->hi(YCLAMP) != 1023)
    fail("INITMOUSE", "clamps are X %d..%d Y %d..%d, expected 0..1023",
         pm->lo(XCLAMP), pm->hi(XCLAMP), pm->lo(YCLAMP), pm->hi(YCLAMP));
  expectByte("INITMOUSE clamp holes", 0x0478, 0x00);
  expectByte("INITMOUSE clamp holes", 0x04F8, 0xFF);
  expectByte("INITMOUSE clamp holes", 0x0578, 0x00);
  expectByte("INITMOUSE clamp holes", 0x05F8, 0x03);

  // ---- SETMOUSE: the mode lands in $07F8+n; modes >= $10 are refused
  mmu->write(hole(0x7F8), 0xEE);
  r = apiCall(SETMOUSE, 0x01);
  expectOk("SETMOUSE 1", r);
  expectByte("SETMOUSE 1", hole(0x7F8), 0x01);
  r = apiCall(SETMOUSE, 0x10);
  expectErr("SETMOUSE $10", r);
  expectByte("SETMOUSE $10 left the mode alone", hole(0x7F8), 0x01);
  if (!card->isEnabled()) fail("SETMOUSE 1", "card not enabled");

  // ---- READMOUSE: position and button through this slot's holes
  pm->x = 300; pm->y = 200; pm->button = true;
  r = apiCall(READMOUSE, 0);
  expectOk("READMOUSE", r);
  expectByte("READMOUSE X low", hole(0x478), 300 & 0xFF);
  expectByte("READMOUSE X high", hole(0x578), 300 >> 8);
  expectByte("READMOUSE Y low", hole(0x4F8), 200 & 0xFF);
  expectByte("READMOUSE Y high", hole(0x5F8), 200 >> 8);
  {
    uint8_t st = mmu->read(hole(0x778));
    if (!(st & 0x80)) fail("READMOUSE status", "button-down bit clear ($%02X)", st);
    if (!(st & 0x20)) fail("READMOUSE status", "moved bit clear ($%02X)", st);
  }
  // no other slot's holes were written
  for (int s = 1; s <= 7; s++) {
    if (s == slot) continue;
    if (mmu->read(0x478 + s) || mmu->read(0x578 + s) || mmu->read(0x4F8 + s) || mmu->read(0x5F8 + s) || mmu->read(0x778 + s))
      fail("READMOUSE", "wrote into slot %d's screen holes", s);
  }

  // ---- CLEARMOUSE: zeroes, and the physical pointer goes home
  r = apiCall(CLEARMOUSE, 0);
  expectOk("CLEARMOUSE", r);
  expectByte("CLEARMOUSE", hole(0x478), 0);
  expectByte("CLEARMOUSE", hole(0x578), 0);
  expectByte("CLEARMOUSE", hole(0x4F8), 0);
  expectByte("CLEARMOUSE", hole(0x5F8), 0);
  if (pm->x != 0 || pm->y != 0) fail("CLEARMOUSE", "pointer at %d,%d, expected 0,0", pm->x, pm->y);

  // ---- POSMOUSE: the holes set the pointer
  mmu->write(hole(0x478), 100 & 0xFF); mmu->write(hole(0x578), 100 >> 8);
  mmu->write(hole(0x4F8), 50 & 0xFF);  mmu->write(hole(0x5F8), 50 >> 8);
  r = apiCall(POSMOUSE, 0);
  expectOk("POSMOUSE", r);
  if (pm->x != 100 || pm->y != 50) fail("POSMOUSE", "pointer at %d,%d, expected 100,50", pm->x, pm->y);

  // ---- CLAMPMOUSE: bounds from the slot-0 holes, A picks the axis
  mmu->write(0x0478, 10); mmu->write(0x0578, 0);      // low = 10
  mmu->write(0x04F8, 500 & 0xFF); mmu->write(0x05F8, 500 >> 8);  // high = 500
  pm->clampCalls = 0;
  r = apiCall(CLAMPMOUSE, 0);
  expectOk("CLAMPMOUSE X", r);
  if (pm->clampCalls != 1 || pm->lastClampDir != XCLAMP || pm->lastClampLo != 10 || pm->lastClampHi != 500)
    fail("CLAMPMOUSE X", "got %d call(s), last dir %d %d..%d; expected X 10..500", pm->clampCalls, pm->lastClampDir, pm->lastClampLo, pm->lastClampHi);
  r = apiCall(CLAMPMOUSE, 1);
  expectOk("CLAMPMOUSE Y", r);
  if (pm->lastClampDir != YCLAMP || pm->lastClampLo != 10 || pm->lastClampHi != 500)
    fail("CLAMPMOUSE Y", "last dir %d %d..%d; expected Y 10..500", pm->lastClampDir, pm->lastClampLo, pm->lastClampHi);
  r = apiCall(CLAMPMOUSE, 2);
  expectErr("CLAMPMOUSE 2", r);

  // ---- HOMEMOUSE: to the low clamp corner, from the slot-0 holes
  r = apiCall(HOMEMOUSE, 0);
  expectOk("HOMEMOUSE", r);
  if (pm->x != 10 || pm->y != 500) fail("HOMEMOUSE", "pointer at %d,%d, expected 10,500", pm->x, pm->y);

  // ---- SERVEMOUSE: with VBL interrupts on, a frame raises one; the ROM
  // must report it from THIS slot's status hole, and leave I as it found it
  r = apiCall(SETMOUSE, 0x09);            // enabled + VBL interrupt
  expectOk("SETMOUSE 9", r);
  int64_t t = 1000000;
  card->maintainMouse(t);                 // arms the period
  card->maintainMouse(t + 17030 + 10);    // and this frame fires
  mmu->write(hole(0x778), 0);
  r = apiCall(SERVEMOUSE, 0);
  expectOk("SERVEMOUSE with a VBL pending", r);
  if (!(mmu->read(hole(0x778)) & 0x08)) fail("SERVEMOUSE", "VBL bit not published in $%04X", hole(0x778));
  if (!r.i) fail("SERVEMOUSE", "I flag not restored (was set going in)");
  r = apiCall(READMOUSE, 0);              // clears the reason
  expectOk("READMOUSE after SERVEMOUSE", r);
  r = apiCall(SERVEMOUSE, 0, false);      // nothing pending, I clear going in
  expectErr("SERVEMOUSE with nothing pending", r);
  if (r.i) fail("SERVEMOUSE", "I flag left set (was clear going in)");
  // A2osX's interrupt handler JSRs to SERVEMOUSE with X and Y unset (on a
  // //c nothing needs them), so it must find its own slot and give the
  // caller's X and Y back
  card->maintainMouse(t + 2 * 17030 + 20);
  mmu->write(hole(0x778), 0);
  r = apiCall(SERVEMOUSE, 0, true, true);
  if (!r.returned) fail("SERVEMOUSE with junk X/Y", "never returned (PC=$%04X)", cpu.pc);
  if (r.c) fail("SERVEMOUSE with junk X/Y", "carry set: it did not find its own slot");
  if (r.x != 0x3C || r.y != 0xB1) fail("SERVEMOUSE with junk X/Y", "X=$%02X Y=$%02X, expected the caller's $3C/$B1 back", r.x, r.y);
  if (!(mmu->read(hole(0x778)) & 0x08)) fail("SERVEMOUSE with junk X/Y", "VBL bit not published in $%04X", hole(0x778));
  r = apiCall(READMOUSE, 0);
  expectOk("READMOUSE after the junk-X/Y SERVEMOUSE", r);
  r = apiCall(SETMOUSE, 0x00);
  expectOk("SETMOUSE 0", r);

  // ---- the output hook, as PR#n calls it: A is the character, X and Y
  // must come back untouched, the card notes the mode
  mmu->write(0x38, 0x1B); mmu->write(0x39, 0xFD);   // KSW = KEYIN, not us
  mmu->write(hole(0x7F8), 0xEE);
  cpu.a = 0xC1; cpu.x = 0x11; cpu.y = 0x22; cpu.flags = F_I;
  r = capture(runFrom(romBase, 500));
  if (!r.returned) fail("PR#n hook", "never returned (PC=$%04X)", cpu.pc);
  if (r.a != 0xC1 || r.x != 0x11 || r.y != 0x22) fail("PR#n hook", "A=$%02X X=$%02X Y=$%02X, expected $C1/$11/$22", r.a, r.x, r.y);
  if (r.sp != 0xFF) fail("PR#n hook", "stack pointer $%02X", r.sp);
  expectByte("PR#n hook: mode", hole(0x7F8), 0x01);
  expectByte("PR#n hook: KSW untouched", 0x38, 0x1B);

  // ---- the input hook, as IN#n's first call reaches it: KSW points at
  // $Cn00, the ROM moves it to $Cn05 and answers with the mouse line
  pm->x = 300; pm->y = 200; pm->button = false;
  mmu->write(0x38, 0x00); mmu->write(0x39, 0xC0 + slot);
  cpu.a = 0x00; cpu.x = 0x00; cpu.y = 0x22; cpu.flags = F_I;
  r = capture(runFrom(romBase, 500));
  if (!r.returned) fail("IN#n first call", "never returned (PC=$%04X)", cpu.pc);
  expectByte("IN#n first call: KSW moved to $Cn05", 0x38, 0x05);
  expectByte("IN#n first call: KSW page", 0x39, 0xC0 + slot);
  {
    const char *want = "300,200,4\r";   // button up, was up: 3+1
    char got[32]; int n = 0;
    for (; n < 31; n++) { got[n] = mmu->read(0x200 + n) & 0x7F; if (got[n] == '\r') { n++; break; } }
    got[n] = 0;
    if (strcmp(got, want)) fail("IN#n first call", "buffer holds \"%.*s\", expected \"300,200,4\"", n - 1, got);
    if (r.x != strlen(want) - 1) fail("IN#n first call", "X=%d, expected the length %d", r.x, (int)strlen(want) - 1);
    if (r.a != 0x8D) fail("IN#n first call", "A=$%02X, expected the return ($8D)", r.a);
    if (r.y != 0x22) fail("IN#n first call", "Y=$%02X, expected $22 back", r.y);
    if (r.sp != 0xFF) fail("IN#n first call", "stack pointer $%02X", r.sp);
  }
  // and every call after that comes in at $Cn05
  pm->x = 7; pm->y = 9; pm->button = true;
  cpu.a = 0x00; cpu.x = 0x00; cpu.y = 0x33; cpu.flags = F_I;
  r = capture(runFrom(romBase + 5, 500));
  if (!r.returned) fail("IN#n call", "never returned (PC=$%04X)", cpu.pc);
  {
    const char *want = "7,9,2\r";        // button down, was up: 1+1
    char got[32]; int n = 0;
    for (; n < 31; n++) { got[n] = mmu->read(0x200 + n) & 0x7F; if (got[n] == '\r') { n++; break; } }
    got[n] = 0;
    if (strcmp(got, want)) fail("IN#n call", "buffer holds \"%.*s\", expected \"7,9,2\"", n - 1, got);
    if (r.x != strlen(want) - 1) fail("IN#n call", "X=%d, expected %d", r.x, (int)strlen(want) - 1);
    if (r.y != 0x33) fail("IN#n call", "Y=$%02X, expected $33 back", r.y);
  }
  expectByte("IN#n call: KSW stays", 0x38, 0x05);
}

int main(int argc, char **argv)
{
  g_filemanager = new NixFileManager();
  g_speaker = new NullSpeaker();
  g_paddles = new NullPaddles();
  g_ui = new NullUI();
  g_display = new NullDisplay();
  g_cpu = &cpu;
  pm = new TestMouse();
  g_mouse = pm;

  disp = new AppleDisplay();
  mmu = new AppleMMU(disp);
  disp->SetMMU(mmu);
  cpu.SetMMU(mmu);
  mmu->Reset();
  TestVM *vm = new TestVM();
  vm->SetMMU(mmu);
  g_vm = vm;

  printf("mouse slot ROM\n");
  // every slot the BIOS offers the mouse. Slot 3 is left out because the
  // MMU keeps a slot-3 card's ROM behind the internal 80-column firmware,
  // so a run there would test the MMU, not the ROM.
  static const uint8_t slots[] = { 1, 2, 4, 5, 6, 7 };
  for (size_t i = 0; i < sizeof(slots); i++) {
    char name[16];
    snprintf(name, sizeof(name), "slot %d", slots[i]);
    slotName = name;
    printf(" %s\n", name);
    placeMouse(slots[i]);
    // the previous slot's holes must be clean for the cross-slot check
    for (int s = 0; s <= 7; s++) {
      mmu->write(0x478 + s, 0); mmu->write(0x578 + s, 0);
      mmu->write(0x4F8 + s, 0); mmu->write(0x5F8 + s, 0);
      mmu->write(0x778 + s, 0); mmu->write(0x7F8 + s, 0);
    }
    runSuite();
  }
  placeMouse(0);

  if (failures) {
    printf("%d FAILURE%s\n", failures, failures == 1 ? "" : "S");
    return 1;
  }
  printf("all passed\n");
  return 0;
}
