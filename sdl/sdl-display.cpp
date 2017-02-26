#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "bios-font.h"
#include "display-bg.h"

#include "globals.h"
#include "applevm.h"

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
			    320*2, 240*2,
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

void SDLDisplay::redraw()
{
  // primarily for the device, where it's in and out of the
  // bios. Draws the background image.
  printf("redraw background\n");

  for (int y=0; y<240; y++) {
    for (int x=0; x<320; x++) {
      uint8_t *p = &displayBitmap[(y * 320 + x)*3];
      drawPixel(x, y, p[0], p[1], p[2]);
    }
  }

  if (g_vm) {
    drawDriveDoor(0, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    drawDriveDoor(1, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }
}

void SDLDisplay::drawDriveStatus(uint8_t which, bool isRunning)
{
  // FIXME: this is a draw from another thread. Can't do that with SDL.
  return;

  // location of status indicator for left drive
  uint16_t xoff = 125;
  uint16_t yoff = 213;

  // and right drive
  if (which == 1)
    xoff += 135;

  for (int y=0; y<1; y++) {
    for (int x=0; x<6; x++) {
      drawPixel(x + xoff, y + yoff, isRunning ? 0xF800 : 0x8AA9);
    }
  }

}

void SDLDisplay::drawDriveDoor(uint8_t which, bool isOpen)
{
  // location of drive door for left drive
  uint16_t xoff = 55;
  uint16_t yoff = 216;

  // location for right drive
  if (which == 1) {
    xoff += 134;
  }

  for (int y=0; y<20; y++) {
    for (int x=0; x<43; x++) {
      uint8_t *p = &driveLatch[(y * 43 + x)*3];
      if (isOpen) {
	p = &driveLatchOpen[(y * 43 + x)*3];
      }
      drawPixel(x+xoff, y+yoff, p[0], p[1], p[2]);
    }
  }
}

void SDLDisplay::drawBatteryStatus(uint8_t percent)
{
  uint16_t xoff = 300;
  uint16_t yoff = 222;

  // the area around the apple is 12 wide
  // it's exactly 11 high
  // the color is 210/202/159

  float watermark = ((float)percent / 100.0) * 11;

  for (int y=0; y<11; y++) {
    uint8_t bgr = 210;
    uint8_t bgg = 202;
    uint8_t bgb = 159;

    if (11-y > watermark) {
      // black...
      bgr = bgg = bgb = 0;
    }
    
    for (int x=0; x<11; x++) {
      uint8_t *p = &appleBitmap[(y * 10 + (x-1))*4];
      // It's RGBA; blend w/ background color

      uint8_t r,g,b;
      float alpha = (float)p[3] / 255.0;
      r = (float)p[0] * alpha + (bgr * (1.0 - alpha));
      g = (float)p[1] * alpha + (bgg * (1.0 - alpha));
      b = (float)p[2] * alpha + (bgb * (1.0 - alpha));

      drawPixel(x+xoff, y+yoff, r, g, b);
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
      uint16_t pixel = (y*320+x)/2;
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

void SDLDisplay::drawPixel(uint16_t x, uint8_t y, uint16_t color)
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

void SDLDisplay::drawPixel(uint16_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b)
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

void SDLDisplay::debugMsg(const char *msg)
{
  printf("%s\n", msg);
}

