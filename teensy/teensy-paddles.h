#include <Arduino.h>

#include "physicalpaddles.h"

class TeensyPaddles : public PhysicalPaddles {
 public:
  TeensyPaddles();
  virtual ~TeensyPaddles();

  virtual uint8_t paddle0();
  virtual uint8_t paddle1();
  virtual void startReading();
};
