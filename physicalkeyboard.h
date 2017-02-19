#ifndef __PHYSICALKEYBOARD_H
#define __PHYSICALKEYBOARD_H

#include <stdint.h>

#include "vmkeyboard.h"

#define ESC 0x1B
#define DEL 0x7F
#define RET 0x0D
#define TAB 0x09
#define LARR 0x08 // control-H
#define RARR 0x15 // control-U
#define DARR 0x0A
#define UARR 0x0B

// Virtual keys
#define _CTRL  0x81
#define LSHFT 0x82
#define RSHFT 0x83
#define LOCK  0x84 // caps lock
#define LA    0x85 // left (open) apple, aka paddle0 button
#define RA    0x86 // right (closed) apple aka paddle1 button

class PhysicalKeyboard {
 public:
  PhysicalKeyboard(VMKeyboard *k) { this->vmkeyboard = k; }
  virtual ~PhysicalKeyboard() {};

  virtual void maintainKeyboard() = 0;

 protected:
  VMKeyboard *vmkeyboard;
};

#endif
