#ifndef __VMKEYBOARD_H
#define __VMKEYBOARD_H

#include <stdint.h>

class VMKeyboard {
 public:
  virtual ~VMKeyboard() {}

  virtual void keyDepressed(uint8_t k) = 0;
  virtual void keyReleased(uint8_t k) = 0;
  virtual void maintainKeyboard(int64_t cycleCount) = 0;

  // Force every key up. Used to recover from a lost key-up (e.g. a focus change
  // or an OS-swallowed release) that would otherwise leave a key stuck repeating.
  virtual void releaseAllKeys() {}
};

#endif
