#ifndef __APPLEMOUSE_H
#define __APPLEMOUSE_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "applemmu.h"
#include "slot.h"

class Mouse : public Slot {
 public:
  Mouse();
  virtual ~Mouse();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset(); // used by BIOS cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  virtual bool hasExtendedRom();
  virtual void loadExtendedRom(uint8_t *toWhere, uint16_t byteOffset);

  void maintainMouse(int64_t cycleCount);

private:
  uint8_t status;
  uint8_t interruptsTriggered;

  uint16_t lastX, lastY;
  bool lastButton;
  bool curButton;
};

#endif
