#include "sdl-printer.h"

#define WINDOWNAME "printer"

inline void putpixel(SDL_Renderer *renderer, int x, int y, uint8_t d)
{
  uint8_t v = (d ? 0xFF : 0x00);

  SDL_SetRenderDrawColor(renderer, v, v, v, 255);
  SDL_RenderDrawPoint(renderer, x, y);
}

SDLPrinter::SDLPrinter()
{
  ypos = 0;
  isDirty = false;

  memset((void *)_hackyBitmap, 0, sizeof(_hackyBitmap));

  window = SDL_CreateWindow(WINDOWNAME,
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            WIDTH, HEIGHT,
                            SDL_WINDOW_SHOWN);

  // SDL_RENDERER_SOFTWARE because, at least on my Mac, this has some
  // serious issues with hardware accelerated drawing (flashing and
  // crashing).
  renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render
}

SDLPrinter::~SDLPrinter()
{
}

void SDLPrinter::update()
{
  if (isDirty) {
    isDirty = false; // set early in case there's a race

    for (int y=0; y<HEIGHT; y++) {
      for (int x=0; x<WIDTH; x++) {
	if (_hackyBitmap[y*WIDTH+x]) {
	  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	} else {
	  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	}
	SDL_RenderDrawPoint(renderer, x, y);
      }
    }

    SDL_RenderPresent(renderer);
  }
}

void SDLPrinter::addLine(uint8_t *rowOfBits)
{
  for (int yoff=0; yoff<9; yoff++) {
    // 960 pixels == 120 bytes -- FIXME
    for (int i=0; i<(NATIVEWIDTH/8); i++) {
      uint8_t bv = rowOfBits[yoff*120+i];
      for (int xoff=0; xoff<8; xoff++) {
	// scale X from "actual FX80" coordinates to "real printer" coordinates
	uint16_t actualX = (uint16_t)(((float)(i*8+xoff) * (float)WIDTH) / (float)NATIVEWIDTH);
	uint8_t pixelColor = (bv & (1 << (7-xoff))) ? 0xFF : 0x00;
	// Make sure to preserve any pixels we've already drawn b/c scaling & overstrike...
	_hackyBitmap[actualX + ((ypos+yoff)%HEIGHT) * WIDTH] |= pixelColor;
      }
    }
  }

  if (ypos >= HEIGHT) {
    ypos = 0;
  }

  isDirty = true;
}

void SDLPrinter::moveDownPixels(uint8_t p)
{
  ypos+= p;
  if (ypos >= HEIGHT) {
    // clear page & restart
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    isDirty = true;
    ypos = 0;
  }
}
