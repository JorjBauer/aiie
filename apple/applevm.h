#ifndef __APPLEVM_H
#define __APPLEVM_H

#include "cpu.h"
#include "appledisplay.h"
#include "diskii.h"
#include "hd32.h"
#include "vmkeyboard.h"
#include "parallelcard.h"
#ifdef TEENSYDUINO
#include "teensy-clock.h"
#else
#include "nix-clock.h"
#endif

#include "vm.h"
class AppleVM : public VM {
 public:
  AppleVM();
  virtual ~AppleVM();

  void Suspend(const char *fn);
  void Resume(const char *fn);

  void cpuMaintenance(uint32_t cycles);

  virtual void Reset();
  void Monitor();

  virtual void triggerPaddleInCycles(uint8_t paddleNum,uint16_t cycleCount);

  const char *DiskName(uint8_t drivenum);
  void ejectDisk(uint8_t drivenum);
  void insertDisk(uint8_t drivenum, const char *filename, bool drawIt = true);

  const char *HDName(uint8_t drivenum);
  void ejectHD(uint8_t drivenum);
  void insertHD(uint8_t drivenum, const char *filename);

  virtual VMKeyboard *getKeyboard();

  DiskII *disk6;
  HD32 *hd32;
 protected:
  VMKeyboard *keyboard;
  ParallelCard *parallel;
#ifdef TEENSYDUINO
  TeensyClock *teensyClock;
#else
  NixClock *nixClock;
#endif
};


#endif
