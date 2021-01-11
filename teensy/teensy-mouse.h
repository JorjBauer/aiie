#ifndef __TEENSY_MOUSE_H
#define __TEENSY_MOUSE_H

#include "physicalmouse.h"

class TeensyMouse : public PhysicalMouse {
 public:
  TeensyMouse();
  virtual ~TeensyMouse();

  virtual void maintainMouse();

  virtual void setPosition(uint16_t x, uint16_t y);
  virtual void getPosition(uint16_t *x, uint16_t *y);
  virtual bool getButton();

  void gotMouseEvent(uint32_t buttonState, int32_t x, int32_t y);
  void mouseButtonEvent(bool state);
private:
  int32_t xpos, ypos;
  bool button;
};

#endif
