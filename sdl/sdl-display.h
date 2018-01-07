#ifndef __SDL_DISPLAY_H
#define __SDL_DISPLAY_H

#include <stdlib.h>

#include <SDL.h>
#include <SDL_mutex.h>
#include <SDL_events.h>

#include "physicaldisplay.h"


#define SDLDISPLAY_WIDTH (320*2)
#define SDLDISPLAY_HEIGHT (240*2)

enum {
  M_NORMAL = 0,
  M_SELECTED = 1,
  M_DISABLED = 2,
  M_SELECTDISABLED = 3
};

class SDLDisplay : public PhysicalDisplay {
 public:
  SDLDisplay();
  virtual ~SDLDisplay();

  virtual void blit(AiieRect r);
  virtual void redraw();

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint8_t sizey, uint16_t wherex, uint8_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);
  virtual void debugMsg(const char *msg);

 private:
  SDL_Window *screen;
  SDL_Renderer *renderer;
};

#endif
