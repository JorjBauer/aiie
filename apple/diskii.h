#ifndef __DISKII_H
#define __DISKII_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "filemanager.h"
#include "applemmu.h"
#include "Slot.h"

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

  void checkFlush(int8_t track);

#ifndef TEENSYDUINO
  void convertDskToNib(const char *outFN);
#endif

 private:
  volatile int8_t curHalfTrack[2];
  volatile int8_t curPhase[2];
  volatile bool trackDirty; // does this track need flushing to disk?
  uint8_t readWriteLatch;
  LRingBuffer *trackBuffer; // nibblized data
  uint8_t rawTrackBuffer[4096]; // not nibblized data
  
  bool writeMode;
  bool writeProt;
  AppleMMU *mmu;

  int8_t disk[2];
  volatile uint8_t indicatorIsOn[2];
  uint8_t diskType[2];

  volatile int8_t trackToRead; // -1 when we're idle; not -1 when we need to read a track.
  volatile int8_t selectedDisk;
  volatile int8_t trackToFlush; // -1 when there's none
  volatile int8_t diskToFlush; // which selected disk are we writing to?
};

#endif
