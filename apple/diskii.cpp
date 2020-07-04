#include "diskii.h"

#ifdef TEENSYDUINO
#include <Arduino.h>
#include "teensy-println.h"
#else
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#endif

#include "applemmu.h" // for FLOATING

#include "globals.h"
#include "appleui.h"

#include "diskii-rom.h"

#define DISKIIMAGIC 0xAA

// how many CPU cycles do we wait to spin down the disk drive? 1023000 == 1 second
#define SPINDOWNDELAY (1023000)

// 10 second delay before flushing
#define FLUSHDELAY (1023000 * 10)

DiskII::DiskII(AppleMMU *mmu)
{
  this->mmu = mmu;

  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;
  curWozTrack[0] = curWozTrack[1] = 0xFF;

  writeMode = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;
  sequencer = 0;
  dataRegister = 0;
  driveSpinupCycles[0] = driveSpinupCycles[1] = 0;
  deliveredDiskBits[0] = deliveredDiskBits[1] = 0;

  disk[0] = disk[1] = NULL;
  diskIsSpinningUntil[0] = diskIsSpinningUntil[1] = 0;
  flushAt[0] = flushAt[1] = 0;
  selectedDisk = 0;
}

DiskII::~DiskII()
{
}

bool DiskII::Serialize(int8_t fd)
{
  uint8_t buf[23] = { DISKIIMAGIC,
		      readWriteLatch,
		      sequencer,
		      dataRegister,
		      writeMode,
		      writeProt,
		      selectedDisk };
  
  if (g_filemanager->write(fd, buf, 7) != 7) {
    return false;
  }

  for (int i=0; i<2; i++) {
    uint8_t ptr = 0;
    buf[ptr++] = curHalfTrack[i];
    buf[ptr++] = curWozTrack[i];
    buf[ptr++] = curPhase[i];
    buf[ptr++] = ((driveSpinupCycles[i] >> 56) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >> 48) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >> 40) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >> 32) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >> 24) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >> 16) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i] >>  8) & 0xFF);
    buf[ptr++] = ((driveSpinupCycles[i]      ) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 56) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 48) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 40) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 32) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 24) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >> 16) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i] >>  8) & 0xFF);
    buf[ptr++] = ((deliveredDiskBits[i]      ) & 0xFF);
    buf[ptr++] = (diskIsSpinningUntil[i] >> 24) & 0xFF;
    buf[ptr++] = (diskIsSpinningUntil[i] >> 16) & 0xFF;
    buf[ptr++] = (diskIsSpinningUntil[i] >>  8) & 0xFF;
    buf[ptr++] = (diskIsSpinningUntil[i]      ) & 0xFF;
    // Safety check: keeping the hard-coded 23 and comparing against ptr.
    // If we change the 23, also need to change the size of buf[] above
    if (g_filemanager->write(fd, buf, 23) != ptr) {
      return false;
    }
    
    if (disk[i]) {
      // Make sure we have flushed the disk images
      disk[i]->flush();
      flushAt[i] = 0; // and there's no need to re-flush them now

      buf[0] = 1;
      if (g_filemanager->write(fd, buf, 1) != 1)
	return false;

      // FIXME: this ONLY works for builds using the filemanager to read
      // the disk image, so it's broken until we port Woz to do that!
      const char *fn = disk[i]->diskName();
      if (g_filemanager->write(fd, fn, strlen(fn)+1) != strlen(fn)+1)  // include null terminator
	return false;
      if (!disk[i]->Serialize(fd))
	return false;
    } else {
      buf[0] = 0;
      if (g_filemanager->write(fd, buf, 0) != 1)
	return false;
    }
  }

  buf[0] = DISKIIMAGIC;
  if (g_filemanager->write(fd, buf, 1) != 1)
    return false;

  return true;
}

bool DiskII::Deserialize(int8_t fd)
{
  uint8_t buf[MAXPATH];
  if (g_filemanager->read(fd, buf, 7) != 7)
    return false;
  if (buf[0] != DISKIIMAGIC)
    return false;

  readWriteLatch = buf[1];
  sequencer = buf[2];
  dataRegister = buf[3];
  writeMode = buf[4];
  writeProt = buf[5];
  selectedDisk = buf[6];

  for (int i=0; i<2; i++) {
    uint8_t ptr = 0;
    if (g_filemanager->read(fd, buf, 23) != 23)
      return false;

    curHalfTrack[i] = buf[ptr++];
    curWozTrack[i] = buf[ptr++];
    curPhase[i] = buf[ptr++];

    driveSpinupCycles[i] = buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= buf[ptr++];

    deliveredDiskBits[i] = buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= buf[ptr++];

    diskIsSpinningUntil[i] = buf[ptr++];
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= buf[ptr++];
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= buf[ptr++];
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= buf[ptr++];
    
    if (disk[i])
      delete disk[i];
    if (g_filemanager->read(fd, buf, 1) != 1)
      return false;
    if (buf[0]) {
      disk[i] = new WozSerializer();

      ptr = 0;
      // FIXME: MAXPATH check!
      while (1) {
	if (g_filemanager->read(fd, &buf[ptr++], 1) != 1)
	  return false;
	if (buf[ptr-1] == 0)
	  break;
      }
      if (buf[0]) {
	// Important we don't read all the tracks, so we can also flush
	// writes back to the fd...
	disk[i]->readFile((char *)buf, false, T_AUTO); // FIXME error checking    
      } else {
	// ERROR: there's a disk but we don't have the path to its image?
	return false;
      }
      
      if (!disk[i]->Deserialize(fd))
	return false;
    } else {
      disk[i] = NULL;
    }
  }

  if (g_filemanager->read(fd, buf, 1) != 1)
    return false;
  if (buf[0] != DISKIIMAGIC)
    return false;

  return true;
}

void DiskII::Reset()
{
  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;

  writeMode = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;

  ejectDisk(0);
  ejectDisk(1);
}

void DiskII::driveOff()
{
  if (diskIsSpinningUntil[selectedDisk] == -1) {
    diskIsSpinningUntil[selectedDisk] = g_cpu->cycles + SPINDOWNDELAY; // 1 second lag
    if (diskIsSpinningUntil[selectedDisk] == -1 ||
	diskIsSpinningUntil[selectedDisk] == 0)
      diskIsSpinningUntil[selectedDisk] = 2; // fudge magic numbers; 0 is "off" and -1 is "forever".

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
  if (diskIsSpinningUntil[selectedDisk] != -1) {
    // If the drive isn't already spinning, then start keeping track of how
    // many bits we've delivered (so we can honor the disk bit-delivery time
    // that might be in the Woz disk image).
    driveSpinupCycles[selectedDisk] = g_cpu->cycles;
    deliveredDiskBits[selectedDisk] = 0;
    diskIsSpinningUntil[selectedDisk] = -1; // magic "forever"
  }
  // FIXME: does the sequencer get reset? Maybe if it's the selected disk? Or no?
  // sequencer = 0;

  g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true); // FIXME: do we really want to update the UI from inside this thread?
}

uint8_t DiskII::readSwitches(uint8_t s)
{
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

  case 0x0C: // shift one read or write byte
    readWriteLatch = readOrWriteByte();
    if (readWriteLatch & 0x80) {
      //      static uint32_t lastC = 0;
      //      printf("%u: read data\n", g_cpu->cycles - lastC);
      //      lastC = g_cpu->cycles;
      if (!(sequencer & 0x80)) {
	//	printf("SEQ RESET EARLY [1]\n");
      }
      sequencer = 0;
    }
    break;

  case 0x0D: // load data register (latch)
    // This is complex and incomplete. cf. Logic State Sequencer, 
    // UTA2E, p. 9-14
    if (!writeMode) {
      if (isWriteProtected())
	readWriteLatch |= 0x80;
      else
	readWriteLatch &= 0x7F;
    }
    if (!(sequencer & 0x80)) {
      //      printf("SEQ RESET EARLY [2]\n");
    }
    sequencer = 0;
    break;

  case 0x0E: // set read mode
    setWriteMode(false);
    break;
  case 0x0F: // set write mode
    setWriteMode(true);
    break;
  }

  // Any even address read returns the readWriteLatch (UTA2E Table 9.1,
  // p. 9-12, note 2)
  return (s & 1) ? FLOATING : readWriteLatch;
}

void DiskII::writeSwitches(uint8_t s, uint8_t v)
{
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

  case 0x0C: // shift one read or write byte
    if (readOrWriteByte() & 0x80) {
      if (!(sequencer & 0x80)) {
	//	printf("SEQ RESET EARLY [3]\n");
      }
      sequencer = 0;
    }
    break;

  case 0x0D: // drive write
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

int64_t DiskII::calcExpectedBits()
{
  // If the disk isn't spinning, then it can't be expected to deliver data
  if (!diskIsSpinningUntil[selectedDisk])
    return 0;

  // Handle potential messy counter rollover
  if (driveSpinupCycles[selectedDisk] > g_cpu->cycles) {
    driveSpinupCycles[selectedDisk] = g_cpu->cycles-1;
    if (driveSpinupCycles[selectedDisk] == 0) // avoid sitting on 0
      driveSpinupCycles[selectedDisk]++;
  }

  uint32_t cyclesPassed = g_cpu->cycles - driveSpinupCycles[selectedDisk];
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
  uint64_t expectedDiskBits = (float)cyclesPassed / 3.90;

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
  // intentionally 'false' (see above call to readFile)
  if (!disk[driveNum]->readFile(filename, false, T_AUTO)) {
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
    if (diskIsSpinningUntil[selectedDisk] == -1) {
      // FIXME: I'm not sure what the right behavior is here (read
      // UTA2E and see if the state diagrams show the right
      // behavior). This spins it down immediately based on something
      // I read about the duoDisk not having both motors on
      // simultaneously.
      diskIsSpinningUntil[selectedDisk] = 0;
      // FIXME: consume any disk bits that need to be consumed, and
      // spin it down
      g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: queue for later drawing?

      // Spin up the other one though
      diskIsSpinningUntil[which] = -1;
      g_ui->drawOnOffUIElement(UIeDisk1_activity + which, false); // FIXME: queue for later drawing?
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

uint8_t DiskII::readOrWriteByte()
{
  if (!disk[selectedDisk]) {
    //printf("reading from uninserted disk\n");
    return 0xFF;
  }

  int32_t bitsToDeliver;
  
  if (diskIsSpinningUntil[selectedDisk] < g_cpu->cycles) {
    // Uum, disk isn't spinning?
    goto done;
  }

  bitsToDeliver = calcExpectedBits();
  
  if (writeMode && !writeProt) {
    // It's a write request.

    // Write requests from DOS 3.3 start with 40 self-sync bytes
    // (cf. Beneath Apple DOS, p.3-8 and 3-9). These 0XFF bytes are
    // written in a 40-cycle loop, where a bit is written every 4
    // cycles; it intentionally lets 2 0-bits slip in there to
    // provide the self-sync pattern.
    //
    // So the timing here is important. Figure out how many bits
    // should have been laid down to the track, and those are 0s.

    int64_t expectedBits = calcExpectedBits();
    while (expectedBits > 0) {
      disk[selectedDisk]->writeNextWozBit(curWozTrack[selectedDisk], 0);
      expectedBits--;
      deliveredDiskBits[selectedDisk]++;
    }

    disk[selectedDisk]->writeNextWozByte(curWozTrack[selectedDisk], readWriteLatch);
    deliveredDiskBits[selectedDisk] += 8;
    goto done;
  }

  if (bitsToDeliver > 0) {
    // We're expected to deliver some bits to the Disk II sequencer.
    // Instead of piecemeal delivering a small number of bits (which we
    // could do, but it's kinda busywork) - instead, we'll do one of two
    // possible things.
    //
    // The first: if we're expecting a small number of bits to be delivered,
    // then we'll grab the next byte from the nibble stream and return it.
    // This itself has three possible cases -
    //   (a) we should be delivering less than a full byte, but we're
    //       actually going to deliver a full byte. bitsToDeliver will
    //       become negative, because we're delivering these too early.
    //       The next call will probably see that it has nothing to deliver
    //       and, as long as the disk image we're using doesn't have a
    //       really fine tolerance on the delivery rate of the bits,
    //       it will all come out in the wash.
    //   (b) we should be delivering exactly a byte, and we're doing the
    //       absolute right thing.
    //   (c) we are more than 1 byte, but less than 2 bytes, behind. If
    //       this is the case, we're probably making up for a timing
    //       problem in this code - where the bits would now have been
    //       lost. By returning the first byte that we found, we're hoping
    //       that the next call will be closer to on time, and we will
    //       eventually catch back up to the stream. Hopefully this makes
    //       the stream a little more resilient - and the error isn't
    //       so far off that the reader notices something is weird on the
    //       timing. (Standard RWTS doesn't, but some copy protection
    //       might.)
    if (bitsToDeliver < 16) {
      while (bitsToDeliver > -16 && ((sequencer & 0x80) == 0)) {
	sequencer <<= 1;
	sequencer |= disk[selectedDisk]->nextDiskBit(curWozTrack[selectedDisk]);
	bitsToDeliver--;
	deliveredDiskBits[selectedDisk]++;
      }
      goto done;
    }

    // If we reach here, we're throwing away a bunch of missed data.
    // This might be normal (where the machine wasn't listening for the data),
    // or it might be exceptional (something wrong with the tuning of data
    // delivery, based on the magic constant in expectedDiskBits above)...
    deliveredDiskBits[selectedDisk] += bitsToDeliver;
    while (bitsToDeliver) {
      sequencer <<= 1;
      sequencer |= disk[selectedDisk]->nextDiskBit(curWozTrack[selectedDisk]);
      bitsToDeliver--;
    }
  }

    
 done:
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

void DiskII::maintenance(uint32_t cycle)
{
  // Handle spin-down for the drive. Drives stay on for a second after
  // the stop was noticed.
  for (int i=0; i<2; i++) {
    if (diskIsSpinningUntil[i] && 
	g_cpu->cycles > diskIsSpinningUntil[i]) {
      // Stop the given disk drive spinning
      diskIsSpinningUntil[i] = 0;
      // FIXME: consume any disk bits that need to be consumed, and spin it down
      g_ui->drawOnOffUIElement(UIeDisk1_activity + i, false); // FIXME: queue for later drawing?
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


