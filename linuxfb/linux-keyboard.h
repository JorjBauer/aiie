#ifndef __LINUX_KEYBOARD_H
#define __LINUX_KEYBOARD_H

#include "physicalkeyboard.h"
#include "vmkeyboard.h"

class LinuxKeyboard : public PhysicalKeyboard {
 public:
  LinuxKeyboard(VMKeyboard *k);
  virtual ~LinuxKeyboard();
  
  virtual void maintainKeyboard();

  virtual bool kbhit();
  virtual int8_t read();
  
 private:
  int fd;
};

#endif
