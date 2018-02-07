#ifndef __FB_DISPLAY_H
#define __FB_DISPLAY_H

#include <stdlib.h>
#include <linux/fb.h>

#include "physicaldisplay.h"

class FBDisplay : public PhysicalDisplay {
 public:
  FBDisplay();
  virtual ~FBDisplay();

  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint8_t sizey, uint16_t wherex, uint8_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);
  virtual void flush();
  virtual void clrScr();
  
 private:
  int fb_fd;
  struct fb_fix_screeninfo finfo;
  struct fb_var_screeninfo vinfo;
  long screensize;
  uint8_t *fbp;
};

#endif
