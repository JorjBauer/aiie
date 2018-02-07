#ifndef __LINUX_PRINTER_H
#define __LINUX_PRINTER_H

#include <stdlib.h>
#include <inttypes.h>

#include "physicalprinter.h"

class LinuxPrinter : public PhysicalPrinter {
 public:
  LinuxPrinter();
  virtual ~LinuxPrinter();

  virtual void addLine(uint8_t *rowOfBits); // must be 960 pixels wide (120 bytes)

  virtual void update();

  virtual void moveDownPixels(uint8_t p);
};

#endif
