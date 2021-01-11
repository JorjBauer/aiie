#ifndef __PHYSICALMOUSE_H
#define __PHYSICALMOUSE_H

#include <stdint.h>

enum {
  XCLAMP = 0,
  YCLAMP = 1
};

class PhysicalMouse {
 public:
  PhysicalMouse() { lowClamp[XCLAMP] = lowClamp[YCLAMP] = 0; highClamp[XCLAMP] = highClamp[YCLAMP] = 1023; }
  virtual ~PhysicalMouse() {};

  virtual void maintainMouse() = 0;

  virtual void setClamp(uint8_t direction, uint16_t low, uint16_t high) { lowClamp[direction] = low; highClamp[direction] = high; }
  virtual void setPosition(uint16_t x, uint16_t y) = 0;
  virtual void getPosition(uint16_t *x, uint16_t *y) = 0;
  virtual bool getButton() = 0;

protected:
  uint16_t lowClamp[2], highClamp[2];
};

#endif
