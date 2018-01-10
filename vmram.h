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

 private:
  uint8_t preallocatedRam[591*256]; // 591 pages of RAM
};


#endif
