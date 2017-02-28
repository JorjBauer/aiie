#ifndef __SDL_DISPLAY_H
#define __SDL_DISPLAY_H

#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_events.h>

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

  virtual void drawDriveDoor(uint8_t which, bool isOpen);
  virtual void setDriveIndicator(uint8_t which, bool isRunning);
  virtual void drawBatteryStatus(uint8_t percent);

  void drawPixel(uint16_t x, uint8_t y, uint16_t color);
  void drawPixel(uint16_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);
  virtual void drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c);
  virtual void drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str);
  virtual void debugMsg(const char *msg);

 private:
  SDL_Window *screen;
  SDL_Renderer *renderer;

  bool driveIndicator[2];
  bool driveIndicatorDirty;
};

#endif
