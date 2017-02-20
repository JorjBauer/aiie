#ifndef __TEENSY_PRINTER_H
#define __TEENSY_PRINTER_H

#include <stdlib.h>
#include <SoftwareSerial.h>

#include "physicalprinter.h"

class TeensyPrinter : public PhysicalPrinter {
 public:
  TeensyPrinter();
  virtual ~TeensyPrinter();

  virtual void addLine(uint8_t *rowOfBits); // must be 960 pixels wide (120 bytes)

  virtual void update();

  virtual void moveDownPixels(uint8_t p);

 private:
  uint16_t ypos;
  SoftwareSerial *ser;
};

#endif
