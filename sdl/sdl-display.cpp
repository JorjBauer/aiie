#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "bios-font.h"
#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"

#define SCREENINSET_X (18*2)
#define SCREENINSET_Y (13*2)

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
  memset(videoBuffer, 0, sizeof(videoBuffer));

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

// drawImageOfSizeAt will horizontally scale out the image b/c the
// images themselves aren't aware of the double resolution. This is an
// inconsistency that probably should be addressed. FIXME?
void SDLDisplay::drawImageOfSizeAt(const uint8_t *img,
				   uint16_t sizex, uint8_t sizey,
				   uint16_t wherex, uint8_t wherey)
{
  for (uint8_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      const uint8_t *p = &img[(y * sizex + x)*3];
      p = &img[(y * sizex + x)*3];
      drawPixel((x+wherex)*2, y+wherey, p[0], p[1], p[2]);
      drawPixel((x+wherex)*2+1, y+wherey, p[0], p[1], p[2]);
    }
  }
}

void SDLDisplay::blit()
{
  // Whole-screen blit - unimplemented here
}

// Blit the videoBuffer to the display device, in the rect given
void SDLDisplay::blit(AiieRect r)
{
  // The dimensions here are the dimensions of the variable screen --
  // not the border. We don't support updating the border *except* by
  // drawing directly to the screen device...

  for (uint16_t y=r.top*2; y<r.bottom*2; y++) {
    for (uint16_t x=r.left*2; x<r.right*2; x++) {
      uint8_t colorIdx = videoBuffer[y*SDLDISPLAY_WIDTH+x];

      uint8_t r, g, b;
      r = loresPixelColors[colorIdx][0];
      g = loresPixelColors[colorIdx][1];
      b = loresPixelColors[colorIdx][2];

      if (g_displayType == m_monochrome) {
	float fv = 0.2125 * r + 0.7154 * g + 0.0721 * b;
	r = b = 0;
	g = fv;
      }
      else if (g_displayType == m_blackAndWhite) {
	float fv = 0.2125 * r + 0.7154 * g + 0.0721 * b;
	r = g = b = fv;
      }

      SDL_SetRenderDrawColor(renderer, r, g, b, 255);
      SDL_RenderDrawPoint(renderer, x+SCREENINSET_X, y+SCREENINSET_Y);
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

void SDLDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  drawPixel(x*2, y, color);
  drawPixel(x*2+1, y, color);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;


  // Pixel-doubling vertically
  for (int yoff=0; yoff<2; yoff++) {
    putpixel(renderer, x, yoff+y*2, r, g, b);
  }
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  // Pixel-doubling vertically
  for (int yoff=0; yoff<2; yoff++) {
    putpixel(renderer, x, yoff+y*2, r, g, b);
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

void SDLDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x] = color;
  videoBuffer[((y*2)+1)*SDLDISPLAY_WIDTH+x] = color;
}

// "DoubleWide" means "please double the X because I'm in low-res width mode"
void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2] = color;
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2+1] = color;

  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2] = color;
  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2+1] = color;
}

void SDLDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorB, uint8_t colorA)
{
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2] = colorA;
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2+1] = colorA;

  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2+2] = colorB;
  videoBuffer[y*2*SDLDISPLAY_WIDTH+x*2+3] = colorB;

  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2] = colorA;
  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2+1] = colorA;

  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2+2] = colorB;
  videoBuffer[(y*2+1)*SDLDISPLAY_WIDTH+x*2+3] = colorB;
}


