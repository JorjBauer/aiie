#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "bios-font.h"
#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"

// RGB map of each of the lowres colors
const uint8_t loresPixelColors[16][3] = { { 0, 0, 0 }, // black
					  { 195, 0, 48 }, // magenta
					  { 0, 0, 130 }, // dark blue
					  { 166, 52, 170 }, // purple
					  { 0, 146, 0 }, // dark green
					  { 105, 105, 105 }, // drak grey
					  { 24, 113, 255 }, // medium blue
					  { 12, 190, 235 }, // light blue
					  { 150, 85, 40 }, // brown
					  { 255, 24, 44 }, // orange
					  { 150, 170, 170 }, // light gray
					  { 255, 158, 150 }, // pink
					  { 0, 255, 0 }, // green
					  { 255, 255, 0 }, // yellow
					  { 130, 255, 130 }, // aqua
					  { 255, 255, 255 } // white
};

SDLDisplay::SDLDisplay()
{
  // FIXME: abstract constants
  screen = SDL_CreateWindow("Aiie!",
			    SDL_WINDOWPOS_UNDEFINED,
			    SDL_WINDOWPOS_UNDEFINED,
			    SDLDISPLAY_WIDTH, SDLDISPLAY_HEIGHT,
			    SDL_WINDOW_SHOWN);

  // SDL_RENDERER_SOFTWARE because, at least on my Mac, this has some
  // serious issues with hardware accelerated drawing (flashing and crashing).
  renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_SOFTWARE);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // set to white
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render
}

SDLDisplay::~SDLDisplay()
{
  SDL_Quit();
}

void SDLDisplay::flush()
{
  SDL_RenderPresent(renderer);
}

void SDLDisplay::redraw()
{
  // primarily for the device, where it's in and out of the
  // bios. Draws the background image.
  printf("redraw background\n");
  g_ui->drawStaticUIElement(UIeOverlay);

  if (g_vm && g_ui) {
    // determine whether or not a disk is inserted & redraw each drive
    g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }
}

void SDLDisplay::drawImageOfSizeAt(const uint8_t *img,
				   uint16_t sizex, uint8_t sizey,
				   uint16_t wherex, uint8_t wherey)
{
  for (uint8_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      const uint8_t *p = &img[(y * sizex + x)*3];
      p = &img[(y * sizex + x)*3];
      drawPixel(x+wherex, y+wherey, p[0], p[1], p[2]);
    }
  }
}

#define BASEX 36
#define BASEY 26

void SDLDisplay::blit(AiieRect r)
{
  uint8_t *videoBuffer = g_vm->videoBuffer; // FIXME: poking deep

  for (uint8_t y=0; y<192; y++) {
    for (uint16_t x=0; x<280; x++) {
      uint16_t pixel = (y*DISPLAYRUN+x)/2;
      uint8_t colorIdx;
      if (x & 1) {
	colorIdx = videoBuffer[pixel] & 0x0F;
      } else {
	colorIdx = videoBuffer[pixel] >> 4;
      }
      for (uint8_t xoff=0; xoff<2; xoff++) {
	for (uint8_t yoff=0; yoff<2; yoff++) {
	  // FIXME: validate BPP >= 3?
	  SDL_SetRenderDrawColor(renderer, loresPixelColors[colorIdx][0], loresPixelColors[colorIdx][1], loresPixelColors[colorIdx][2], 255);
	  SDL_RenderDrawPoint(renderer, x*2+xoff+BASEX, y*2+yoff+BASEY);
	}
      }
    }
  }

  if (overlayMessage[0]) {
    drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
  }

  SDL_RenderPresent(renderer);
}

inline void putpixel(SDL_Renderer *renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDrawPoint(renderer, x, y);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;


  // Pixel-doubling
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      putpixel(renderer, xoff+x*2, yoff+y*2, r, g, b);
    }
  }
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  // Pixel-doubling
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      putpixel(renderer, xoff+x*2, yoff+y*2, r, g, b);
    }
  }
}

void SDLDisplay::drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c)
{
  int8_t xsize = 8,
    ysize = 0x0C,
    offset = 0x20;
  uint16_t temp;

  c -= offset;// font starts with a space                                                     

  uint16_t offPixel, onPixel;
  switch (mode) {
  case M_NORMAL:
    onPixel = 0xFFFF;
    offPixel = 0x0010;
    break;
  case M_SELECTED:
    onPixel = 0x0000;
    offPixel = 0xFFFF;
    break;
  case M_DISABLED:
  default:
    onPixel = 0x7BEF;
    offPixel = 0x0000;
    break;
  case M_SELECTDISABLED:
    onPixel = 0x7BEF;
    offPixel = 0xFFE0;
    break;
  }

  temp=(c*ysize);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    uint8_t ch = BiosFont[temp];
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (ch & (1 << (7-x_off))) {
	drawPixel(x + x_off, y + y_off, onPixel);
      } else {
	drawPixel(x + x_off, y + y_off, offPixel);
      }
    }
    temp++;
  }

}

void SDLDisplay::drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font
  
  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?
  }
}

void SDLDisplay::clrScr()
{
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // set to white
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render
}

