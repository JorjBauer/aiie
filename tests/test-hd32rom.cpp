// test-hd32rom: the HD32 hard-disk card's slot ROM (apple/hd32rom.s),
// entered by the real CPU.
//
// WHY THIS EXISTS. The ROM is 194 bytes that the machine ROM, ProDOS and
// SmartPort-style loaders enter at fixed offsets, with a contract about
// what comes back in the registers, the flags and the stack. A full boot
// exercises one path of it and reports nothing but a picture. This links
// the real Cpu, AppleMMU and HD32 against scratch images whose every
// block is known, enters the ROM the way each caller does, and requires
// the positive outcome: the right bytes in the right place, the flags and
// registers the contract promises, and the failure paths failing the way
// ProDOS expects, not merely "no crash".

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "cpu.h"
#include "applemmu.h"
#include "appledisplay.h"
#include "hd32.h"
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

// physicaldisplay.cpp paints drive lamps through AppleVM; there is no VM
// here, but the linker wants the symbol.
const char *AppleVM::DiskName(uint8_t) { return ""; }
const char *AppleVM::HDName(uint8_t) { return ""; }

// ---------------------------------------------------------------------
// stubs: the MMU and the card want these to exist, not to do anything

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
uint8_t g_slotDiskII = 6;
uint8_t g_slotHD32 = 7;
uint8_t g_displayType = m_blackAndWhite;
uint8_t g_luminanceCutoff = 0;
bool g_video7 = false;

static Cpu cpu;
static AppleMMU *mmu;
static AppleDisplay *disp;
static HD32 *hd;

static int failures = 0;

// ---------------------------------------------------------------------
// THE IMAGES. Two scratch files whose every block is a known pattern, so
// a read that lands the wrong block, the wrong drive or the wrong image
// is caught by content, not by the absence of an error.

#define BLK 512
#define IMG1_BLOCKS 300   // past 255, so the block number needs both bytes
#define IMG2_BLOCKS 16

static char img1Path[1024], img2Path[1024], img3Path[1024];

// block b of image n: byte i = (n*31 + b*7 + i) & 0xFF, except that block
// 0 of a bootable image starts with $01 (the ProDOS boot-block mark).
static uint8_t patternByte(int image, int block, int i, bool bootable)
{
  if (block == 0 && i == 0) return bootable ? 0x01 : 0x00;
  return (uint8_t)(image * 31 + block * 7 + i);
}

static void makeImage(const char *path, int image, int blocks, bool bootable)
{
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  for (int b = 0; b < blocks; b++) {
    uint8_t buf[BLK];
    for (int i = 0; i < BLK; i++) buf[i] = patternByte(image, b, i, bootable);
    fwrite(buf, 1, BLK, f);
  }
  fclose(f);
}

// ---------------------------------------------------------------------
// DRIVING THE CPU. Every entry is made the way the real caller makes it:
// the boot by jumping to $Cn00 as the machine ROM's slot scan does, the
// ProDOS call by a JSR with $42..$47 set, the SmartPort call by executing
// an actual JSR / .byte / .word sequence assembled into RAM. The run stops
// when the PC reaches the address the contract says it must, or when it
// is clearly lost.

#define SLOT_ROM  0xC700
#define FLOPPY    0xC600
#define BOOTRUN   0x0801

static bool runUntil(uint16_t stopA, uint16_t stopB, int limit)
{
  for (int i = 0; i < limit; i++) {
    if (cpu.pc == stopA || cpu.pc == stopB) return true;
    cpu.step();
  }
  return false;
}

struct Result { uint8_t a, x, y; bool c; bool returned; uint8_t sp; };

// The ProDOS block-device call: JSR $Cn0A with the call in $42..$47.
static Result prodosCall(uint8_t cmd, uint8_t unit, uint16_t buf, uint16_t block)
{
  const uint16_t ret = 0x0300;   // the "caller": nothing is ever run there
  mmu->write(0x42, cmd);
  mmu->write(0x43, unit);
  mmu->write(0x44, buf & 0xFF);
  mmu->write(0x45, buf >> 8);
  mmu->write(0x46, block & 0xFF);
  mmu->write(0x47, block >> 8);
  cpu.flags = F_I;               // C, Z, N, V all clear going in
  cpu.a = cpu.x = cpu.y = 0xEE;  // so a register the ROM never set shows
  // what a JSR from $02FD leaves: the address of its own last byte
  mmu->write(0x01FF, (ret - 1) >> 8);
  mmu->write(0x01FE, (ret - 1) & 0xFF);
  cpu.sp = 0xFD;
  cpu.pc = SLOT_ROM + 0x0A;
  Result r;
  r.returned = runUntil(ret, ret, 2000);
  r.a = cpu.a; r.x = cpu.x; r.y = cpu.y; r.c = cpu.flags & F_C; r.sp = cpu.sp;
  return r;
}

// The SmartPort call, exactly as a loader issues it:
//   $0300: JSR $Cn0D ; .byte cmd ; .word $0310   ; resumes at $0306
//   $0310: the parameter list
static Result smartportCall(uint8_t cmd, uint8_t unit, uint16_t buf, uint32_t block)
{
  const uint16_t site = 0x0300, plist = 0x0310, resume = 0x0306;
  mmu->write(site + 0, 0x20);
  mmu->write(site + 1, 0x0D);
  mmu->write(site + 2, 0xC7);
  mmu->write(site + 3, cmd);
  mmu->write(site + 4, plist & 0xFF);
  mmu->write(site + 5, plist >> 8);
  mmu->write(plist + 0, 3);
  mmu->write(plist + 1, unit);
  mmu->write(plist + 2, buf & 0xFF);
  mmu->write(plist + 3, buf >> 8);
  mmu->write(plist + 4, block & 0xFF);
  mmu->write(plist + 5, (block >> 8) & 0xFF);
  mmu->write(plist + 6, (block >> 16) & 0xFF);
  // poison $42..$47: the shim must build the whole call itself
  for (int i = 0x42; i <= 0x47; i++) mmu->write(i, 0xEE);
  cpu.sp = 0xFF;
  cpu.flags = F_I | F_C;         // C set going in: the shim must not trust it
  cpu.a = cpu.x = cpu.y = 0xEE;
  cpu.pc = site;
  Result r;
  r.returned = runUntil(resume, resume, 2000);
  r.a = cpu.a; r.x = cpu.x; r.y = cpu.y; r.c = cpu.flags & F_C; r.sp = cpu.sp;
  return r;
}

// Enter $Cn00 as the slot scan does. Returns where it ended up: the boot
// block's entry, the floppy controller, or wherever it was when the step
// budget ran out.
static uint16_t bootFrom(uint16_t stopA, uint16_t stopB)
{
  cpu.sp = 0xFF;
  cpu.flags = F_I;
  cpu.a = cpu.x = cpu.y = 0xEE;
  cpu.pc = SLOT_ROM;
  runUntil(stopA, stopB, 2000);
  return cpu.pc;
}

// ---------------------------------------------------------------------
// checks

static void fail(const char *what, const char *fmt, ...)
{
  failures++;
  printf("  FAIL: %s: ", what);
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  printf("\n");
}

static void expectByte(const char *what, uint16_t addr, uint8_t want)
{
  uint8_t got = mmu->read(addr);
  if (got != want) fail(what, "$%04X is $%02X, expected $%02X", addr, got, want);
}

// the ProDOS/SmartPort success contract: returned, C clear, A = 0
static void expectOk(const char *what, const Result &r)
{
  if (!r.returned) { fail(what, "never returned to the caller (PC=$%04X)", cpu.pc); return; }
  if (r.c) fail(what, "carry set, A=$%02X", r.a);
  if (r.a != 0) fail(what, "A=$%02X, expected 0", r.a);
  if (r.sp != 0xFF) fail(what, "stack pointer $%02X on return, expected $FF", r.sp);
}

// the failure contract: returned, C set, A = the ProDOS error code
static void expectError(const char *what, const Result &r, uint8_t code)
{
  if (!r.returned) { fail(what, "never returned to the caller (PC=$%04X)", cpu.pc); return; }
  if (!r.c) fail(what, "carry clear, A=$%02X", r.a);
  if (r.a != code) fail(what, "A=$%02X, expected $%02X", r.a, code);
  if (r.sp != 0xFF) fail(what, "stack pointer $%02X on return, expected $FF", r.sp);
}

// the whole block, and a guard byte on either side of the buffer
static void expectBlock(const char *what, uint16_t buf, int image, int block)
{
  int wrong = 0;
  for (int i = 0; i < BLK; i++)
    if (mmu->read(buf + i) != patternByte(image, block, i, true)) wrong++;
  if (wrong) fail(what, "%d of %d bytes at $%04X differ from image %d block %d", wrong, BLK, buf, image, block);
  if (mmu->read(buf - 1) != 0x5A) fail(what, "byte before the buffer was overwritten");
  if (mmu->read(buf + BLK) != 0x5A) fail(what, "byte after the buffer was overwritten");
}

static void guardBuffer(uint16_t buf)
{
  mmu->write(buf - 1, 0x5A);
  mmu->write(buf + BLK, 0x5A);
  for (int i = 0; i < BLK; i++) mmu->write(buf + i, 0xEE);
}

// the file on disk, for checking a write went where it should
static void expectFileBlock(const char *what, const char *path, int block, const uint8_t *want)
{
  uint8_t got[BLK];
  FILE *f = fopen(path, "rb");
  if (!f || fseek(f, (long)block * BLK, SEEK_SET) || fread(got, 1, BLK, f) != BLK) {
    fail(what, "cannot read block %d of %s", block, path);
    if (f) fclose(f);
    return;
  }
  fclose(f);
  if (memcmp(got, want, BLK)) fail(what, "block %d on disk does not hold what was written", block);
}

// (Re)build the card in slot 7 so loadROM runs with the current
// g_slotDiskII. setSlot deletes the previous card.
static void installCard()
{
  hd = new HD32(mmu);
  mmu->setSlot(g_slotHD32, hd);
}

// ---------------------------------------------------------------------

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
  installCard();

  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = "/tmp";
  snprintf(img1Path, sizeof(img1Path), "%s/test-hd32rom-1.%d.hdv", tmp, getpid());
  snprintf(img2Path, sizeof(img2Path), "%s/test-hd32rom-2.%d.hdv", tmp, getpid());
  snprintf(img3Path, sizeof(img3Path), "%s/test-hd32rom-3.%d.hdv", tmp, getpid());
  makeImage(img1Path, 1, IMG1_BLOCKS, true);
  makeImage(img2Path, 2, IMG2_BLOCKS, true);
  makeImage(img3Path, 3, 4, false);   // mounted but not bootable

  printf("HD32 slot ROM\n");

  // ---- 1. THE SIGNATURE AND ID BYTES. The slot scan, ProDOS and any
  // SmartPort-aware loader read these before running a byte of code.
  printf(" signature and ID bytes\n");
  expectByte("scan signature", SLOT_ROM + 0x01, 0x20);
  expectByte("scan signature", SLOT_ROM + 0x03, 0x00);
  expectByte("scan signature", SLOT_ROM + 0x05, 0x03);
  expectByte("not a SmartPort controller", SLOT_ROM + 0x07, 0x3C);
  expectByte("SmartPort ID type", SLOT_ROM + 0xFB, 0x00);
  expectByte("block count: ask STATUS", SLOT_ROM + 0xFC, 0x00);
  expectByte("block count: ask STATUS", SLOT_ROM + 0xFD, 0x00);
  expectByte("device attributes", SLOT_ROM + 0xFE, 0xD7);
  expectByte("ProDOS entry offset", SLOT_ROM + 0xFF, 0x0A);
  expectByte("slot byte patched in", SLOT_ROM + 0x0E, 0xA2);
  expectByte("slot byte patched in", SLOT_ROM + 0x0F, g_slotHD32 << 4);
  expectByte("floppy fallback patched in", SLOT_ROM + 0x40, 0x4C);
  expectByte("floppy fallback patched in", SLOT_ROM + 0x41, 0x00);
  expectByte("floppy fallback patched in", SLOT_ROM + 0x42, 0xC0 + g_slotDiskII);

  // ---- 2. PRODOS STATUS. Success is C clear, A = 0 and the size in X/Y;
  // an empty drive is C set with a ProDOS error code.
  printf(" ProDOS STATUS\n");
  hd->insertDisk(0, img1Path);
  Result r = prodosCall(0, 0x70, 0, 0);
  expectOk("status, drive 1", r);
  if (r.x != (IMG1_BLOCKS & 0xFF) || r.y != (IMG1_BLOCKS >> 8))
    fail("status, drive 1", "size X=$%02X Y=$%02X, expected %d blocks", r.x, r.y, IMG1_BLOCKS);
  r = prodosCall(0, 0xF0, 0, 0);
  expectError("status, drive 2 with nothing mounted", r, 0x27);
  hd->insertDisk(1, img2Path);
  r = prodosCall(0, 0xF0, 0, 0);
  expectOk("status, drive 2", r);
  if (r.x != IMG2_BLOCKS || r.y != 0)
    fail("status, drive 2", "size X=$%02X Y=$%02X, expected %d blocks", r.x, r.y, IMG2_BLOCKS);

  // ---- 3. PRODOS READ. Both drives, a page-crossing buffer, and the
  // block past the end, which must be an I/O error rather than silence.
  printf(" ProDOS READ\n");
  guardBuffer(0x2000);
  r = prodosCall(1, 0x70, 0x2000, 5);
  expectOk("read block 5, drive 1", r);
  expectBlock("read block 5, drive 1", 0x2000, 1, 5);
  guardBuffer(0x2080);
  r = prodosCall(1, 0xF0, 0x2080, 9);
  expectOk("read block 9, drive 2", r);
  expectBlock("read block 9, drive 2", 0x2080, 2, 9);
  guardBuffer(0x2000);
  r = prodosCall(1, 0x70, 0x2000, 299);
  expectOk("read block 299, drive 1 (high byte set)", r);
  expectBlock("read block 299, drive 1 (high byte set)", 0x2000, 1, 299);
  guardBuffer(0x2000);
  r = prodosCall(1, 0x70, 0x2000, IMG1_BLOCKS);
  expectError("read past the end", r, 0x27);
  // the same block number on the other drive is a different block: the
  // card's one-block cache must not answer for the wrong drive
  guardBuffer(0x2000);
  r = prodosCall(1, 0x70, 0x2000, 5);
  expectOk("read block 5, drive 1 again", r);
  guardBuffer(0x2000);
  r = prodosCall(1, 0xF0, 0x2000, 5);
  expectOk("read block 5, drive 2", r);
  expectBlock("read block 5, drive 2", 0x2000, 2, 5);
  // and swapping the image behind a drive drops its cached block
  hd->insertDisk(1, img1Path);
  guardBuffer(0x2000);
  r = prodosCall(1, 0xF0, 0x2000, 5);
  expectOk("read block 5, drive 2 after a swap", r);
  expectBlock("read block 5, drive 2 after a swap", 0x2000, 1, 5);
  hd->insertDisk(1, img2Path);

  // ---- 4. PRODOS WRITE. The bytes must reach the file, and read back.
  printf(" ProDOS WRITE\n");
  uint8_t want[BLK];
  for (int i = 0; i < BLK; i++) { want[i] = (uint8_t)(0xA5 ^ i); mmu->write(0x3000 + i, want[i]); }
  r = prodosCall(2, 0x70, 0x3000, 20);
  expectOk("write block 20, drive 1", r);
  expectFileBlock("write block 20, drive 1", img1Path, 20, want);
  guardBuffer(0x4000);
  r = prodosCall(1, 0x70, 0x4000, 20);
  expectOk("read back block 20", r);
  int wrong = 0;
  for (int i = 0; i < BLK; i++) if (mmu->read(0x4000 + i) != want[i]) wrong++;
  if (wrong) fail("read back block 20", "%d bytes differ", wrong);
  // and drive 2's file was not touched by a drive-1 write
  uint8_t untouched[BLK];
  for (int i = 0; i < BLK; i++) untouched[i] = patternByte(2, 20 % IMG2_BLOCKS, i, true);
  expectFileBlock("drive 2 untouched", img2Path, 20 % IMG2_BLOCKS, untouched);

  // ---- 5. SMARTPORT. The inline call form: return past the three
  // parameter bytes with the stack balanced, unit 1 and 2 mapped to the
  // two drives, STATUS answering with the size.
  printf(" SmartPort entry\n");
  guardBuffer(0x5000);
  r = smartportCall(1, 1, 0x5000, 7);
  expectOk("SmartPort read unit 1 block 7", r);
  expectBlock("SmartPort read unit 1 block 7", 0x5000, 1, 7);
  guardBuffer(0x5000);
  r = smartportCall(1, 2, 0x5000, 3);
  expectOk("SmartPort read unit 2 block 3", r);
  expectBlock("SmartPort read unit 2 block 3", 0x5000, 2, 3);
  r = smartportCall(0, 2, 0x5000, 0);
  expectOk("SmartPort status unit 2", r);
  if (r.x != IMG2_BLOCKS || r.y != 0)
    fail("SmartPort status unit 2", "size X=$%02X Y=$%02X, expected %d blocks", r.x, r.y, IMG2_BLOCKS);
  hd->ejectDisk(1);
  r = smartportCall(1, 2, 0x5000, 3);
  expectError("SmartPort read unit 2 after eject", r, 0x27);
  // a SmartPort write, then read it back through ProDOS
  for (int i = 0; i < BLK; i++) { want[i] = (uint8_t)(0x3C + i * 3); mmu->write(0x6000 + i, want[i]); }
  r = smartportCall(2, 1, 0x6000, 30);
  expectOk("SmartPort write unit 1 block 30", r);
  expectFileBlock("SmartPort write unit 1 block 30", img1Path, 30, want);

  // ---- 6. BOOT. With an image: block 0 at $0800, entered at $0801 with
  // X = slot*16, which is how the loader learns its slot. Otherwise the
  // floppy controller, for each reason it can happen.
  printf(" boot\n");
  guardBuffer(0x0800);
  uint16_t pc = bootFrom(BOOTRUN, FLOPPY);
  if (pc != BOOTRUN) fail("boot from drive 1", "ended at $%04X, expected $%04X", pc, BOOTRUN);
  else {
    expectBlock("boot from drive 1", 0x0800, 1, 0);
    if (cpu.x != (g_slotHD32 << 4)) fail("boot from drive 1", "X=$%02X at $0801, expected $%02X", cpu.x, g_slotHD32 << 4);
  }

  mmu->setAppleKey(0, true);
  pc = bootFrom(BOOTRUN, FLOPPY);
  if (pc != FLOPPY) fail("boot with Open-Apple held", "ended at $%04X, expected the floppy at $%04X", pc, FLOPPY);
  mmu->setAppleKey(0, false);

  hd->ejectDisk(0);
  pc = bootFrom(BOOTRUN, FLOPPY);
  if (pc != FLOPPY) fail("boot with nothing mounted", "ended at $%04X, expected the floppy at $%04X", pc, FLOPPY);

  hd->insertDisk(0, img3Path);
  pc = bootFrom(BOOTRUN, FLOPPY);
  if (pc != FLOPPY) fail("boot from a non-boot image", "ended at $%04X, expected the floppy at $%04X", pc, FLOPPY);

  // drive 2 alone does not boot: the boot path is drive 1 only
  hd->ejectDisk(0);
  hd->insertDisk(1, img2Path);
  pc = bootFrom(BOOTRUN, FLOPPY);
  if (pc != FLOPPY) fail("boot with only drive 2 mounted", "ended at $%04X, expected the floppy at $%04X", pc, FLOPPY);

  // and with no floppy controller at all, the fallback holds still on its
  // own JMP instead of running off the end of the ROM
  g_slotDiskII = 0;
  installCard();
  expectByte("no floppy: fallback loops", SLOT_ROM + 0x41, 0x40);
  expectByte("no floppy: fallback loops", SLOT_ROM + 0x42, 0xC7);
  pc = bootFrom(BOOTRUN, 0xFFFF);
  if (pc != SLOT_ROM + 0x40) fail("boot with nothing mounted and no floppy", "ended at $%04X, expected to hold at $%04X", pc, SLOT_ROM + 0x40);
  g_slotDiskII = 6;

  unlink(img1Path);
  unlink(img2Path);
  unlink(img3Path);

  if (failures) {
    printf("%d FAILURE%s\n", failures, failures == 1 ? "" : "S");
    return 1;
  }
  printf("all passed\n");
  return 0;
}
