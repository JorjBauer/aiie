#include "diskii.h"

#ifdef TEENSYDUINO
#include <Arduino.h>
#include "teensy-println.h"
#include "iocompat.h"
#else
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#endif

#include "applemmu.h" // for _FLOATINGBUS

#include "globals.h"
#include "appleui.h"

#include "serialize.h"

#include "diskii-rom.h"

#define DISKIIMAGIC 0xAA

// how many CPU cycles do we wait to spin down the disk drive? 1023000 == 1 second
#define SPINDOWNDELAY (1023000)

// When the drive spins down, we also need to be sure its contents are flushed.
#define FLUSHDELAY SPINDOWNDELAY

#define SPINFOREVER -2
#define NOTSPINNING -1

DiskII::DiskII(AppleMMU *mmu)
{
  this->mmu = mmu;

  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;
  curWozTrack[0] = curWozTrack[1] = 0xFF;

  writeMode = false;
  q6 = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;
  sequencer = 0;
  dataRegister = 0;
  lssState = 0;
  driveSpinupCycles[0] = driveSpinupCycles[1] = 0; // CPU cycle number when the disk drive spins up
  deliveredDiskBits[0] = deliveredDiskBits[1] = 0;

  disk[0] = disk[1] = NULL;
  diskIsSpinningUntil[0] = diskIsSpinningUntil[1] = -1;
  flushAt[0] = flushAt[1] = 0;
  selectedDisk = 0;
}

DiskII::~DiskII()
{
}

bool DiskII::Serialize(int8_t fd)
{
  serializeMagic(DISKIIMAGIC);
  serialize8(readWriteLatch);
  serialize8(sequencer);
  serialize8(dataRegister);
  serialize8(writeMode);
  serialize8(q6);
  serialize8(lssState);
  serialize8(writeProt);
  serialize8(selectedDisk);

  for (int i=0; i<2; i++) {
    serialize8(curHalfTrack[i]);
    serialize8(curWozTrack[i]);
    serialize8(curPhase[i]);
    serialize64(driveSpinupCycles[i]);
    serialize64(deliveredDiskBits[i]);
    serialize64(diskIsSpinningUntil[i]);
    
    if (disk[i]) {
      // Make sure we have flushed the disk images
      disk[i]->flush();
      flushAt[i] = 0; // and there's no need to re-flush them now

      serialize8(1);

      // FIXME: this ONLY works for builds using the filemanager to read
      // the disk image, so it's broken until we port Woz to do that!
      const char *fn = disk[i]->diskName();
      serializeString(fn);
      if (!disk[i]->Serialize(fd))
	goto err;
    } else {
      serialize8(0);
    }
  }

  serializeMagic(DISKIIMAGIC);

  return true;
 err:
  return false;
}

bool DiskII::Deserialize(int8_t fd)
{
  deserializeMagic(DISKIIMAGIC);

  deserialize8(readWriteLatch);
  deserialize8(sequencer);
  deserialize8(dataRegister);
  deserialize8(writeMode);
  deserialize8(q6);
  deserialize8(lssState);
  deserialize8(writeProt);
  deserialize8(selectedDisk);

  for (int i=0; i<2; i++) {
    deserialize8(curHalfTrack[i]);
    deserialize8(curWozTrack[i]);
    deserialize8(curPhase[i]);

    deserialize64(driveSpinupCycles[i]);
    deserialize64(deliveredDiskBits[i]);
    deserialize64(diskIsSpinningUntil[i]);

    uint8_t hasDisk;
    deserialize8(hasDisk);
    
    if (disk[i]) delete disk[i];
    if (hasDisk) {
      disk[i] = new WozSerializer();

      // FIXME: MAXPATH check!
      char fn[MAXPATH];
      deserializeString(fn);
      if (fn[0]) {
	printf("Restoring disk image named '%s'\n", fn);
	disk[i]->readFile((char *)fn, true, T_AUTO); // FIXME error checking    
      } else {
	// ERROR: there's a disk but we don't have the path to its image?
	printf("Failed to read inserted disk name for disk %d\n", i);
	goto err;
      }
      
      if (!disk[i]->Deserialize(fd)) {
	printf("Failed to deserialize disk %d\n", i);
	goto err;
      }
    } else {
      disk[i] = NULL;
    }
  }

  deserializeMagic(DISKIIMAGIC);

  return true;
  
 err:
  return false;
}

void DiskII::Reset()
{
  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;

  writeMode = false;
  q6 = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;
  lssState = 0;

  ejectDisk(0);
  ejectDisk(1);
}

void DiskII::driveOff()
{
  if (diskIsSpinningUntil[selectedDisk] == SPINFOREVER) {
    diskIsSpinningUntil[selectedDisk] = g_cpu->cycles + SPINDOWNDELAY; // 1 second lag
    // The drive-is-on-indicator is turned off later, when the disk
    // actually spins down.
  }
  
  if (disk[selectedDisk]) {
    flushAt[selectedDisk] = g_cpu->cycles + FLUSHDELAY;
    if (flushAt[selectedDisk] == 0)
      flushAt[selectedDisk] = 1; // fudge magic number; 0 is "don't flush"
  }
}

void DiskII::driveOn()
{
  if (diskIsSpinningUntil[selectedDisk] != SPINFOREVER) {
    // If the drive isn't already spinning, then start keeping track of how
    // many bits we've delivered (so we can honor the disk bit-delivery time
    // that might be in the Woz disk image).
    driveSpinupCycles[selectedDisk] = g_cpu->cycles;
    deliveredDiskBits[selectedDisk] = 0;
    diskIsSpinningUntil[selectedDisk] = SPINFOREVER;
  }
  // FIXME: does the sequencer get reset? Maybe if it's the selected disk? Or no?
  // sequencer = 0;
  g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true);
}

uint8_t DiskII::readSwitches(uint8_t s)
{
  tickLSS();

  switch (s) {
  case 0x00: // change stepper motor phase
    break;
  case 0x01:
    setPhase(0);
    break;
  case 0x02:
    break;
  case 0x03:
    setPhase(1);
    break;
  case 0x04:
    break;
  case 0x05:
    setPhase(2);
    break;
  case 0x06: // 3 off
    break;
  case 0x07: // 3 on
    setPhase(3);
    break;

  case 0x08: // drive off
    driveOff();
    break;
  case 0x09: // drive on
    driveOn();
    break;

  case 0x0A: // select drive 1
    select(0);
    break;
  case 0x0B: // select drive 2
    select(1);
    break;

  case 0x0C: // Q6 off: shift/read
    q6 = false;
    readWriteLatch = readOrWriteByte(true);
    // The LSS auto-clears the data register at state C (A0-CLR) or
    // state F (E0-CLR) on its own timing — no need to force a reset
    // here. Doing so would leave lssState out of sync with what real
    // hardware would be in at this point.
    break;

  case 0x0D: // Q6 on: load (sense WP in read mode)
    q6 = true;
    // Don't pre-load sequencer with WP. Once Q6 is asserted, tickLSS
    // runs the LOAD column of the LSS ROM (all 0A-SR entries), which
    // shifts the write-protect bit in from the MSB on every clock.
    // After 8 LSS clocks the register naturally becomes 0x00 or 0xFF.
    break;

  case 0x0E: // set read mode
    setWriteMode(false);
    break;
  case 0x0F: // set write mode
    setWriteMode(true);
    break;
  }

  // Any even address read returns the readWriteLatch (UTA2E Table 9.1,
  // p. 9-12, note 2). In real hardware that value is the current LSS
  // shift register, gated onto the bus via !A0. tickLSS() above has
  // already caught the sequencer up to the current cycle, so mirror
  // it into readWriteLatch here for every access except $C08C, which
  // populates readWriteLatch itself (including the overshoot-until-
  // bit-7 guarantee that the standard DOS read loop depends on).
  if (s != 0x0C) {
    readWriteLatch = sequencer;
  }
  return (s & 1) ? _FLOATINGBUS : readWriteLatch;
}

void DiskII::writeSwitches(uint8_t s, uint8_t v)
{
  tickLSS();

  switch (s) {
  case 0x00: // change stepper motor phase
    break;
  case 0x01:
    setPhase(0);
    break;
  case 0x02:
    break;
  case 0x03:
    setPhase(1);
    break;
  case 0x04:
    break;
  case 0x05:
    setPhase(2);
    break;
  case 0x06: // 3 off
    break;
  case 0x07: // 3 on
    setPhase(3);
    break;

  case 0x08: // drive off
    driveOff();
    break;
  case 0x09: // drive on
    driveOn();
    break;

  case 0x0A: // select drive 1
    select(0);
    break;
  case 0x0B: // select drive 2
    select(1);
    break;

  case 0x0C: // Q6 off: shift/write
    q6 = false;
    readOrWriteByte(true);
    break;

  case 0x0D: // Q6 on: load
    q6 = true;
    break;

  case 0x0E: // set read mode
    setWriteMode(false);
    break;

  case 0x0F: // set write mode
    setWriteMode(true);
    break;
  }

  // All writes update the latch
  if (writeMode) {
    readWriteLatch = v;
  }
}

/* The Disk ][ has a stepper motor that moves the head across the tracks.
 * Switches 0-7 turn off and on the four different magnet phases; pulsing 
 * from (e.g.) phase 0 to phase 1 makes the motor move up a track, and 
 * (e.g.) phase 1 to phase 0 makes the motor move down a track.
 *
 * Except that's not quite true: the stepper actually moves the head a 
 * _half_ track.
 *
 * This is a very simplified version of the stepper motor code. In theory, 
 * we should keep track of all 4 phase magnets; and then only move up or down
 * a half track when two adjacent motors are on (not three adjacent motors;
 * and not two opposite motors). But that physical characteristic isn't 
 * important for most diskettes, and our image formats aren't likely to 
 * be able to provide appropriate half-track data to the programs that played 
 * tricks with these half-tracks (for copy protection or whatever).
 * 
 * This setPhase is only called when turning *on* a phase. It's assumed that 
 * something is turning *off* the phases correctly; and that the combination 
 * of the previous phase that was on and the current phase that's being turned
 * on are reliable enough to determine direction.
 *
 * The _phase_delta array is four sets of offsets - one for each
 * current phase, detailing what the step will be given the next
 * phase.  This kind of emulates the messiness of going from phase 0
 * to 2 -- it's going to move forward two half-steps -- but then doing
 * the same thing again is just going to move you back two half-steps...
 *
 */

void DiskII::setPhase(uint8_t phase)
{
  const int8_t _phase_delta[16] = {  0,  1,  2, -1, // prev phase 0 -> 0/1/2/3
				    -1,  0,  1,  2, // prev phase 1 -> 0/1/2/3
				    -2, -1,  0,  1, // prev phase 2 -> 0/1/2/3
				     1, -2, -1,  0  // prev phase 3 -> 0/1/2/3
  };

  int8_t prevPhase = curPhase[selectedDisk];
  int8_t prevHalfTrack = curHalfTrack[selectedDisk];


  curHalfTrack[selectedDisk] += _phase_delta[(prevPhase * 4) + phase];
  curPhase[selectedDisk] = phase;

  // Cap at 35 tracks (a normal disk size). Some drives let you go farther, 
  // and we could support that by increasing this limit - but the images 
  // would be different too, so there would be more work to abstract out...
  if (curHalfTrack[selectedDisk] > 35 * 2 - 1) {
    curHalfTrack[selectedDisk] = 35 * 2 - 1;
  }

  // Don't go past the innermost track, of course.
  if (curHalfTrack[selectedDisk] < 0) {
    curHalfTrack[selectedDisk] = 0;
    // recalibrate! This is where the fun noise goes DaDaDaDaDaDaDaDaDa
  }

  if (curHalfTrack[selectedDisk] != prevHalfTrack) {
    if (disk[selectedDisk]) {
      curWozTrack[selectedDisk] = disk[selectedDisk]->dataTrackNumberForQuarterTrack(curHalfTrack[selectedDisk]*2);
    } else {
      curWozTrack[selectedDisk] = 0;
    }
  }
}

bool DiskII::isWriteProtected()
{
  return (writeProt ? 0xFF : 0x00);
}

// DOS 3.3 Logic State Sequencer ROM, transcribed from UTA2E Fig 9.11
// (p.9-20). Each entry is (next_state << 4) | command. Column index
// encodes the three ROM address inputs:
//   col = (Q6 ? 4 : 0) | (QA << 1) | (RP ? 0 : 1)
// so col 0..3 are the SHIFT (Q6=0) quadrant and col 4..7 are LOAD
// (Q6=1). Within each quadrant QA' (=0) comes before QA (=1), and
// within each QA pair RP comes before NO RP.
// Commands (low nibble): 0-7 CLR, 8/C NOP, 9 SL0, A/E SR (shift
// write-protect right), B/F LD (load from data bus), D SL1.
//
// Key path from UTA2E p.9-29 narrative: after QA sets, the read
// pulse transitions state 2 (QA WAIT) to state 0 via 08-NOP, then
// "3 CP NOP" walks states 0, 1, 3 using col 3 (SHIFT, QA, NO RP).
// That requires row 0, col 3 = 0x18 so state 0 advances to state 1
// rather than looping back — this is the DOS 3.3 change from DOS
// 3.2's 0x08 that makes tight-packed bytes eventually CLR at state
// C. Get that one cell wrong and the whole read sequence deadlocks
// on the first complete byte.
static const uint8_t lssReadRom[16][8] = {
  {0x18, 0x18, 0x18, 0x18, 0x0A, 0x0A, 0x0A, 0x0A}, // State 0
  {0x2D, 0x2D, 0x38, 0x38, 0x0A, 0x0A, 0x0A, 0x0A}, // State 1
  {0xD8, 0x38, 0x08, 0x28, 0x0A, 0x0A, 0x0A, 0x0A}, // State 2 (QA WAIT at col 3)
  {0xD8, 0x48, 0xD8, 0x48, 0x0A, 0x0A, 0x0A, 0x0A}, // State 3
  {0xD8, 0x58, 0xD8, 0x58, 0x0A, 0x0A, 0x0A, 0x0A}, // State 4
  {0xD8, 0x68, 0xD8, 0x68, 0x0A, 0x0A, 0x0A, 0x0A}, // State 5
  {0xD8, 0x78, 0xD8, 0x78, 0x0A, 0x0A, 0x0A, 0x0A}, // State 6
  {0xD8, 0x88, 0xD8, 0x88, 0x0A, 0x0A, 0x0A, 0x0A}, // State 7
  {0xD8, 0x98, 0xD8, 0x98, 0x0A, 0x0A, 0x0A, 0x0A}, // State 8
  {0xD8, 0x29, 0xD8, 0xA8, 0x0A, 0x0A, 0x0A, 0x0A}, // State 9
  {0xCD, 0xBD, 0xD8, 0xB8, 0x0A, 0x0A, 0x0A, 0x0A}, // State A
  {0xD9, 0x59, 0xD8, 0xC8, 0x0A, 0x0A, 0x0A, 0x0A}, // State B
  {0xD9, 0xD9, 0xD8, 0xA0, 0x0A, 0x0A, 0x0A, 0x0A}, // State C (A0-CLR at col 3)
  {0xD8, 0x08, 0xE8, 0xE8, 0x0A, 0x0A, 0x0A, 0x0A}, // State D
  {0xFD, 0xFD, 0xF8, 0xF8, 0x0A, 0x0A, 0x0A, 0x0A}, // State E
  {0xDD, 0x4D, 0xE0, 0xE0, 0x0A, 0x0A, 0x0A, 0x0A}  // State F
};

void DiskII::tickLSS()
{
  // Full LSS simulation per UTA2E Chapter 9. The sequencer ROM is
  // clocked at ~2 MHz — eight LSS clocks per WOZ bit (which is 4 µs
  // wide). Within a bit, a '1' produces a read pulse on the first
  // LSS clock and no pulse on the remaining seven; a '0' has no
  // pulse at all. Each clock looks up (state, Q6, QA, RP) in the
  // ROM and runs the returned command, which naturally produces the
  // MSB "byte flag" hold-and-auto-clear behavior described in UTA2E
  // p.9-26/9-29.
  if (!disk[selectedDisk]) return;
  if (writeMode) return;
  if (diskIsSpinningUntil[selectedDisk] == NOTSPINNING) return;
  if (diskIsSpinningUntil[selectedDisk] != SPINFOREVER &&
      diskIsSpinningUntil[selectedDisk] < g_cpu->cycles) return;
  // Head parked between tracks (TMAP 0xFF). Woz::nextDiskBit would
  // fault; let the next valid track catch up when we land.
  if (curWozTrack[selectedDisk] == 0xFF) return;

  int64_t bitsToDeliver = calcExpectedBits();
  while (bitsToDeliver > 0) {
    uint8_t bit = disk[selectedDisk]->nextDiskBit(curWozTrack[selectedDisk]);
    for (uint8_t sub = 0; sub < 8; sub++) {
      uint8_t rp = (sub == 0 && bit) ? 1 : 0;
      uint8_t qa = (sequencer & 0x80) ? 1 : 0;
      uint8_t col = (q6 ? 4 : 0) | (qa << 1) | (rp ? 0 : 1);
      uint8_t rom = lssReadRom[lssState][col];
      lssState = rom >> 4;
      uint8_t cmd = rom & 0x0F;
      if (!(cmd & 0x08)) {
	sequencer = 0;                                // CLR
      } else {
	switch (cmd & 0x07) {
	case 0: case 4: break;                        // NOP (8, C)
	case 1: sequencer <<= 1; break;               // SL0 (9)
	case 5: sequencer = (sequencer << 1) | 1;     // SL1 (D)
	  break;
	case 2: case 6:                               // SR (A, E)
	  sequencer = (sequencer >> 1) |
	              (isWriteProtected() ? 0x80 : 0x00);
	  break;
	case 3: case 7:                               // LD (B, F)
	  sequencer = readWriteLatch;
	  break;
	}
      }
    }
    bitsToDeliver--;
    deliveredDiskBits[selectedDisk]++;
  }
}

int64_t DiskII::calcExpectedBits()
{
  // If the disk isn't spinning, then it can't be expected to deliver data
  if (diskIsSpinningUntil[selectedDisk]==NOTSPINNING)
    return 0;

  int64_t cyclesPassed = g_cpu->cycles - driveSpinupCycles[selectedDisk];
  // This constant defines how fast the disk drive "spins".
  // 4.0 is good for DOS 3.3 writes, and reads as 205ms in
  //   Copy 2+'s drive speed verifier.
  // 3.99: 204.5ms
  // 3.90: 199.9ms
  // 3.91: 200.5ms
  // 3.51: 176ms, and is too fast for DOS to write properly.
  //
  // As-is, this won't read NIB files for some reason I haven't
  // fully understood; but if you slow the disk down to /5.0,
  // then they load?
  int64_t expectedDiskBits = (float)cyclesPassed / 3.90;

  return expectedDiskBits - deliveredDiskBits[selectedDisk];
}

void DiskII::setWriteMode(bool enable)
{
  if (enable) {
    // At this point we need to update the track pointer so we know
    // where we're going to start writing bits.

    int64_t db = calcExpectedBits();
    if (db > 0) {
      // make sure the disk is at the right point for our program counter's time
      // before we start writing data.
      deliveredDiskBits[selectedDisk] += db;
      while (db) {
	sequencer <<= 1;
	sequencer |= disk[selectedDisk]->nextDiskBit(curWozTrack[selectedDisk]);
	db--;
      }
    }
  }
  writeMode = enable;
}

static uint8_t _lc(char c)
{
  if (c >= 'A' && c <= 'Z') {
    c = c - 'A' + 'a';
  }
  return c;
}

static bool _endsWithI(const char *s1, const char *s2)
{
  if (strlen(s2) > strlen(s1)) {
    return false;
  }
  
  const char *p = &s1[strlen(s1)-1];
  int16_t l = strlen(s2)-1;
  while (l >= 0) {
    if (_lc(*p--) != _lc(s2[l]))
      return false;
    l--;
  }
  return true;
}

void DiskII::insertDisk(int8_t driveNum, const char *filename, bool drawIt)
{
  ejectDisk(driveNum);

  disk[driveNum] = new WozSerializer();
  if (!disk[driveNum]->readFile(filename, true, T_AUTO)) {
    delete disk[driveNum];
    disk[driveNum] = NULL;
    return;
  }

  curWozTrack[driveNum] = disk[driveNum]->dataTrackNumberForQuarterTrack(curHalfTrack[driveNum]*2);

  if (drawIt)
    g_ui->drawOnOffUIElement(UIeDisk1_state + driveNum, false);
}

void DiskII::ejectDisk(int8_t driveNum)
{
  if (disk[driveNum]) {
    disk[driveNum]->flush();
    flushAt[driveNum] = 0;
    delete disk[driveNum];
    disk[driveNum] = NULL;
    g_ui->drawOnOffUIElement(UIeDisk1_state + driveNum, true);
  }
}

void DiskII::select(int8_t which)
{
  if (which != 0 && which != 1)
    return;

  if (which != selectedDisk) {
    if (diskIsSpinningUntil[selectedDisk] == SPINFOREVER) {
      // FIXME: I'm not sure what the right behavior is here (read
      // UTA2E and see if the state diagrams show the right
      // behavior). This spins it down immediately based on something
      // I read about the duoDisk not having both motors on
      // simultaneously.
      diskIsSpinningUntil[selectedDisk] = NOTSPINNING;
      // FIXME: consume any disk bits that need to be consumed, and
      // spin it down
      g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false);

      // Spin up the other one though
      diskIsSpinningUntil[which] = SPINFOREVER;
      g_ui->drawOnOffUIElement(UIeDisk1_activity + which, true);
    }
    
    // Queue flushing the cache of the disk that's no longer selected
    if (disk[selectedDisk]) {
      flushAt[selectedDisk] = g_cpu->cycles + FLUSHDELAY;
      if (flushAt[selectedDisk] == 0)
	flushAt[selectedDisk] = 1; // fudge magic number; 0 is "don't flush"
    }
    
    // set the selected disk drive
    selectedDisk = which;

    // FIXME: does this reset the sequencer?
    sequencer = 0;
  }

  // Update the current woz track for the given disk drive
  if (disk[selectedDisk]) {
    curWozTrack[selectedDisk] =
      disk[selectedDisk]->dataTrackNumberForQuarterTrack(curHalfTrack[selectedDisk]*2);
  }
}

uint8_t DiskII::readOrWriteByte(bool allowOvershoot)
{
  if (!disk[selectedDisk]) {
    return 0xFF;
  }

  if (diskIsSpinningUntil[selectedDisk] != SPINFOREVER &&
      diskIsSpinningUntil[selectedDisk] < g_cpu->cycles) {
    return sequencer;
  }
  // Head parked between tracks (unmapped QT in TMAP). Real hardware would
  // read noise here; leave sequencer alone so Woz::nextDiskBit doesn't
  // fault on datatrack 0xFF.
  if (curWozTrack[selectedDisk] == 0xFF) {
    return sequencer;
  }

  if (writeMode) {
    if (!writeProt) {
      // Write requests from DOS 3.3 start with 40 self-sync bytes
      // (cf. Beneath Apple DOS, p.3-8 and 3-9). These 0xFF bytes are
      // written in a 40-cycle loop, where a bit is written every 4
      // cycles; it intentionally lets 2 0-bits slip in there to
      // provide the self-sync pattern. Lay down 0-bits for whatever
      // time has passed, then the byte from the latch.
      int64_t expectedBits = calcExpectedBits();
      while (expectedBits > 0) {
	disk[selectedDisk]->writeNextWozBit(curWozTrack[selectedDisk], 0);
	expectedBits--;
	deliveredDiskBits[selectedDisk]++;
      }
      disk[selectedDisk]->writeNextWozByte(curWozTrack[selectedDisk], readWriteLatch);
      deliveredDiskBits[selectedDisk] += 8;
    }
    // Write-protected writes do nothing (real hardware sees the write
    // but the media rejects it); either way, don't fall through to the
    // read path.
    return sequencer;
  }

  // tickLSS() has run the LSS state machine up to the current cycle;
  // sequencer already holds what real hardware would have on the data
  // bus. The allowOvershoot parameter is vestigial (retained for the
  // interface) — the LSS's natural byte-flag hold-and-clear replaces
  // the old overshoot cheat.
  (void)allowOvershoot;
  return sequencer;
}

const char *DiskII::DiskName(int8_t num)
{
  if (disk[num]) {
    return disk[num]->diskName();
  }

  // Nothing inserted in that drive
  return "";
}

void DiskII::loadROM(uint8_t *toWhere)
{
#ifdef TEENSYDUINO
  println("loading DiskII rom");
  for (uint16_t i=0; i<=0xFF; i++) {
    toWhere[i] = pgm_read_byte(&romData[i]);
  }
#else
  printf("loading DiskII rom\n");
  memcpy(toWhere, romData, 256);
#endif
}

void DiskII::maintenance(int64_t cycle)
{
  // Handle spin-down for the drive. Drives stay on for a second after
  // the stop was noticed.
  for (int i=0; i<2; i++) {
    if (diskIsSpinningUntil[i] != SPINFOREVER && 
	g_cpu->cycles > diskIsSpinningUntil[i]) {
      // Stop the given disk drive spinning
      diskIsSpinningUntil[i] = NOTSPINNING;
      // FIXME: consume any disk bits that need to be consumed, and spin it down
      g_ui->drawOnOffUIElement(UIeDisk1_activity + i, false);
    }

    if (flushAt[i] &&
	g_cpu->cycles > flushAt[i]) {
      if (disk[i]) {
	disk[i]->flush();
      }
      flushAt[i] = 0;
    }

  }
}

uint8_t DiskII::selectedDrive()
{
  return selectedDisk;
}

uint8_t DiskII::headPosition(uint8_t drive)
{
  return curHalfTrack[drive];
}


