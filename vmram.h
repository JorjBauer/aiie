#ifndef __VMRAM__H
#define __VMRAM__H

#include <stdint.h>

/* Preallocated RAM class. */

class VMRam {
 public: 
  VMRam();
  ~VMRam();

  void init();

  uint8_t readByte(uint32_t addr);
  void writeByte(uint32_t addr, uint8_t value);

  uint8_t *memPtr(uint32_t addr);

  bool Serialize(int8_t fd);
  bool Deserialize(int8_t fd);

  bool Test();

 private:
  // We need 599 pages of 256 bytes for the //e.
  //
  // This previously used a split memory model where some of this was
  // in internal ram and some was external - and while that's not true
  // right now, it may be true again in the future.
  //
  // If we go that way: zero-page should be in internal RAM (it's
  // changed very often). Some other pages that are read or written
  // often should probably go in here too. The order of the pages (in
  // apple/applemmu.cpp) defines what order the pages are referenced
  // in the VMRam object; the lowest should wind up in internal RAM.

  // Pages 0-3 are ZP; we want those in RAM.
  // Pages 4-7 are 0x200 - 0x3FF. We want those in RAM too (text pages).

  // Has to be static if we're using the EXTMEM sectioning, so it's now in vmram.cpp :/
  //EXTMEM uint8_t preallocatedRam[599*256];


};


#endif
