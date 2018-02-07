#ifndef __TEENSY_KEYBOARD_H
#define __TEENSY_KEYBOARD_H

#include "physicalkeyboard.h"

class TeensyKeyboard : public PhysicalKeyboard {
 public:
  TeensyKeyboard(VMKeyboard *k);
  virtual ~TeensyKeyboard();

  // Interface used by the VM...
  virtual void maintainKeyboard();

  // Interface used by the BIOS...
  virtual bool kbhit();
  virtual int8_t read();


 private:
  void pressedKey(uint8_t key);
  void releasedKey(uint8_t key);

 private:
  bool leftShiftPressed;
  bool rightShiftPressed;
  bool ctrlPressed;
  bool capsLock;
  bool leftApplePressed;
  bool rightApplePressed;

  int8_t numPressed;
};

#endif
