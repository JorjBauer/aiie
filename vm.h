#ifndef __VM_H
#define __VM_H

#include <stdint.h>
#include <stdlib.h> // for calloc

#include "mmu.h"
#include "vmdisplay.h"
#include "vmkeyboard.h"

class VM {
 public:
  VM() { mmu=NULL; vmdisplay = NULL; videoBuffer = (uint8_t *)calloc(320*240/2, 1); hasIRQ = false;}
  virtual ~VM() { if (mmu) delete mmu; if (vmdisplay) delete vmdisplay; free(videoBuffer); }

  virtual void SetMMU(MMU *mmu) { this->mmu = mmu; }
  virtual MMU *getMMU() { return mmu; }
  virtual VMKeyboard *getKeyboard() = 0;

  virtual void Reset() = 0;

  virtual void triggerPaddleInCycles(uint8_t paddleNum, uint16_t cycleCount) = 0;

  uint8_t *videoBuffer;
  VMDisplay *vmdisplay;
  MMU *mmu;
  bool hasIRQ;
};

#endif
