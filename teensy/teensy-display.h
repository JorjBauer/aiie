#ifndef __TEENSY_DISPLAY_H
#define __TEENSY_DISPLAY_H

#include <Arduino.h>
#include <ILI9341_t3n.h>

#include "physicaldisplay.h"

class BIOS;

class TeensyDisplay : public PhysicalDisplay {
  friend class BIOS;

 public:
  TeensyDisplay();
  virtual ~TeensyDisplay();
  
  virtual void blit();
  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void clrScr(uint8_t coloridx);
  virtual void flush();

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint8_t sizey, uint16_t wherex, uint8_t wherey);

  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorA, uint8_t colorB);
  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);

  uint32_t frameCount();
 private:
  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
  virtual void drawUIPixel(uint16_t x, uint16_t y, uint16_t color);

  bool needsRedraw;
  bool driveIndicator[2];
  bool driveIndicatorDirty;
};

#endif
