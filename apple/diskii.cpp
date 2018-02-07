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

DiskII::DiskII(AppleMMU *mmu)
{
  this->trackBuffer = new LRingBuffer(NIBTRACKSIZE);

  this->mmu = mmu;

  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;

  trackToFlush = -1;

  writeMode = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;

  disk[0] = disk[1] = -1;
  indicatorIsOn[0] = indicatorIsOn[1] = 0;
  selectedDisk = 0;
  diskType[0] = diskType[1] = dosDisk;
}

DiskII::~DiskII()
{
  delete this->trackBuffer; this->trackBuffer = NULL;
}

bool DiskII::Serialize(int8_t fd)
{
  /* Make sure to flush anything to disk first */
  checkFlush(curHalfTrack[selectedDisk]>>1);

  g_filemanager->writeByte(fd, DISKIIMAGIC);

  g_filemanager->writeByte(fd, curHalfTrack[0]);
  g_filemanager->writeByte(fd, curHalfTrack[1]);

  g_filemanager->writeByte(fd, curPhase[0]);
  g_filemanager->writeByte(fd, curPhase[1]);

  g_filemanager->writeByte(fd, readWriteLatch);
  g_filemanager->writeByte(fd, writeMode);
  g_filemanager->writeByte(fd, writeProt);

  // Don't save disk[0,1]; save their file names & cursors
  g_filemanager->SerializeFile(fd, disk[0]);
  g_filemanager->SerializeFile(fd, disk[1]);

  g_filemanager->writeByte(fd, indicatorIsOn[0]);
  g_filemanager->writeByte(fd, indicatorIsOn[1]);

  g_filemanager->writeByte(fd, diskType[0]);
  g_filemanager->writeByte(fd, diskType[1]);
  g_filemanager->writeByte(fd, selectedDisk);

  trackBuffer->Serialize(fd);

  g_filemanager->writeByte(fd, DISKIIMAGIC);

  return true;
}

bool DiskII::Deserialize(int8_t fd)
{
  /* Make sure to flush anything to disk first */
  checkFlush(curHalfTrack[selectedDisk]>>1);

  if (g_filemanager->readByte(fd) != DISKIIMAGIC) {
    return false;
  }

  curHalfTrack[0] = g_filemanager->readByte(fd);
  curHalfTrack[1] = g_filemanager->readByte(fd);

  curPhase[0] = g_filemanager->readByte(fd);
  curPhase[1] = g_filemanager->readByte(fd);

  readWriteLatch = g_filemanager->readByte(fd);
  writeMode = g_filemanager->readByte(fd);
  writeProt = g_filemanager->readByte(fd);

  disk[0] = g_filemanager->DeserializeFile(fd);
  disk[1] = g_filemanager->DeserializeFile(fd);

  indicatorIsOn[0] = g_filemanager->readByte(fd);
  indicatorIsOn[1] = g_filemanager->readByte(fd);

  diskType[0] = g_filemanager->readByte(fd);
  diskType[1] = g_filemanager->readByte(fd);

  selectedDisk = g_filemanager->readByte(fd);

  trackBuffer->Deserialize(fd);

  // Reset the dirty caches and whatnot
  trackToFlush = -1;

  if (g_filemanager->readByte(fd) != DISKIIMAGIC) {
    return false;
  }

  return true;
}

void DiskII::Reset()
{
  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;

  trackToFlush = -1;

  writeMode = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;

  ejectDisk(0);
  ejectDisk(1);
}

// FIXME: why does this need an argument?
void DiskII::checkFlush(int8_t track)
{
  if (trackToFlush != -1) {
    flushTrack(trackToFlush, selectedDisk);
    trackToFlush = -1;
  }
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
    indicatorIsOn[selectedDisk] = 99;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: delay a bit? Queue for later drawing?
    checkFlush(curHalfTrack[selectedDisk]>>1);
    break;
  case 0x09: // drive on
    indicatorIsOn[selectedDisk] = 100;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true); // FIXME: delay a bit? Queue for later drawing?
    break;

  case 0x0A: // select drive 1
    select(0);
    break;
  case 0x0B: // select drive 2
    select(1);
    break;

  case 0x0C: // shift one read or write byte
    readWriteLatch = readOrWriteByte();
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
    break;

  case 0x0E: // set read mode
    setWriteMode(false);
    break;
  case 0x0F: // set write mode
    setWriteMode(true);
    break;
  }

  // FIXME: improve the spin-down here. We need a CPU cycle callback
  // for some period of time instead of this silly decrement counter.
  if (!indicatorIsOn[selectedDisk]) {
    //    printf("Unexpected read while disk isn't on?\n");
    indicatorIsOn[selectedDisk] = 100;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true); // FIXME: queue for later drawing?
  }
  if (indicatorIsOn[selectedDisk] > 0 && indicatorIsOn[selectedDisk] < 100) {
    // slowly spin it down...
    if (--indicatorIsOn[selectedDisk] == 0) {
      g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: queue for later drawing?
    }

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
    break;
  case 0x09: // drive on
    break;

  case 0x0A: // select drive 1
    select(0);
    break;
  case 0x0B: // select drive 2
    select(1);
    break;

  case 0x0C: // shift one read or write byte
    readOrWriteByte();
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

  /*
    printf("phase %d => %d; curHalfTrack %d => %d\n",
    prevPhase, curPhase[selectedDisk],
    prevHalfTrack, curHalfTrack[selectedDisk]);
  */

  if (curHalfTrack[selectedDisk]>>1 != prevHalfTrack>>1) {
    // We're changing track - flush the old track back to disk
    checkFlush(prevHalfTrack>>1);

    // Prime the cache by reading the current track
    if (disk[selectedDisk] != -1) {
      readDiskTrack(selectedDisk, curHalfTrack[selectedDisk]>>1);
    }
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
  disk[driveNum] = g_filemanager->openFile(filename);
  if (drawIt)
    g_ui->drawOnOffUIElement(UIeDisk1_state + driveNum, false);

  if (_endsWithI(filename, ".nib")) {
    diskType[driveNum] = nibDisk;
  } else if (_endsWithI(filename, ".po")) {
    diskType[driveNum] = prodosDisk;
  } else {
    diskType[driveNum] = dosDisk;
#ifndef TEENSYDUINO
    // debugging: make a nib copy of the image to play with
    //    convertDskToNib("/tmp/debug.nib");
#endif
  }

  if (driveNum == selectedDisk) {
    readDiskTrack(selectedDisk, curHalfTrack[selectedDisk]>>1);
  }
}

void DiskII::ejectDisk(int8_t driveNum)
{
  if (disk[driveNum] != -1) {
    if (selectedDisk == driveNum) {
      checkFlush(0); // FIXME: bogus argument
      trackBuffer->clear();
    }
    g_filemanager->closeFile(disk[driveNum]);
    disk[driveNum] = -1;
    g_ui->drawOnOffUIElement(UIeDisk1_state + driveNum, true);
  }
}

void DiskII::select(int8_t which)
{
  if (which != 0 && which != 1)
    return;

  if (which != selectedDisk) {
    indicatorIsOn[selectedDisk] = 0;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: queue for later drawing?

    checkFlush(curHalfTrack[selectedDisk]>>1);

    // set the selected disk drive
    selectedDisk = which;

    // Preread the current track
    if (disk[selectedDisk] != -1) {
      readDiskTrack(selectedDisk, curHalfTrack[selectedDisk]>>1);
    }
  }
}

uint8_t DiskII::readOrWriteByte()
{
  if (disk[selectedDisk] == -1) {
    return GAP;
  }

  if (writeMode && !writeProt) {

    if (!trackBuffer->hasData()) {
      // Error: writing to empty track buffer? That's a raw write w/o
      // knowing where we are on the disk. Dangerous, at very least;
      // I'm not sure what the best action would be here. For the 
      // moment, just refuse to write it.

      g_display->debugMsg("DII: unguarded write");
      return GAP;
    }

    trackToFlush = curHalfTrack[selectedDisk]>>1;
    // It's possible that a badly behaving OS could try to write more
    // data than we have buffer to handle. Don't let it. We should
    // only need something like 500 bytes, at worst. In the typical
    // case, we're talking about something like
    //
    // ~5 bytes of GAP
    // 3 bytes of sector prolog
    // 2 bytes of volume
    // 2 bytes of track
    // 2 bytes of sector
    // 2 bytes of checksum
    // 2 bytes of epilog
    // ~5 bytes of GAP
    // 3 bytes of data prolog
    // 342 bytes of GRP-encoded (6:2) data
    // 1 byte of checksum
    // 3 bytes of epilog
    // 1 byte of GAP
    // == 373 bytes
    //
    // ... so if we get up to the full 1024 we've allocated, there's
    // something suspicious happening.

    if (readWriteLatch < 0x96) {
      // Can't write a de-nibblized byte...
      g_display->debugMsg("DII: bad write");
      return 0;
    }

    trackBuffer->replaceByte(readWriteLatch);

    return 0;
  }

  // return 0x00 every other byte. Helps the logic sequencer stay in sync.
  // Otherwise we wind up waiting long periods of time for it to sync up, 
  // presumably because we're overrunning it (returning data faster than 
  // the actual drive would be able to)?
  static bool whitespace = false;
  if (whitespace) {
    whitespace = false;
    return 0x00;
  }

  whitespace = !whitespace;

  uint8_t ret = trackBuffer->peekNext();
  return ret;
}

void DiskII::readDiskTrack(int8_t diskWeAreUsing, int8_t trackWeAreReading)
{
  checkFlush(trackWeAreReading); // FIXME: bogus argument

  trackBuffer->clear();
  trackBuffer->setPeekCursor(0);

  if (diskType[diskWeAreUsing] == nibDisk) {
    // Read one nibblized sector at a time and jam it in trackBuf
    // directly.  We don't read the whole track at once only because
    // of RAM constraints on the Teensy. There's no reason we
    // couldn't, though, if RAM weren't at a premium.
    
    for (int i=0; i<16; i++) {
      g_filemanager->seekBlock(disk[diskWeAreUsing], trackWeAreReading * 16 + i, true);
      if (!g_filemanager->readBlock(disk[diskWeAreUsing], rawTrackBuffer, true)) {
	// FIXME: error handling?
	g_display->debugMsg("DII: FM nib read failure");
	return;
      }
      trackBuffer->addBytes(rawTrackBuffer, 416);
    }
  } else {
    // It's a .dsk / .po disk image. Read the whole track in to
    // rawTrackBuffer and nibblize it.
    g_filemanager->seekBlock(disk[diskWeAreUsing], trackWeAreReading * 16, false);
    if (!g_filemanager->readTrack(disk[diskWeAreUsing], rawTrackBuffer, false)) {
      // FIXME: error handling?
      g_display->debugMsg("DII: FM block read failure");
      return;
    }

    nibblizeTrack(trackBuffer, rawTrackBuffer, diskType[diskWeAreUsing], curHalfTrack[selectedDisk]>>1);
  }
}

void DiskII::fillDiskBuffer()
{
}

const char *DiskII::DiskName(int8_t num)
{
  if (disk[num] != -1)
    return g_filemanager->fileName(disk[num]);

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

void DiskII::flushTrack(int8_t track, int8_t sel)
{
  // safety check: if we're write-protected, then how did we get here?
  if (writeProt) {
    g_display->debugMsg("DII: Write Protected");
    return;
  }

  if (!trackBuffer->hasData()) {
    // Dunno what happened - we're writing but haven't initialized the sector buffer?
    g_display->debugMsg("DII: uninit'd write");
    return;
  }

  if (diskType[sel] == nibDisk) {
    // Write the whole track out exactly as we've got it. Hopefully
    // someone has re-calcuated appropriate checksums on it...
    g_display->debugMsg("DII: Not writing Nib image");
    return;
  }

  nibErr e = denibblizeTrack(trackBuffer, rawTrackBuffer, diskType[sel], curHalfTrack[selectedDisk]>>1);
  switch (e) {
  case errorShortTrack:
    g_display->debugMsg("DII: short track");
    trackBuffer->clear();
    return;

  case errorMissingSectors:
    // The nibblized track doesn't contain all possible sectors - so it's broken. Drop the write.
    g_display->debugMsg("DII: missing sectors");
    trackBuffer->clear();
    break;

  case errorNone:
    break;
  }

  // ok, write the track!
  g_filemanager->seekBlock(disk[sel], track * 16);
  g_filemanager->writeTrack(disk[sel], rawTrackBuffer);
}

