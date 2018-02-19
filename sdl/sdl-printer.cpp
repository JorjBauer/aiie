#include "sdl-printer.h"
#include <stdio.h>

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

  window = NULL;
  renderer = NULL;

  printerMutex = SDL_CreateMutex();
  currentPageNumber = 0;
}

SDLPrinter::~SDLPrinter()
{
  SDL_DestroyMutex(printerMutex);
}

void SDLPrinter::update()
{
  if (isDirty) {
    // If SDL_TryLockMutex returns 0, then it locked the mutex and
    // we'll continue. Otherwise, assume we're drawing something
    // complicated and don't gum up the works...
    if (SDL_TryLockMutex(printerMutex))
      return;

    isDirty = false; // set early in case there's a race

    if (!window) {
      window = SDL_CreateWindow(WINDOWNAME,
				SDL_WINDOWPOS_UNDEFINED,
				SDL_WINDOWPOS_UNDEFINED,
				WIDTH, HEIGHT,
				SDL_WINDOW_SHOWN);
      
      renderer = SDL_CreateRenderer(window, -1, 0);
    }

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
    SDL_UnlockMutex(printerMutex);

    SDL_RenderPresent(renderer);
  }
}

void SDLPrinter::addLine(uint8_t *rowOfBits)
{
  SDL_LockMutex(printerMutex);
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
  SDL_UnlockMutex(printerMutex);
}

void SDLPrinter::moveDownPixels(uint8_t p)
{
  SDL_LockMutex(printerMutex);
  ypos+= p;
  if (ypos >= HEIGHT) {
    savePageAsBitmap(++currentPageNumber);

    // clear page & restart
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    
    for (int y=0; y<HEIGHT; y++) {
      for (int x=0; x<WIDTH; x++) {
	_hackyBitmap[y*WIDTH+x] = 0;
      }
    }

    isDirty = true;
    ypos = 0;
  }
  SDL_UnlockMutex(printerMutex);
}

void SDLPrinter::savePageAsBitmap(uint32_t pageno)
{
  SDL_Surface* saveSurface = NULL;

  char buf[255];
  sprintf(buf, "page-%d.bmp", pageno);

  saveSurface = SDL_CreateRGBSurfaceFrom((void *)_hackyBitmap, 
					 WIDTH,
					 HEIGHT,
					 8, // bpp
					 WIDTH * 1, // bytes per row

					 0xE0, // rmask
					 0x1C, // gmask
					 0x03, // bmask
					 0x00  // amask
					 );
  if (saveSurface) {
    SDL_SaveBMP(saveSurface, buf);
    SDL_FreeSurface(saveSurface);
  } else {
    printf("Failed to create saveSurface\n");
  }
}

