#ifndef __PHYSICALDISPLAY_H
#define __PHYSICALDISPLAY_H

#include <string.h> // strncpy

#include "vmdisplay.h" // FIXME: for AiieRect

class PhysicalDisplay {
 public:
  PhysicalDisplay() { overlayMessage[0] = '\0'; }
  virtual ~PhysicalDisplay() {};

  virtual void flush() = 0; // flush any pending drawings
  virtual void redraw() = 0; // total redraw, assuming nothing
  virtual void blit() = 0;             // blit everything to the display (including UI area)
  virtual void blit(AiieRect r) = 0;   // blit a piece of the VM area to the display

  virtual void drawUIImage(uint8_t imageIdx) = 0;
  // FIXME: drawImageOfSizeAt should probably be private now
  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint16_t sizey, uint16_t wherex, uint16_t wherey) = 0;

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c) = 0;
  virtual void drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str) = 0;
  virtual void debugMsg(const char *msg) {   strncpy(overlayMessage, msg, sizeof(overlayMessage));overlayMessage[strlen(overlayMessage)] = 0; }

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color) = 0;
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) = 0;

  virtual void drawUIPixel(uint16_t x, uint16_t y, uint16_t color) = 0;

  virtual void clrScr(uint8_t coloridx) = 0;

  // methods to draw in to the buffer - not directly to the screen.

  // First, methods that expect *us* to pixel-double the width...
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color) = 0;
  virtual void cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorA, uint8_t colorB) = 0;

  // Then the direct-pixel methods
  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color) = 0;
  
 protected:
  char overlayMessage[40];
};

#endif
