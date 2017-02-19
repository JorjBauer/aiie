#ifndef __VMDISPLAY_H
#define __VMDISPLAY_H

class MMU;

class VMDisplay {
 public:
  VMDisplay(uint8_t *vb) { videoBuffer = vb; }
  virtual ~VMDisplay() { videoBuffer = NULL; };

  virtual void SetMMU(MMU *m) { mmu = m; }

  virtual bool needsRedraw() = 0;
  virtual void didRedraw() = 0;

  MMU *mmu;
  uint8_t *videoBuffer;
};

#endif
