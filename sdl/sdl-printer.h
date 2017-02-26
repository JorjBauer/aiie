#ifndef __SDL_PRINTER_H
#define __SDL_PRINTER_H

#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_events.h>

#include "physicalprinter.h"

#define HEIGHT 800
#define NATIVEWIDTH 960 // FIXME: printer can change density...                                                                                                            


//#define WIDTH 384 // emulating the teeny printer I've got                                                                                                                
#define WIDTH 960


class SDLPrinter : public PhysicalPrinter {
 public:
  SDLPrinter();
  virtual ~SDLPrinter();

  virtual void addLine(uint8_t *rowOfBits); // must be 960 pixels wide (120 bytes)

  virtual void update();

  virtual void moveDownPixels(uint8_t p);

 private:
  bool isDirty;
  uint16_t ypos;

  SDL_Window *window;
  SDL_Renderer *renderer;
  bool _hackyBitmap[WIDTH * HEIGHT];

};

#endif
