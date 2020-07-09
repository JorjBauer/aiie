#ifndef __SDL_DISPLAY_H
#define __SDL_DISPLAY_H

#include <stdlib.h>

#include <SDL.h>
#include <SDL_events.h>

#include "physicaldisplay.h"

// scale can be 1,2,4. '1' is half-width at the highest resolution
// (80-col mode). '2' is full width. '4' is double full width.
#define SDLDISPLAY_SCALE 1
#define SDLDISPLAY_WIDTH (320*SDLDISPLAY_SCALE)
#define SDLDISPLAY_HEIGHT (240*SDLDISPLAY_SCALE)

class SDLDisplay : public PhysicalDisplay {
 public:
  SDLDisplay();
  virtual ~SDLDisplay();

  virtual void blit();
  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void flush();

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint8_t sizey, uint16_t wherex, uint8_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void drawUIPixel(uint16_t x, uint16_t y, uint16_t color);

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);
  virtual void clrScr();

  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorA, uint8_t colorB);


 private:
  uint8_t videoBuffer[SDLDISPLAY_HEIGHT * SDLDISPLAY_WIDTH];

  SDL_Window *screen;
  SDL_Renderer *renderer;
};

#endif
