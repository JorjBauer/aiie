#include "hd32.h"

/* AppleWin 32-MB hard drive emulation.
 *
 * cf. https://github.com/AppleWin/AppleWin/tree/master/firmware/HDD
 *
 *
 * General interface is outlined in 
 *   https://github.com/AppleWin/AppleWin/blob/master/source/Harddisk.cpp
 */

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
#include <errno.h>
#endif

#include "serialize.h"

#include "globals.h"

#include "hd32-rom.h"

#define HD32_BLOCKSIZE 512

#define HD32MAGIC 0xF5

#define DEVICE_OK 0x00
#define DEVICE_UNKNOWN_ERROR 0x28
#define DEVICE_IO_ERROR 0x27
#define DEVICE_WRITE_PROTECTED 0x28
#define DEVICE_OFF_LINE 0x2F

// Switches...
#define HD32_EXEC_RETSTAT 0x0
#define HD32_STATUS 0x1
#define HD32_COMMAND 0x2
#define HD32_UNITNUM 0x3
#define HD32_LBBUF 0x4
#define HD32_HBBUF 0x5
#define HD32_LBBLOCKNUM 0x6
#define HD32_HBBLOCKNUM 0x7
#define HD32_NEXTBYTE 0x8

// Commands
#define CMD_STATUS 0x0
#define CMD_READ 0x1
#define CMD_WRITE 0x2
#define CMD_FORMAT 0x3

HD32::HD32(AppleMMU *mmu)
{
  this->mmu = mmu;
  fd[0] = fd[1] = -1;   // so the Reset() below ejects against "no image", not garbage
  Reset();
}

HD32::~HD32()
{
}

bool HD32::Serialize(int8_t fd)
{
  serializeMagic(HD32MAGIC);
  serialize8(driveSelected);
  serialize8(unitSelected);
  serialize8(command);
  serialize8(enabled);
  serialize8(errorState[0]);
  serialize8(errorState[1]);
  serialize16(memBlock[0]);
  serialize16(memBlock[1]);
  serialize32(cursor[0]);
  serialize32(cursor[1]);

  for (int i=0; i<2; i++) {
    const char *fn = diskName(i);
    serializeString(fn);
  }
  serializeMagic(HD32MAGIC);
  return true;

 err:
  return false;
}

bool HD32::Deserialize(int8_t fd)
{
  deserializeMagic(HD32MAGIC);

  deserialize8(driveSelected);
  deserialize8(unitSelected);
  deserialize8(command);
  deserialize8(enabled);
  deserialize8(errorState[0]);
  deserialize8(errorState[1]);
  deserialize16(memBlock[0]);
  deserialize16(memBlock[1]);
  deserialize32(cursor[0]);
  deserialize32(cursor[1]);
  
  cachedBlockNum = -1; // just invalidate the cache; it will reload...

  for (int i=0; i<2; i++) {
    char buf[MAXPATH];
    deserializeString(buf);
    // FIXME: this tromps on error and some other vars ... that we just restored
    insertDisk(i, (char *)buf);
  }

  deserializeMagic(HD32MAGIC);
  
  return true;
 err:
  return false;
}

void HD32::Reset()
{
  enabled = 1;

  // Close any open images cleanly rather than dropping the descriptors: Reset
  // now runs on every cold boot / reboot (see AppleVM::Reset), so leaking here
  // would leak a file handle per reboot.
  ejectDisk(0);
  ejectDisk(1);
  errorState[0] = errorState[1] = 0;
  memBlock[0] = memBlock[1] = 0;
  diskBlock[0] = diskBlock[1] = 0;
  driveSelected = 0;
  command = CMD_STATUS;

  cachedBlockNum = -1;
}

uint8_t HD32::readSwitches(uint8_t s)
{
  uint8_t ret = DEVICE_OK;

  if (!enabled) {
    return DEVICE_IO_ERROR;
  }

  switch (s) {
  case HD32_EXEC_RETSTAT:
    switch (command) {
    case CMD_STATUS:
      // set ret to DEVICE_IO_ERROR & set error state=true if no image loaded
      if (fd[driveSelected] == -1) {
	// Nothing inserted
	ret = DEVICE_IO_ERROR;
	errorState[driveSelected] = 1;
      } else {
	ret = DEVICE_OK;
	errorState[driveSelected] = 0;
      }
      break;

    case CMD_READ:
      // FIXME: if diskblock[selectedDrive] >= disk image size, set/return io error
      errorState[driveSelected] = 0;
      ret = DEVICE_OK;

      cursor[driveSelected] = diskBlock[driveSelected] * HD32_BLOCKSIZE;
      if (!readBlockFromSelectedDrive()) {
	ret = DEVICE_IO_ERROR;
	errorState[driveSelected] = 1;
      }
      break;
      
    case CMD_WRITE:
      // FIXME: if diskblock[selectedDrive] >= disk image size, set/return io error
      // Clear any stale error first (as CMD_READ/CMD_STATUS do): the driver ROM
      // reports success/failure from errorState via HD32_STATUS, so a leftover 1
      // from an earlier call (e.g. an unhandled SmartPort command hitting the
      // default case) would make a perfectly good write look like an I/O error.
      errorState[driveSelected] = 0;
      ret = DEVICE_OK;
      if (!writeBlockToSelectedDrive()){
	ret = DEVICE_IO_ERROR;
	errorState[driveSelected] = 1;
      }
      break;

    case CMD_FORMAT:
      // Currently ignored. FIXME: make this zero out a 32MB file?
      break;

    default:
      errorState[driveSelected] = 1;
      ret = DEVICE_UNKNOWN_ERROR;
      break;

    }

    break;

  case HD32_STATUS:
    ret = errorState[driveSelected];
    break;
    
  case HD32_COMMAND:
    ret = command;
    break;

  case HD32_UNITNUM:
    ret = unitSelected;
    break;

  case HD32_LBBUF:
    ret = memBlock[driveSelected] & 0x00FF;
    break;
  case HD32_HBBUF:
    ret = ((memBlock[driveSelected] & 0xFF00) >> 8);
    break;

  case HD32_LBBLOCKNUM:
    ret = diskBlock[driveSelected] & 0x00FF;
    break;
  case HD32_HBBLOCKNUM:
    ret = ((diskBlock[driveSelected] & 0xFF00) >> 8);
    break;

  case HD32_NEXTBYTE:
    ret = readNextByteFromSelectedDrive();
    break;
  }

  return ret;
}

void HD32::writeSwitches(uint8_t s, uint8_t v)
{
  if (!enabled)
    return;

  switch (s) {
  case HD32_COMMAND:
    command = v;
    break;
  case HD32_UNITNUM:
    unitSelected = v;
    // FIXME: verify slot#?
    driveSelected = (v & 0x80) ? 1 : 0;
    break;
  case HD32_LBBUF:
    memBlock[driveSelected] = (memBlock[driveSelected] & 0xFF00) | v;
    break;
  case HD32_HBBUF:
    memBlock[driveSelected] = (memBlock[driveSelected] & 0x00FF) | (v << 8);
    break;
  case HD32_LBBLOCKNUM:
    diskBlock[driveSelected] = (diskBlock[driveSelected] & 0xFF00) | v;
    break;
  case HD32_HBBLOCKNUM:
    diskBlock[driveSelected] = (diskBlock[driveSelected] & 0x00FF) | (v << 8);
    break;
  }
}

void HD32::loadROM(uint8_t *toWhere)
{
#ifdef TEENSYDUINO
  println("loading HD32 rom");
  for (uint16_t i=0; i<=0xFF; i++) {
    toWhere[i] = pgm_read_byte(&romData[i]);
  }
#else
  printf("loading HD32 rom\n");
  memcpy(toWhere, romData, 256);
#endif

  // The ROM has a hardcoded JMP $C600 at offset $5C as a fallback when
  // the HD can't boot (fall back to Disk II). Patch the target to
  // point to wherever the Disk II actually is, or loop forever if
  // there is no Disk II.
  if (g_slotDiskII) {
    toWhere[0x5E] = 0xC0 + g_slotDiskII;
  } else {
    toWhere[0x5D] = 0x5C;
    toWhere[0x5E] = 0xC0 + g_slotHD32;
  }
}

uint8_t HD32::readNextByteFromSelectedDrive()
{
  uint8_t ret = 0;
  
  if (fd[driveSelected] == -1) {
    return 0;
  }

  int32_t blockToRead = cursor[driveSelected] >> 9; // 512-byte block number
  if (blockToRead != cachedBlockNum) {
    int32_t fileOff = blockToRead*512 + (int32_t)hdrOffset[driveSelected];
    if (g_filemanager->lseek(fd[driveSelected], fileOff, SEEK_SET) != fileOff) {
      goto err;
    }
    ssize_t nread = g_filemanager->read(fd[driveSelected], cachedBlock, 512);
    if (nread != 512) {
      goto err;
    }
    cachedBlockNum = blockToRead;

  }

  ret = cachedBlock[cursor[driveSelected] & 0x1FF];
  cursor[driveSelected]++;
  return ret;

 err:
  //  memset(cachedBlock, 0, sizeof(cachedBlock));
  //  cachedBlockNum = -1;
  return false;
}

// Based on diskBlock[driveSelected]; updates cursor[driveSelected].
// Populates the local cache as well as the memory block pointed to.
bool HD32::readBlockFromSelectedDrive()
{
  if (fd[driveSelected]==-1)
    return false;

  cursor[driveSelected] = diskBlock[driveSelected] * HD32_BLOCKSIZE;
  int32_t blockToRead = cursor[driveSelected] >> 9; // 512-byte block number
  if (blockToRead != cachedBlockNum) {
    int32_t fileOff = blockToRead*512 + (int32_t)hdrOffset[driveSelected];
    if (g_filemanager->lseek(fd[driveSelected], fileOff, SEEK_SET) != fileOff) {
      goto err;
    }
    ssize_t nread = g_filemanager->read(fd[driveSelected], cachedBlock, 512);
    if (nread != 512) {
      goto err;
    }
    cachedBlockNum = blockToRead;
  }
  
  for (uint16_t i=0; i<HD32_BLOCKSIZE; i++) {
    mmu->write(memBlock[driveSelected] + i, cachedBlock[i]);
  }

  return true;

 err:
  //  memset(cachedBlock, 0, sizeof(cachedBlock));
  //  cachedBlockNum = -1;
  return false;
}

bool HD32::writeBlockToSelectedDrive()
{
  cachedBlockNum = -1; // just invalidate any cache we have

  if (fd[driveSelected]==-1)
    return false;

  for (uint16_t i=0; i<HD32_BLOCKSIZE; i++) {
    cachedBlock[i] = mmu->read(memBlock[driveSelected] + i);
  }
  int32_t fileOff = diskBlock[driveSelected]*HD32_BLOCKSIZE + (int32_t)hdrOffset[driveSelected];
  if (g_filemanager->lseek(fd[driveSelected], fileOff, SEEK_SET) != fileOff ||
      g_filemanager->write(fd[driveSelected], cachedBlock, HD32_BLOCKSIZE) != HD32_BLOCKSIZE) {
    // FIXME
#ifndef TEENSYDUINO
    printf("ERROR: failed to write to hd file? errno %d\n", errno);
#endif
    return false;
  }

  return true;
}

void HD32::setEnabled(uint8_t e)
{
  enabled = e;
}

const char *HD32::diskName(int8_t num)
{
  if (fd[num] != -1)
    return g_filemanager->fileName(fd[num]);

  return "";
}

// A 2IMG (.2mg) file wraps the raw disk data in a header (usually 64 bytes).
// Sniff it so block 0 lands on the real data instead of the header. The layout
// is little-endian: "2IMG" magic, then at byte 8 the header length (u16), at
// byte 12 the image format (u32: 0=DOS order, 1=ProDOS order, 2=nibble), and at
// byte 24 the data offset (u32). HD32 is a ProDOS block device, so only the
// ProDOS-order layout is meaningful; a DOS-order or nibble image is flagged but
// still mounted at its data offset (it simply will not mount cleanly in ProDOS).
// Returns the byte offset to block 0 (0 when this is not a 2IMG image).
static uint32_t sniff2mgOffset(int8_t fd)
{
  uint8_t h[64];
  if (g_filemanager->lseek(fd, 0, SEEK_SET) != 0)
    return 0;
  if (g_filemanager->read(fd, h, sizeof(h)) != (int)sizeof(h))
    return 0;
  if (h[0] != '2' || h[1] != 'I' || h[2] != 'M' || h[3] != 'G')
    return 0;

  uint32_t format  = h[12] | (h[13] << 8) | (h[14] << 16) | (h[15] << 24);
  uint32_t dataOff = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
  uint16_t hdrLen  = h[8]  | (h[9]  << 8);

  if (dataOff == 0)                 // some writers leave data offset at 0
    dataOff = hdrLen ? hdrLen : 64; // fall back to the header length, then 64

#ifndef TEENSYDUINO
  if (format != 1) {
    printf("WARNING: 2IMG image is not ProDOS-ordered (format %u); "
           "it will not mount cleanly as a hard disk\n", format);
  }
#endif
  return dataOff;
}

void HD32::insertDisk(int8_t driveNum, const char *filename)
{
  ejectDisk(driveNum);
  fd[driveNum] = g_filemanager->openFile(filename);
  hdrOffset[driveNum] = (fd[driveNum] != -1) ? sniff2mgOffset(fd[driveNum]) : 0;
  errorState[driveNum] = 0;
  enabled = 1;
}

void HD32::ejectDisk(int8_t driveNum)
{
  if (fd[driveNum] != -1) {
    g_filemanager->closeFile(fd[driveNum]);
    fd[driveNum] = -1;
  }
  hdrOffset[driveNum] = 0;
}

