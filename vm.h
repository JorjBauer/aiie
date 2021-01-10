#ifndef __VM_H
#define __VM_H

#include <stdint.h>
#include <stdlib.h> // for NULL

#include "mmu.h"
#include "vmdisplay.h"
#include "vmkeyboard.h"

#define DISPLAYWIDTH 320
#define DISPLAYHEIGHT 240

class VM {
 public:
  VM() { mmu=NULL; vmdisplay = NULL; hasIRQ = false;}
  virtual ~VM() { if (mmu) delete mmu; if (vmdisplay) delete vmdisplay; }

  virtual bool Suspend(const char *fn) = 0;
  virtual bool Resume(const char *fn) = 0;

  virtual void SetMMU(MMU *mmu) { this->mmu = mmu; }
  virtual MMU *getMMU() { return mmu; }
  virtual VMKeyboard *getKeyboard() = 0;

  virtual void Reset() = 0;

  virtual void triggerPaddleInCycles(uint8_t paddleNum, uint16_t cycleCount) = 0;

  VMDisplay *vmdisplay;
  MMU *mmu;
  bool hasIRQ;
};

#endif
