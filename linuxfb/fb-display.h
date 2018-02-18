#ifndef __FB_DISPLAY_H
#define __FB_DISPLAY_H

#include <stdlib.h>
#include <linux/fb.h>

#include "physicaldisplay.h"

#define FBDISPLAY_WIDTH (320*2)
#define FBDISPLAY_HEIGHT (240*2)

class FBDisplay : public PhysicalDisplay {
 public:
  FBDisplay();
  virtual ~FBDisplay();

  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint8_t sizey, uint16_t wherex, uint8_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void drawUIPixel(uint16_t x, uint16_t y, uint16_t color);

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);
  virtual void flush();
  virtual void clrScr();

  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorA, uint8_t colorB);
				      
  
 private:
  volatile uint8_t videoBuffer[FBDISPLAY_HEIGHT * FBDISPLAY_WIDTH];

  int fb_fd;
  struct fb_fix_screeninfo finfo;
  struct fb_var_screeninfo vinfo;
  long screensize;
  uint8_t *fbp;
};

#endif
