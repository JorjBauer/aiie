#ifndef __TEENSY_DISPLAY_H
#define __TEENSY_DISPLAY_H

#include <Arduino.h>
#include <SPI.h>
#include "basedisplay.h"

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

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str);

  virtual void drawUIImage(uint8_t imageIdx);
  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint16_t sizey, uint16_t wherex, uint16_t wherey);

  void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint16_t color16);
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

private:
  bool use8875;

  const uint8_t *shellImage;
  const uint16_t shellWidth, shellHeight;
  const uint8_t *d1OpenImage;
  const uint16_t driveWidth, driveHeight; // assume all the latches are the same width/height no matter what position
  const uint8_t *d1ClosedImage;
  const uint8_t *d2OpenImage;
  const uint8_t *d2ClosedImage;
  const uint8_t *appleImage;
  const uint16_t appleImageWidth, appleImageHeight;
};

#endif
