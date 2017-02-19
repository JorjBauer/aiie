#ifndef __OPENCV_KEYBOARD_H
#define __OPENCV_KEYBOARD_H

#include "physicalkeyboard.h"
#include "vmkeyboard.h"

class OpenCVKeyboard : public PhysicalKeyboard {
 public:
  OpenCVKeyboard(VMKeyboard *k);
  virtual ~OpenCVKeyboard();
  
  virtual void maintainKeyboard();
 private:
  int lastKey;
};

#endif
