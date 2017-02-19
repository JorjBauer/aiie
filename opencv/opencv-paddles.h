#include <stdint.h>

#include "physicalpaddles.h"

class OpenCVPaddles : public PhysicalPaddles {
 public:
  OpenCVPaddles();
  virtual ~OpenCVPaddles();

  virtual void startReading();
  virtual uint8_t paddle0();
  virtual uint8_t paddle1();

 public:
  uint8_t p0;
  uint8_t p1;
};
