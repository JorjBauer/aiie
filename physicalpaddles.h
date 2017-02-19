#ifndef __PHYSICALPADDLES_H
#define __PHYSICALPADDLES_H

#include <stdint.h>

class PhysicalPaddles {
 public:
  virtual ~PhysicalPaddles() {}

  virtual void startReading() = 0;
  virtual uint8_t paddle0() = 0;
  virtual uint8_t paddle1() = 0;
};

#endif
