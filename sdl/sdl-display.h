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

  virtual void flush();

  virtual void drawUIImage(uint8_t imageIdx);
  virtual void drawDriveActivity(bool drive0, bool drive1);

  virtual void drawImageOfSizeAt(const uint8_t *img, uint16_t sizex, uint16_t sizey, uint16_t wherex, uint16_t wherey);

  virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color);
  virtual void drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

  virtual void clrScr(uint8_t coloridx);

  virtual void cachePixel(uint16_t x, uint16_t y, uint8_t color);
  virtual void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color);
  void cacheDoubleWidePixel(uint16_t x, uint16_t y, uint32_t packedColor);

  void windowResized(uint32_t w, uint32_t h);

 private:
  uint32_t videoBuffer[SDL_HEIGHT][SDL_WIDTH];

  SDL_Window *screen;
  SDL_Renderer *renderer;
  SDL_Texture *buffer;
  
  uint8_t *shellImage;
  uint16_t shellWidth, shellHeight;
  uint8_t *d1OpenImage;
  uint16_t driveWidth, driveHeight; // assume all the latches are the
                                    // same width/height no matter
                                    // what position
  uint8_t *d1ClosedImage;
  uint8_t *d2OpenImage;
  uint8_t *d2ClosedImage;
  uint8_t *appleImage;
  uint16_t appleImageWidth, appleImageHeight;

  bool driveIndicator[2];
};

#endif
