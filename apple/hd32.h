#ifndef __HD32_H
#define __HD32_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "filemanager.h"
#include "applemmu.h"
#include "slot.h"

#include "LRingBuffer.h"

class HD32 : public Slot {
 public:
  HD32(AppleMMU *mmu);
  virtual ~HD32();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  void setEnabled(uint8_t e);

  void insertDisk(int8_t driveNum, const char *filename);
  void ejectDisk(int8_t driveNum);

  const char *diskName(int8_t num);

  // Called from the VM's CPU maintenance tick: turns the activity light
  // off once the drive has been quiet for a moment.
  void maintenance(int64_t cycles);

 protected:
  void noteActivity(uint8_t drive);
 public:

 protected:
  uint8_t readNextByteFromSelectedDrive();
  bool readBlockFromSelectedDrive();
  bool writeBlockToSelectedDrive();

 private:
  AppleMMU *mmu;

  uint8_t driveSelected; // 0 or 1
  uint8_t unitSelected; // b7 = drive#; b6..4 = slot#; b3..0 = ?
  uint8_t command;       // CMD_*; FIXME, make enum

  uint8_t enabled;

  uint8_t errorState[2]; // status of last operation
  uint16_t memBlock[2];  // pointer to memory (for writing blocks to disk)
  uint16_t diskBlock[2]; // currently selected block
  
  int8_t fd[2];
  uint32_t cursor[2]; // seek position on the given file handle
  uint32_t hdrOffset[2]; // bytes to skip before block 0 (2IMG header; 0 = raw image)
  uint16_t blockCount[2]; // image size in 512-byte blocks (0 = nothing mounted)

  // Activity light: lit by a block read or write, out after a short quiet
  // spell (0 means not lit).
  int64_t activityUntil[2];

  // One block of read cache. Keyed on the drive as well as the block number:
  // both drives have a block 5, and a swapped image has a new block 0.
  int32_t cachedBlockNum;   // -1 = nothing cached
  int8_t cachedBlockDrive;
  uint8_t cachedBlock[512];
};

#endif
