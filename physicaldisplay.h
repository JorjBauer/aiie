#ifndef __PHYSICALDISPLAY_H
#define __PHYSICALDISPLAY_H

#include <string.h> // strncpy

class PhysicalDisplay {
 public:
  PhysicalDisplay() { overlayMessage[0] = '\0'; }
  virtual ~PhysicalDisplay() {};

  virtual void redraw() = 0; // total redraw, assuming nothing
  virtual void blit() = 0;   // redraw just the VM display area

  virtual void drawDriveDoor(uint8_t which, bool isOpen) = 0;
  virtual void drawDriveStatus(uint8_t which, bool isRunning) = 0;
  virtual void drawBatteryStatus(uint8_t percent) = 0;

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c) = 0;
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str) = 0;
  virtual void debugMsg(const char *msg) {   strncpy(overlayMessage, msg, sizeof(overlayMessage)); }

 protected:
  char overlayMessage[40];
};

#endif
