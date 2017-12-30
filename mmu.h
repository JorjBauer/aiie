#ifndef __MMU_H
#define __MMU_H

#include <stdint.h>

class MMU {
 public:
  virtual ~MMU() {}

  virtual void Reset() = 0;

  virtual uint8_t read(uint16_t mem) = 0;
  virtual void write(uint16_t mem, uint8_t val) = 0;
  virtual uint8_t readDirect(uint16_t address, uint8_t fromPage) = 0;

  virtual bool Serialize(int8_t fd) = 0;
  virtual bool Deserialize(int8_t fd) = 0;
};

#endif
