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
  virtual void flush() { };

  virtual void clrScr(uint8_t coloridx);

  virtual void drawUIImage(uint8_t imageIdx);
  virtual void drawDriveActivity(bool drive0, bool drive1);

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint16_t sizey, uint16_t wherex, uint16_t wherey);

  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);

  uint32_t frameCount();
 private:
  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  bool driveIndicator[2];

private:
  // These were declared const but are populated in the constructor
  // body via getImageInfoAndData(). With strict modern compilers
  // that's UB (writing to const) and can cause the optimizer to
  // assume they're whatever zero-init value they had — leading to
  // bogus zero widths and out-of-bounds buffer math at runtime.
  uint8_t *shellImage;
  uint16_t shellWidth, shellHeight;
  uint8_t *d1OpenImage;
  uint16_t driveWidth, driveHeight; // assume all the latches are the same width/height
  uint8_t *d1ClosedImage;
  uint8_t *d2OpenImage;
  uint8_t *d2ClosedImage;
  uint8_t *appleImage;
  uint16_t appleImageWidth, appleImageHeight;

  bool use8875;
  
  BaseDisplay *tft;
};

#endif
