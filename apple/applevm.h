#ifndef __APPLEVM_H
#define __APPLEVM_H

#include "cpu.h"
#include "appledisplay.h"
#include "diskii.h"
#include "vmkeyboard.h"
#include "parallelcard.h"
#include "mockingboard.h"
#ifdef TEENSYDUINO
#include "teensy-clock.h"
#endif

#include "vm.h"
class AppleVM : public VM {
 public:
  AppleVM();
  virtual ~AppleVM();

  void cpuMaintenance(uint32_t cycles);

  virtual void Reset();
  void Monitor();

  virtual void triggerPaddleInCycles(uint8_t paddleNum,uint16_t cycleCount);

  const char *DiskName(uint8_t drivenum);
  void ejectDisk(uint8_t drivenum);
  void insertDisk(uint8_t drivenum, const char *filename, bool drawIt = true);
  void batteryLevel(uint8_t zeroToOneHundred);

  virtual VMKeyboard *getKeyboard();

 protected:
  DiskII *disk6;
  VMKeyboard *keyboard;
  ParallelCard *parallel;
  Mockingboard *mockingboard;
#ifdef TEENSYDUINO
  TeensyClock *teensyClock;
#endif
};


#endif
