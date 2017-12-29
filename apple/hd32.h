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
#include "Slot.h"

#include "RingBuffer.h"

class HD32 : public Slot {
 public:
  HD32(AppleMMU *mmu);
  virtual ~HD32();

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  void setEnabled(uint8_t e);

  void insertDisk(int8_t driveNum, const char *filename);
  void ejectDisk(int8_t driveNum);

  const char *diskName(int8_t num);

 protected:
  uint8_t readNextByteFromSelectedDrive();
  bool writeBlockToSelectedDrive();

 private:
  AppleMMU *mmu;

  uint8_t driveSelected; // 0 or 1
  uint8_t unitSelected; // b7 = drive#; b6..4 = slot#; b3..0 = ?
  uint8_t command;       // CMD_*; FIXME, make enum

  uint8_t enabled;

  uint8_t errorState[2]; // status of last operation
  uint16_t memBlock[2];  // ??
  uint16_t diskBlock[2]; // currently selected block
  
  int8_t fd[2];
  uint32_t cursor[2]; // seek position on the given file handle
};

#endif
