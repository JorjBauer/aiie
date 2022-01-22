#ifndef __SDL_DISPLAY_H
#define __SDL_DISPLAY_H

#include <stdlib.h>

#include <SDL.h>
#include <SDL_events.h>

#include "physicaldisplay.h"

#define SDL_WIDTH 800
#define SDL_HEIGHT 480

class SDLDisplay : public PhysicalDisplay {
 public:
  SDLDisplay();
  virtual ~SDLDisplay();

  virtual void blit();
  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void flush();

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint16_t sizey, uint16_t wherex, uint16_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void drawUIPixel(uint16_t x, uint16_t y, uint16_t color);

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str);
  virtual void clrScr(uint8_t coloridx);

  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint32_t packedColor);
  virtual void cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorA, uint8_t colorB);

  void windowResized(uint32_t w, uint32_t h);

 private:
  uint32_t videoBuffer[SDL_HEIGHT][SDL_WIDTH];

  SDL_Window *screen;
  SDL_Renderer *renderer;
  SDL_Texture *buffer;
};

#endif
