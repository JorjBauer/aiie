#ifndef __DISKII_H
#define __DISKII_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "woz-serializer.h"

#include "filemanager.h"
#include "applemmu.h"
#include "slot.h"

#include "LRingBuffer.h"
#include "nibutil.h"

class DiskII : public Slot {
 public:
  DiskII(AppleMMU *mmu);
  virtual ~DiskII();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  void insertDisk(int8_t driveNum, const char *filename, bool drawIt = true);
  void ejectDisk(int8_t driveNum);

  const char *DiskName(int8_t num);
  void flushTrack(int8_t track, int8_t sel);

  void fillDiskBuffer(); // called from main loop

 private:
  void setPhase(uint8_t phase);

  bool isWriteProtected();
  void setWriteMode(bool enable);
  void select(int8_t which); // 0 or 1 for drives 1 and 2, respectively
  uint8_t readOrWriteByte();

#ifndef TEENSYDUINO
  void convertDskToNib(const char *outFN);
#endif

 private:
  volatile int8_t curHalfTrack[2];
  volatile uint8_t curWozTrack[2];
  volatile int8_t curPhase[2];
  uint8_t readWriteLatch;
  uint8_t sequencer, dataRegister; // diskII logic state sequencer vars
  WozSerializer *disk[2];
  uint32_t lastDiskRead[2];
  
  bool writeMode;
  bool writeProt;
  AppleMMU *mmu;

  volatile uint8_t indicatorIsOn[2];

  volatile int8_t selectedDisk;
};

#endif
