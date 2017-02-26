#include <stdint.h>

#include "physicalpaddles.h"

class SDLPaddles : public PhysicalPaddles {
 public:
  SDLPaddles();
  virtual ~SDLPaddles();

  virtual void startReading();
  virtual uint8_t paddle0();
  virtual uint8_t paddle1();

  void gotMouseMovement(uint16_t x, uint16_t y);

 public:
  uint8_t p0;
  uint8_t p1;
};
