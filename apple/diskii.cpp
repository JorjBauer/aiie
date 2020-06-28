#include "diskii.h"

#ifdef TEENSYDUINO
#include <Arduino.h>
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
  g_filemanager->writeByte(fd, DISKIIMAGIC);

  g_filemanager->writeByte(fd, readWriteLatch);
  g_filemanager->writeByte(fd, sequencer);
  g_filemanager->writeByte(fd, dataRegister);
  g_filemanager->writeByte(fd, writeMode);
  g_filemanager->writeByte(fd, writeProt);
  g_filemanager->writeByte(fd, selectedDisk);

  for (int i=0; i<2; i++) {
    g_filemanager->writeByte(fd, curHalfTrack[i]);
    g_filemanager->writeByte(fd, curWozTrack[i]);
    g_filemanager->writeByte(fd, curPhase[i]);
    
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 56) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 48) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 40) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 32) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 24) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >> 16) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i] >>  8) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((driveSpinupCycles[i]      ) & 0xFF));
    
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 56) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 48) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 40) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 32) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 24) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >> 16) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i] >>  8) & 0xFF));
    g_filemanager->writeByte(fd,
			     ((deliveredDiskBits[i]      ) & 0xFF));
    
    g_filemanager->writeByte(fd, 
			     (diskIsSpinningUntil[i] >> 24) & 0xFF);
    g_filemanager->writeByte(fd, 
			     (diskIsSpinningUntil[i] >> 16) & 0xFF);
    g_filemanager->writeByte(fd, 
			     (diskIsSpinningUntil[i] >>  8) & 0xFF);
    g_filemanager->writeByte(fd, 
			     (diskIsSpinningUntil[i]      ) & 0xFF);
    
    if (disk[i]) {
      // Make sure we have flushed the disk images
      disk[i]->flush();
      flushAt[i] = 0; // and there's no need to re-flush them now

      g_filemanager->writeByte(fd, 1);
      // FIXME: this ONLY works for builds using the filemanager to read
      // the disk image, so it's broken until we port Woz to do that!
      const char *fn = disk[i]->diskName();
      for (int j=0; j<strlen(fn); j++) {
	g_filemanager->writeByte(fd, fn[j]);
      }
      g_filemanager->writeByte(fd, 0);
      if (!disk[i]->Serialize(fd))
	return false;
    } else {
      g_filemanager->writeByte(fd, 0);
    }
  }
  
  g_filemanager->writeByte(fd, DISKIIMAGIC);

  return true;
}

bool DiskII::Deserialize(int8_t fd)
{
  if (g_filemanager->readByte(fd) != DISKIIMAGIC) {
    return false;
  }
  
  readWriteLatch = g_filemanager->readByte(fd);
  sequencer = g_filemanager->readByte(fd);
  dataRegister = g_filemanager->readByte(fd);
  writeMode = g_filemanager->readByte(fd);
  writeProt = g_filemanager->readByte(fd);
  selectedDisk = g_filemanager->readByte(fd);

  for (int i=0; i<2; i++) {
    curHalfTrack[i] = g_filemanager->readByte(fd);
    curWozTrack[i] = g_filemanager->readByte(fd);
    curPhase[i] = g_filemanager->readByte(fd);

    driveSpinupCycles[i] = g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);
    driveSpinupCycles[i] <<= 8; driveSpinupCycles[i] |= g_filemanager->readByte(fd);

    deliveredDiskBits[i] = g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);
    deliveredDiskBits[i] <<= 8; deliveredDiskBits[i] |= g_filemanager->readByte(fd);

    diskIsSpinningUntil[i] = g_filemanager->readByte(fd);
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= g_filemanager->readByte(fd);
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= g_filemanager->readByte(fd);
    diskIsSpinningUntil[i] <<= 8; diskIsSpinningUntil[i] |= g_filemanager->readByte(fd);
    
    if (disk[i])
      delete disk[i];
    if (g_filemanager->readByte(fd) == 1) {
      disk[i] = new WozSerializer();
      char buf[MAXPATH];
      char c;
      int ptr = 0;
      while ( (c = g_filemanager->readByte(fd) != 0) ) {
	buf[ptr++] = c;
      }
      buf[ptr] = 0;
      if (buf[0]) {
	// Important we don't read all the tracks, so we can also flush
	// writes back to the fd...
	disk[i]->readFile(buf, false, T_AUTO); // FIXME error checking    
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

  if (g_filemanager->readByte(fd) != DISKIIMAGIC) {
    return false;
  }

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
  diskIsSpinningUntil[selectedDisk] = g_cpu->cycles + SPINDOWNDELAY; // 1 second lag
  if (diskIsSpinningUntil[selectedDisk] == -1 ||
      diskIsSpinningUntil[selectedDisk] == 0)
    diskIsSpinningUntil[selectedDisk] = 2; // fudge magic numbers; 0 is "off" and -1 is "forever".

  // The drive-is-on-indicator is turned off later, when the disk
  // actually spins down.

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
    curWozTrack[selectedDisk] = disk[selectedDisk]->dataTrackNumberForQuarterTrack(curHalfTrack[selectedDisk]*2);
  }
}

bool DiskII::isWriteProtected()
{
  return (writeProt ? 0xFF : 0x00);
}

void DiskII::setWriteMode(bool enable)
{
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
  disk[driveNum]->readFile(filename, false, T_AUTO); // FIXME error checking

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
      // behavior). For now, I'm setting the spindown of the
      // now-deselected disk.
      diskIsSpinningUntil[selectedDisk] = g_cpu->cycles + SPINDOWNDELAY;
      if (diskIsSpinningUntil[selectedDisk] == -1 ||
	  diskIsSpinningUntil[selectedDisk] == 0)
	diskIsSpinningUntil[selectedDisk] = 2; // fudge magic numbers; 0 is "off" and -1 is "forever".
    }

    // Queue flushing the cache of the disk that's no longer selected
    if (disk[selectedDisk]) {
      flushAt[selectedDisk] = g_cpu->cycles + FLUSHDELAY;
      if (flushAt[selectedDisk] == 0)
	flushAt[selectedDisk] = 1; // fudge magic number; 0 is "don't flush"
    }
    
    // set the selected disk drive
    selectedDisk = which;
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

  uint32_t curCycles = g_cpu->cycles;

  // FIXME: for writes, we need to check s/t like ... if (diskIsSpinningUntil[selectedDisk] >= curCycles) { return } ...

  if (writeMode && !writeProt) {
    // It's a write request. Inject 'readWriteLatch'.
    disk[selectedDisk]->writeNextWozByte(curWozTrack[selectedDisk], readWriteLatch);
    goto done;
  }

  if (diskIsSpinningUntil[selectedDisk] >= curCycles) {

    // Figure out how many cycles we missed since the last disk read,
    // and pop the right number of bits off the woz track.

    // Handle rollover, which is a mess.
    if (driveSpinupCycles[selectedDisk] > g_cpu->cycles) {
      //      printf("Cycle rollover\n");
      driveSpinupCycles[selectedDisk] = g_cpu->cycles-1; // FIXME: is the -1 correct? What if we were @ 0?
#ifndef TEENSYDUINO
      exit(2); // for debugging, FIXME ***
#endif
    }

    uint32_t cyclesPassed = g_cpu->cycles - driveSpinupCycles[selectedDisk];
    // FIXME: this is a bit of a magic constant, which makes the drive
    // test in Copy2+ at 179.4ms per revolution (334.4rpm). I'd like to
    // understand that better and get to to the proper 200ms (300rpm).
    uint64_t expectedDiskBits = (float) cyclesPassed / 3.51;
    int64_t bitsToDeliver = expectedDiskBits - deliveredDiskBits[selectedDisk];

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
  Serial.println("loading DiskII rom");
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
