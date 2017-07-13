#include <Arduino.h>

#include "physicalpaddles.h"

class TeensyPaddles : public PhysicalPaddles {
 public:
  TeensyPaddles(uint8_t p0pin, uint8_t p1pin, bool p0rev, bool p1rev);
  virtual ~TeensyPaddles();

  virtual uint8_t paddle0();
  virtual uint8_t paddle1();
  virtual void startReading();

  uint8_t p0pin;
  uint8_t p1pin;
  bool p0rev;
  bool p1rev;
};
