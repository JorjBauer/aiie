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

  bool Serialize(int8_t fd);
  bool Deserialize(int8_t fd);

  bool Test();

 private:
  // We need 591 pages of 256 bytes for the //e. There's not 
  // enough RAM in the Teensy 3.6 for both this (nearly 148k) 
  // and the display's DMA (320*240*2 = 150k).
  //
  // We could put all of the //e RAM in an external SRAM -- but the 
  // external SRAM access is necessarily slower than just reading the 
  // built-in RAM. So this is a hybrid: we allocate some internal 
  // SRAM from the Teensy, and will use it for the low addresses of 
  // our VM space; and anything above that goes to the external SRAM.
  //
  // Changing this invalidates the save files, so don't just change it 
  // willy-nilly :)
  //
  // Zero-page should be in internal RAM (it's changed very often). Some 
  // other pages that are read or written often should probably go in
  // here too. The order of the pages (in apple/applemmu.cpp) defines
  // what order the pages are referenced in the VMRam object; the lowest
  // wind up in internal RAM.

  // Pages 0-3 are ZP; we want those in RAM.
  // Pages 4-7 are 0x200 - 0x3FF. We want those in RAM too (text pages).

  uint8_t preallocatedRam[591*256];
};


#endif
