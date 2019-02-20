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
  this->mmu = mmu;

  curPhase[0] = curPhase[1] = 0;
  curHalfTrack[0] = curHalfTrack[1] = 0;
  curWozTrack[0] = curWozTrack[1] = 0xFF;

  writeMode = false;
  writeProt = false; // FIXME: expose an interface to this
  readWriteLatch = 0x00;
  sequencer = 0;
  dataRegister = 0;
  lastDiskRead[0] = lastDiskRead[1] = 0;

  disk[0] = disk[1] = NULL;
  indicatorIsOn[0] = indicatorIsOn[1] = 0;
  selectedDisk = 0;
}

DiskII::~DiskII()
{
}

bool DiskII::Serialize(int8_t fd)
{
  return false;

  // FIXME: all the new variables are missing ***

  g_filemanager->writeByte(fd, DISKIIMAGIC);

  g_filemanager->writeByte(fd, curHalfTrack[0]);
  g_filemanager->writeByte(fd, curHalfTrack[1]);

  g_filemanager->writeByte(fd, curPhase[0]);
  g_filemanager->writeByte(fd, curPhase[1]);

  g_filemanager->writeByte(fd, readWriteLatch);
  g_filemanager->writeByte(fd, writeMode);
  g_filemanager->writeByte(fd, writeProt);

  for (int i=0; i<2; i++) {
    if (disk[i]) {
      g_filemanager->writeByte(fd, 1);
    } else {
      g_filemanager->writeByte(fd, 0);
    }
    if (!disk[i]->Serialize(fd))
      return false;
  }

  g_filemanager->writeByte(fd, indicatorIsOn[0]);
  g_filemanager->writeByte(fd, indicatorIsOn[1]);

  g_filemanager->writeByte(fd, selectedDisk);

  g_filemanager->writeByte(fd, DISKIIMAGIC);

  return true;
}

bool DiskII::Deserialize(int8_t fd)
{
  return false;
  // FIXME: all the new variables are missing ***

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

  for (int i=0; i<2; i++) {
    if (disk[i])
      delete disk[i];
    if (g_filemanager->readByte(fd) == 1) {
      disk[i] = new WozSerializer();
      if (!disk[i]->Deserialize(fd))
	return false;
    } else {
      disk[i] = NULL;
    }
  }

  indicatorIsOn[0] = g_filemanager->readByte(fd);
  indicatorIsOn[1] = g_filemanager->readByte(fd);

  selectedDisk = g_filemanager->readByte(fd);

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
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: delay a bit? Queue for later drawing? ***
    break;
  case 0x09: // drive on
    indicatorIsOn[selectedDisk] = 100;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true); // FIXME: delay a bit? Queue for later drawing? ***

    // Start the given disk drive spinning
    lastDiskRead[selectedDisk] = g_cpu->cycles;
    break;

  case 0x0A: // select drive 1
    select(0);
    break;
  case 0x0B: // select drive 2
    select(1);
    break;

  case 0x0C: // shift one read or write byte
    readWriteLatch = readOrWriteByte();
    /*
    if (readWriteLatch & 0x80)
      printf(" => Disk II reads 0x%.2X @ $%.4X\n", sequencer, g_cpu->pc);
    */
    if (readWriteLatch & 0x80)
      sequencer = 0;
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
  // for some period of time instead of this silly decrement counter ***
  if (!indicatorIsOn[selectedDisk]) {
    //    printf("Unexpected read while disk isn't on?\n");
    indicatorIsOn[selectedDisk] = 100;
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, true); // FIXME: queue for later drawing?
  }
  if (indicatorIsOn[selectedDisk] > 0 && indicatorIsOn[selectedDisk] < 100) {
    // slowly spin it down...
    if (--indicatorIsOn[selectedDisk] == 0) {
      g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: queue for later drawing?

    // Stop the given disk drive spinning
      lastDiskRead[selectedDisk] = 0; // FIXME: magic value. We need a tristate for this. ***
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
    if (readOrWriteByte() & 0x80)
      sequencer = 0;
    break;

  case 0x0D: // drive write
    // FIXME
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
    // We're changing track - flush the old track back to disk

    curWozTrack[selectedDisk] = disk[selectedDisk]->trackNumberForQuarterTrack(curHalfTrack[selectedDisk]*2);
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
  disk[driveNum]->readFile(filename); // FIXME error checking

  curWozTrack[driveNum] = disk[driveNum]->trackNumberForQuarterTrack(curHalfTrack[driveNum]*2);

  if (drawIt)
    g_ui->drawOnOffUIElement(UIeDisk1_state + driveNum, false);
}

void DiskII::ejectDisk(int8_t driveNum)
{
  if (disk[driveNum]) {
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
    indicatorIsOn[selectedDisk] = 100; // spindown time (fixme)
    g_ui->drawOnOffUIElement(UIeDisk1_activity + selectedDisk, false); // FIXME: queue for later drawing?

    // set the selected disk drive
    selectedDisk = which;
  }

  // Update the current woz track for the given disk drive
  curWozTrack[selectedDisk] =
    disk[selectedDisk]->trackNumberForQuarterTrack(curHalfTrack[selectedDisk]*2);
}

uint8_t DiskII::readOrWriteByte()
{
  if (!disk[selectedDisk]) {
    //printf("reading from uninserted disk\n");
    return 0xFF;
  }

  // FIXME: not handling writes at all at the moment ***
  if (writeMode && !writeProt) {
    return 0;
  }

  uint32_t curCycles = g_cpu->cycles;
  bool updateCycles = false;

  if (lastDiskRead[selectedDisk] == 0) {
    // assume it's a first-read-after-spinup; return the first valid data
    sequencer = disk[selectedDisk]->nextDiskByte(curWozTrack[selectedDisk]);
    updateCycles = true;
    goto done;
  }

  // Otherwise we figure out how many cycles we missed since the last
  // disk read, and pop the right number of bits off the woz track
  uint32_t missedCycles;
  missedCycles = curCycles - lastDiskRead[selectedDisk];

  missedCycles >>= 2;
  if (missedCycles)
    updateCycles = true;
  while (missedCycles) {
    sequencer <<= 1;
    sequencer |= disk[selectedDisk]->nextDiskBit(curWozTrack[selectedDisk]);
    missedCycles--;
  }

 done:
  if (updateCycles) {
    // We only update the lastDiskRead counter if the number of passed
    // cycles indicates that we did some sort of work...
    lastDiskRead[selectedDisk] = curCycles;
  }
  return sequencer;
}

void DiskII::fillDiskBuffer()
{
}

const char *DiskII::DiskName(int8_t num)
{
  // ***

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

  // ***
}
