#ifndef __MOCKINGBOARD_H
#define __MOCKINGBOARD_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "applemmu.h"
#include "Slot.h"
#include "sy6522.h"

class Mockingboard : public Slot {
 public:
  Mockingboard();
  virtual ~Mockingboard();

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);
  
  void update(uint32_t cycles);

  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t val);

 private:
  SY6522 sy6522[2];
};

#endif
