// Characterization + round-trip tests for DiskII's LSS read path and
// its write path. Drives DiskII::readSwitches / writeSwitches the same
// way the 6502 would and checks the resulting byte stream (reads) or
// the resulting on-disk bit stream read back through the LSS (writes).
//
// Dependencies are stubbed so this test links without the rest of the
// emulator: we only need a Cpu with a `cycles` field, a no-op VMui,
// and a null AppleMMU pointer (DiskII stores it but never calls it).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#include "cpu.h"
#include "vmui.h"
#include "filemanager.h"
#include "globals.h"
#include "apple/appleui.h"
#include "apple/diskii.h"
#include "apple/woz.h"
#include "apple/nibutil.h"

// ---------------------------------------------------------------------
// Stubs for the globals DiskII reads (g_cpu->cycles, g_ui->drawOnOff...)
// ---------------------------------------------------------------------
static Cpu s_cpu;
class StubUI : public VMui {
 public:
  void drawStaticUIElement(uint8_t) override {}
  void drawOnOffUIElement(uint8_t, bool) override {}
  void drawPercentageUIElement(uint8_t, uint8_t) override {}
  void blit() override {}
};
static StubUI s_ui;

// globals.h extern list — satisfy the linker for anything diskii /
// woz drags in. NULL wherever the test doesn't use them.
FileManager *g_filemanager = NULL;
Cpu *g_cpu = &s_cpu;
VM *g_vm = NULL;
PhysicalDisplay *g_display = NULL;
PhysicalKeyboard *g_keyboard = NULL;
PhysicalMouse *g_mouse = NULL;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalPrinter *g_printer = NULL;
VMui *g_ui = &s_ui;
int8_t g_volume = 0;
uint8_t g_displayType = 0;
VMRam g_ram;
volatile uint8_t g_debugMode = 0;
volatile bool g_biosInterrupt = false;
uint32_t g_speed = 1023000;
bool g_invertPaddleX = false;
bool g_invertPaddleY = false;
uint8_t g_luminanceCutoff = 0;
char debugBuf[255];

// ---------------------------------------------------------------------
// Test framework (tiny — just counters and an ASSERT macro)
// ---------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
static const char *g_curTest = "";

#define TEST(name) do { g_curTest = name; fprintf(stderr, "\n[%s]\n", name); } while (0)

#define CHECK(cond, fmt, ...) do { \
  if (!(cond)) { \
    fprintf(stderr, "  FAIL %s: " fmt "\n", g_curTest, ##__VA_ARGS__); \
    g_fail++; \
  } else { \
    g_pass++; \
  } \
} while (0)

#define CHECK_EQ_U8(got, want, what) do { \
  uint8_t _g = (got), _w = (want); \
  if (_g != _w) { \
    fprintf(stderr, "  FAIL %s: %s — got $%02X, want $%02X\n", \
            g_curTest, what, _g, _w); \
    g_fail++; \
  } else { \
    g_pass++; \
  } \
} while (0)

// ---------------------------------------------------------------------
// Helpers to drive DiskII the way a 6502 program would.
// ---------------------------------------------------------------------
//
// Apple II slot 6 disk-II switches (relative to the slot base):
//   $0-$7: phase motor on/off (even=off, odd=on)
//   $8:    drive off   $9:   drive on
//   $A:    select drv1 $B:   select drv2
//   $C:    Q6 off  (shift/read)
//   $D:    Q6 on   (load/sense WP)
//   $E:    Q7 off  (read mode)
//   $F:    Q7 on   (write mode)

// Advance the emulated CPU clock and poll once. 7 cycles ≈ one LDA/BPL
// iteration in DOS 3.3's sync-scan loop.
static uint8_t cpuRead(DiskII &d, uint8_t reg, int cyclesSpent = 4) {
  g_cpu->cycles += cyclesSpent;
  return d.readSwitches(reg);
}
static void cpuWrite(DiskII &d, uint8_t reg, uint8_t val, int cyclesSpent = 4) {
  g_cpu->cycles += cyclesSpent;
  d.writeSwitches(reg, val);
}

// Poll $C08C in a BPL-like loop until a byte with bit 7 set is
// returned, or we give up after `maxPolls` tries. Each poll costs the
// same cycles a real LDA-BPL iter would: 4 (LDA) + 3 (BPL taken). On
// exit we add a few cycles representing the post-BPL work (EOR, CMP,
// store-to-buffer, etc.) so consecutive pollForByte calls have
// realistic inter-byte spacing — without it, the second call would
// catch the LSS still in its byte-flag hold window and re-read the
// same byte. ~16 cycles between bytes matches DOS 3.3's tighter
// header-reading loops.
static uint8_t pollForByte(DiskII &d, int maxPolls = 200) {
  for (int i = 0; i < maxPolls; i++) {
    uint8_t b = cpuRead(d, 0x0C, 4);   // LDA $C08C,X
    if (b & 0x80) {                    // BPL falls through
      g_cpu->cycles += 12;             // post-BPL processing
      return b;
    }
    g_cpu->cycles += 3;                // BPL taken, loop again
  }
  return 0x00;                         // timeout
}

// Turn on the drive, select drive 1, set read mode. Then give the
// sequencer a few rotations' worth of cycles to get into a predictable
// state before the test starts sampling.
static void primeForRead(DiskII &d) {
  cpuRead(d, 0x09);   // drive on
  cpuRead(d, 0x0A);   // select drive 1
  cpuRead(d, 0x0E);   // read mode
  // Let ~1ms of emulated time pass so the LSS settles and we're not
  // reading "bits before the motor turned on".
  for (int i = 0; i < 150; i++) {
    cpuRead(d, 0x0C, 7);
  }
}

// ---------------------------------------------------------------------
// Disk fixtures
// ---------------------------------------------------------------------
//
// Create a blank DSK (35×16×256 = 143360 zeroed bytes) in a temp file.
// DiskII::insertDisk will nibblize it on load, so this gives us a
// known-clean starting state.
static char s_tempPath[] = "/tmp/diskii-test-XXXXXX.dsk";
static const char *makeBlankDsk() {
  int fd = mkstemps(s_tempPath, 4);
  if (fd < 0) { perror("mkstemps"); exit(1); }
  uint8_t zero[256];
  memset(zero, 0, sizeof(zero));
  for (int t = 0; t < 35; t++)
    for (int s = 0; s < 16; s++)
      write(fd, zero, 256);
  close(fd);
  return s_tempPath;
}

// Load Miner 2049er II's side A if available — a useful fixture for
// exercising the LSS against real-world disk content (including its
// all-0xA5 track 1).
static const char *MINER_WOZ =
  "woz-test-images/WOZ 2.0/Miner 2049er II - Disk 1, Side A.woz";

// ---------------------------------------------------------------------
// Read tests
// ---------------------------------------------------------------------

// Scan track 0 for the first sector prolog (D5 AA 96) and verify the
// decoded volume/track/sector fields. This is what the Disk II boot
// ROM does; if the LSS is producing correct nibbles, we'll find a
// well-formed sector header in short order.
static void testReadFindsSectorProlog(const char *diskPath) {
  TEST("read: find D5 AA 96 sector prolog");
  DiskII d(NULL);
  d.insertDisk(0, diskPath, false);

  g_cpu->cycles = 0;
  primeForRead(d);

  // Scan for D5 AA 96 across up to ~2 full track rotations' worth of
  // bytes (~12000). One rotation is ~6400 nibbles.
  uint8_t prev2 = 0, prev1 = 0, cur = 0;
  int scanned = 0;
  bool found = false;
  for (; scanned < 20000; scanned++) {
    prev2 = prev1; prev1 = cur;
    cur = pollForByte(d);
    if (prev2 == 0xD5 && prev1 == 0xAA && cur == 0x96) {
      found = true;
      break;
    }
  }
  CHECK(found, "D5 AA 96 not found in %d bytes scanned", scanned);

  if (found) {
    // Decode the 4-4 encoded header immediately following.
    uint8_t v1 = pollForByte(d), v2 = pollForByte(d);
    uint8_t t1 = pollForByte(d), t2 = pollForByte(d);
    uint8_t s1 = pollForByte(d), s2 = pollForByte(d);
    uint8_t c1 = pollForByte(d), c2 = pollForByte(d);
    uint8_t vol = ((v1 << 1) | 1) & v2;
    uint8_t trk = ((t1 << 1) | 1) & t2;
    uint8_t sec = ((s1 << 1) | 1) & s2;
    uint8_t chk = ((c1 << 1) | 1) & c2;
    fprintf(stderr, "  sector header: vol=%02X trk=%02X sec=%02X chk=%02X\n",
            vol, trk, sec, chk);
    CHECK_EQ_U8(vol ^ trk ^ sec, chk, "sector checksum");
    CHECK(trk == 0, "expected track 0, got $%02X", trk);
    CHECK(sec < 16, "sector number $%02X out of range", sec);

    // An epilog like DE AA EB normally follows the checksum on
    // DOS 3.3, but Miner omits it (copy protection — the sector
    // body runs straight into the data prolog). Print what we see
    // as info only; don't require standard DOS formatting.
    uint8_t e1 = pollForByte(d), e2 = pollForByte(d);
    fprintf(stderr, "  (bytes after checksum: %02X %02X — "
                    "standard DOS would be DE AA)\n", e1, e2);
  }
}

// Miner 2049er II's track 1 is a solid stream of 0xA5 written tight —
// no timing gaps. Real hardware's LSS reads it as some repeating
// pattern; which byte depends on the 8-bit alignment cross-track sync
// lands us at, and that isn't guaranteed to be 0xA5 because WOZ's
// proportional scaling (new_pos = pos * new_len / old_len) doesn't
// preserve byte boundaries when tracks have different lengths. What
// we *do* require: the LSS must deliver bit-7-set bytes (the state
// machine didn't get stuck) and the distribution must be dominated by
// a small number of values (the pattern is repeating, not random).
static void testReadMinerTrack1A5(const char *diskPath) {
  TEST("read: Miner track 1 produces repeating bit-7-set pattern");
  DiskII d(NULL);
  d.insertDisk(0, diskPath, false);

  g_cpu->cycles = 0;
  primeForRead(d);

  // Step the head up to track 1 (quarter-track 4, two phase steps).
  cpuRead(d, 0x01); cpuRead(d, 0x00);   // phase 0 on/off
  cpuRead(d, 0x03); cpuRead(d, 0x02);   // phase 1 on/off
  cpuRead(d, 0x05); cpuRead(d, 0x04);   // phase 2 on/off

  int samples[256] = {0};
  int bit7set = 0;
  const int N = 400;
  for (int i = 0; i < N; i++) {
    uint8_t b = pollForByte(d);
    samples[b]++;
    if (b & 0x80) bit7set++;
  }
  CHECK(bit7set > N * 3 / 4,
        "only %d of %d samples had bit 7 set — LSS isn't assembling bytes",
        bit7set, N);

  // Count distinct values that appeared. A tight 0xA5 stream should
  // produce at most a small handful of distinct bytes as the LSS
  // cycles through its state machine — if the byte distribution is
  // diffuse, the LSS is probably stepping through garbage.
  int distinct = 0, topCount = 0;
  uint8_t topByte = 0;
  for (int v = 0; v < 256; v++) {
    if (samples[v]) distinct++;
    if (samples[v] > topCount) { topCount = samples[v]; topByte = v; }
  }
  fprintf(stderr, "  distinct byte values: %d, most common: $%02X x%d\n",
          distinct, topByte, topCount);
  // Loose threshold — the LSS cycles between a "held nibble" (QA WAIT)
  // and "rebuild" states as it auto-clears, so we can see the held
  // value plus a spread of partial-rebuild values. What matters is
  // that one value dominates, signalling the pattern is repeating.
  CHECK(topCount > N / 2,
        "most common byte $%02X only appeared %d/%d times — pattern "
        "isn't stable", topByte, topCount, N);
}

// ---------------------------------------------------------------------
// Write tests (round-trip through the LSS read path)
// ---------------------------------------------------------------------
//
// The real test for write correctness is: write a known sector, then
// read it back through the same LSS we trust for reads. If the read
// path decodes what the write path laid down, the write path is
// producing hardware-legal bit streams.

// Simulate the RWTS write-a-byte sequence. Real DOS 3.3 writes bytes
// at two different cadences:
//   40-cycle loops for sync leaders (produces 8 ONEs + 2 ZEROs per
//     byte, forming the 10-bit self-sync pattern);
//   32-cycle loops for data / address field bytes (tight-packed, no
//     gap bits between them).
// aiie's write path lays down a leading run of zero bits equal to
// calcExpectedBits() since the last write, then the 8 bits of the
// latch, so the caller controls the inter-byte gap via cycle spacing.
static void writeByte(DiskII &d, uint8_t v, int totalCycles) {
  // The full "store to latch + store to $C08C" pair is 8 cycles on
  // the 6502 (STA abs,X + STA abs,X); the rest is whatever padding
  // the caller needs to hit `totalCycles` start-to-start.
  cpuWrite(d, 0x0D, v, 4);                       // STA $C08D,X
  cpuWrite(d, 0x0C, v, totalCycles - 4);         // STA $C08C,X + pad
}
static void writeSyncByte(DiskII &d, uint8_t v) { writeByte(d, v, 40); }
static void writeDataByte(DiskII &d, uint8_t v) { writeByte(d, v, 32); }

static void testWriteThenReadFF(const char *diskPath) {
  TEST("write: FFs read back as bit-7-set bytes");
  DiskII d(NULL);
  d.insertDisk(0, diskPath, false);

  g_cpu->cycles = 0;
  cpuRead(d, 0x09);   // drive on
  cpuRead(d, 0x0A);   // select
  // Settle.
  for (int i = 0; i < 100; i++) cpuRead(d, 0x0C, 7);

  // Switch to write mode and lay down 200 bytes of 0xFF — a standard
  // sync leader. DOS 3.3 writes these in 40-cycle loops; 32-ish is
  // close enough for a single write per byte.
  cpuRead(d, 0x0F);   // enable write
  for (int i = 0; i < 200; i++) writeSyncByte(d, 0xFF);
  cpuRead(d, 0x0E);   // back to read mode

  // Now read back. After 200 FFs we should see a run of bit-7-set
  // bytes when we sample.
  int bit7set = 0;
  const int N = 100;
  for (int i = 0; i < N; i++) {
    uint8_t b = pollForByte(d);
    if (b & 0x80) bit7set++;
  }
  CHECK(bit7set > N * 3 / 4,
        "only %d of %d post-write samples had bit 7 set — write path "
        "isn't producing readable sync bytes", bit7set, N);
}

// Round-trip a sector header (D5 AA 96 + 4-4 encoded vol/trk/sec/chk +
// DE AA EB epilog) and verify we can find and decode it.
static void testWriteSectorHeaderRoundTrip(const char *diskPath) {
  TEST("write: sector header round-trips through LSS");
  DiskII d(NULL);
  d.insertDisk(0, diskPath, false);

  g_cpu->cycles = 0;
  cpuRead(d, 0x09);
  cpuRead(d, 0x0A);
  for (int i = 0; i < 100; i++) cpuRead(d, 0x0C, 7);

  const uint8_t vol = 0xFE, trk = 0x00, sec = 0x03;
  const uint8_t chk = vol ^ trk ^ sec;
  auto enc44_hi = [](uint8_t x){ return (uint8_t)(((x >> 1) & 0x55) | 0xAA); };
  auto enc44_lo = [](uint8_t x){ return (uint8_t)((x & 0x55) | 0xAA); };

  cpuRead(d, 0x0F);   // write mode
  // Sync leader: 40-cycle loops to lay down FF + 2 timing zeros.
  for (int i = 0; i < 64; i++) writeSyncByte(d, 0xFF);
  // Prolog + 4-4 encoded header fields + epilog: 32-cycle loops
  // (tight-packed, no inter-byte gap).
  writeDataByte(d, 0xD5); writeDataByte(d, 0xAA); writeDataByte(d, 0x96);
  writeDataByte(d, enc44_hi(vol)); writeDataByte(d, enc44_lo(vol));
  writeDataByte(d, enc44_hi(trk)); writeDataByte(d, enc44_lo(trk));
  writeDataByte(d, enc44_hi(sec)); writeDataByte(d, enc44_lo(sec));
  writeDataByte(d, enc44_hi(chk)); writeDataByte(d, enc44_lo(chk));
  writeDataByte(d, 0xDE); writeDataByte(d, 0xAA); writeDataByte(d, 0xEB);
  // Trailing sync so the read sequencer can find its way out.
  for (int i = 0; i < 64; i++) writeSyncByte(d, 0xFF);
  cpuRead(d, 0x0E);   // back to read mode

  // Scan for D5 AA 96 and decode.
  uint8_t p2 = 0, p1 = 0, c = 0;
  bool found = false;
  for (int i = 0; i < 3000 && !found; i++) {
    p2 = p1; p1 = c;
    c = pollForByte(d);
    if (p2 == 0xD5 && p1 == 0xAA && c == 0x96) found = true;
  }
  CHECK(found, "D5 AA 96 not found after write — write path broken");
  if (found) {
    uint8_t v1 = pollForByte(d), v2 = pollForByte(d);
    uint8_t t1 = pollForByte(d), t2 = pollForByte(d);
    uint8_t s1 = pollForByte(d), s2 = pollForByte(d);
    uint8_t c1 = pollForByte(d), c2 = pollForByte(d);
    fprintf(stderr, "  raw header bytes read back: "
                    "%02X %02X %02X %02X %02X %02X %02X %02X\n",
            v1, v2, t1, t2, s1, s2, c1, c2);
    fprintf(stderr, "  expected (wrote):           "
                    "%02X %02X %02X %02X %02X %02X %02X %02X\n",
            enc44_hi(vol), enc44_lo(vol),
            enc44_hi(trk), enc44_lo(trk),
            enc44_hi(sec), enc44_lo(sec),
            enc44_hi(chk), enc44_lo(chk));
    CHECK_EQ_U8(((v1 << 1) | 1) & v2, vol, "vol round-trip");
    CHECK_EQ_U8(((t1 << 1) | 1) & t2, trk, "trk round-trip");
    CHECK_EQ_U8(((s1 << 1) | 1) & s2, sec, "sec round-trip");
    CHECK_EQ_U8(((c1 << 1) | 1) & c2, chk, "chk round-trip");
  }
}

// ---------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------
int main(int argc, char *argv[]) {
  // Allow selecting which disk images to use via argv for flexibility;
  // default to Miner for the real-world read tests and a scratch DSK
  // for the write tests.
  const char *minerPath = (argc > 1) ? argv[1] : MINER_WOZ;
  const char *scratchPath = makeBlankDsk();

  if (access(minerPath, R_OK) == 0) {
    testReadFindsSectorProlog(minerPath);
    testReadMinerTrack1A5(minerPath);
  } else {
    fprintf(stderr, "\n(skipping Miner read tests — %s not found)\n",
            minerPath);
  }

  testWriteThenReadFF(scratchPath);
  testWriteSectorHeaderRoundTrip(scratchPath);

  unlink(scratchPath);

  fprintf(stderr, "\n==== %d passed, %d failed ====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
