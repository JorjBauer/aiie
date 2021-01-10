#ifndef __PHYSICALMOUSE_H
#define __PHYSICALMOUSE_H

#include <stdint.h>

class PhysicalMouse {
 public:
  PhysicalMouse() {}
  virtual ~PhysicalMouse() {};

  virtual void maintainMouse() = 0;
};

#endif
