#ifndef __APPLEMMU_H
#define __APPLEMMU_H

#include <stdlib.h>
#include "appledisplay.h"
#include "slot.h"
#include "mmu.h"
#include "noslotclock.h"

// when we read a nondeterministic result, we return
// _FLOATINGBUS. Maybe some day we can come back here and figure out
// how to return what the Apple would have.

#define _FLOATINGBUS 0

// Switches activated by various memory locations
enum {
  S_TEXT  = 0x0001,
  S_MIXED = 0x0002,
  S_HIRES = 0x0004,
  S_PAGE2 = 0x0008,
  S_80COL = 0x0010,
  S_ALTCH = 0x0020,
  S_80STORE = 0x0040,
  S_DHIRES = 0x0080
};

typedef bool (*callback_t)(void *);

class AppleVM;

class AppleMMU : public MMU {
  friend class AppleVM;

 public:
  AppleMMU(AppleDisplay *display);
  virtual ~AppleMMU();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual uint8_t read(uint16_t address);
  virtual uint8_t readDirect(uint16_t address, uint8_t fromPage);
  virtual void write(uint16_t address, uint8_t v);

  virtual void Reset();

  void keyboardInput(uint8_t v);
  void setKeyDown(bool isTrue);

  void triggerPaddleTimer(uint8_t paddle);

  void resetRAM(); // used by BIOS on cold boot

  void setSlot(int8_t slotnum, Slot *peripheral);

  void setAppleKey(int8_t which, bool isDown);

 protected:
  bool handleNoSlotClock(uint16_t address, uint8_t *rv);

  void resetDisplay();
  uint8_t readSwitches(uint16_t address);
  void writeSwitches(uint16_t address, uint8_t v);
  void handleMemorySwitches(uint16_t address, uint16_t lastSwitch);

  void updateMemoryPages();

 private:
  AppleDisplay *display;
  uint16_t switches;
 public: // 'public' for debugging
  bool auxRamRead;
  bool auxRamWrite;
  bool bank2;
  bool readbsr;
  bool writebsr;
  bool altzp;
  bool intcxrom;
  bool slot3rom;
  int8_t slotLatch;

  bool preWriteFlag;        // see UTA2E p. 5-23

  Slot *slots[8]; // slots 0-7

  uint16_t readPages[0x100];
  uint16_t writePages[0x100];

  bool anyKeyDown;
  
  NoSlotClock *clock;
};

#endif
