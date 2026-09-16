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
  virtual void busReset(); // the RESET line: motor off, drive 1, media kept
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  void insertDisk(int8_t driveNum, const char *filename, bool drawIt = true);
  void ejectDisk(int8_t driveNum);

  const char *DiskName(int8_t num);

  void maintenance(int64_t cycles);

  uint8_t selectedDrive();
  uint8_t headPosition(uint8_t drive);

  // Drive events, for a host that plays drive sounds. The listener is
  // called on the emulator thread as each happens; NULL means nobody is
  // listening. Steps are reported only while the drive is enabled (the
  // motor on, or in its spin-down second), because a real drive's phases
  // do nothing otherwise. STOP_HIT is a step asked for below track 0:
  // the head against the stop, the boot-time chatter.
  enum DriveEvent { DRIVE_MOTOR_ON, DRIVE_MOTOR_OFF, DRIVE_STEP_IN, DRIVE_STEP_OUT, DRIVE_STOP_HIT };
  static void (*eventListener)(uint8_t drive, DriveEvent e);
  
 private:
  void setPhase(uint8_t phase);

  bool isWriteProtected();
  void setWriteMode(bool enable);
  void select(int8_t which); // 0 or 1 for drives 1 and 2, respectively
  uint8_t readOrWriteByte(bool allowOvershoot);

  void driveOn();
  void driveOff();

#ifndef TEENSYDUINO
  void convertDskToNib(const char *outFN);
#endif
  
  int64_t calcExpectedBits();

  // Advance the LSS sequencer based on CPU cycles elapsed since bits
  // were last delivered. Called on every soft-switch access so the
  // data latch reflects a continuously-running LSS, not one that only
  // ticks on C08C reads.
  void tickLSS();
  void lssClockBit(uint8_t bit);

 public:
  // debugging
  WozSerializer *disk[2];
private:
  volatile int8_t curHalfTrack[2];
  volatile uint8_t curWozTrack[2];
  volatile int8_t curPhase[2];
  volatile uint8_t readWriteLatch;
  volatile uint8_t sequencer, dataRegister; // diskII logic state sequencer vars
  volatile uint8_t lssState;                // LSS state machine (0-F), UTA2E Fig 9.11
  volatile int64_t driveSpinupCycles[2];
  volatile int64_t deliveredDiskBits[2];
  volatile int64_t noiseBits[2];   // random bits fed while the head is on an unmapped position
  
  bool writeMode;   // Q7: false=read, true=write
  bool q6;          // Q6: false (toggled by $C08C)=shift/write, true ($C08D)=load
  bool writeProt;
  AppleMMU *mmu;

  volatile int64_t diskIsSpinningUntil[2];

  volatile int8_t selectedDisk;

  volatile int64_t flushAt[2];

  // The clock maintenance() last saw. A CPU reset restarts the clock, and
  // a deadline set before it (a spin-down, a flush) is re-armed from the
  // new clock rather than waited out from the old one.
  int64_t lastMaintenanceCycle;
};

#endif
