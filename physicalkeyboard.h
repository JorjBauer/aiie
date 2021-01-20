#ifndef __PHYSICALKEYBOARD_H
#define __PHYSICALKEYBOARD_H

#include <stdint.h>

#include "vmkeyboard.h"

#define PK_ESC 0x1B
#define PK_DEL 0x7F
#define PK_RET 0x0D
#define PK_TAB 0x09
#define PK_LARR 0x08 // control-H
#define PK_RARR 0x15 // control-U
#define PK_DARR 0x0A
#define PK_UARR 0x0B

// Virtual keys
#define PK_CTRL  0x81
#define PK_LSHFT 0x82
#define PK_RSHFT 0x83
#define PK_LOCK  0x84 // caps lock
#define PK_LA    0x85 // left (open) apple, aka paddle0 button
#define PK_RA    0x86 // right (closed) apple aka paddle1 button

#define PK_NONE  0xFF // not a key; but 0x00 is used internally by the
		      // library, and I don't want to harsh its buzz

class PhysicalKeyboard {
 public:
  PhysicalKeyboard(VMKeyboard *k) { this->vmkeyboard = k; }
  virtual ~PhysicalKeyboard() {};

  virtual void maintainKeyboard() = 0;

  virtual bool kbhit() = 0;
  virtual int8_t read() = 0;

 protected:
  VMKeyboard *vmkeyboard;
};

#endif
