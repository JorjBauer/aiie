#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"
// FIXME should be able to omit this include and relay on the xterns, which
// would prove it's linking properly
#include "apple/font.h"
extern const unsigned char ucase_glyphs[512];
extern const unsigned char lcase_glyphs[256];
extern const unsigned char mousetext_glyphs[256];
extern const unsigned char interface_glyphs[256];

#define SCREENINSET_X (18*SDLDISPLAY_SCALE)
#define SCREENINSET_Y (13*SDLDISPLAY_SCALE)

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
      drawPixel((x+wherex)*SDLDISPLAY_SCALE, y+wherey, p[0], p[1], p[2]);
      drawPixel((x+wherex)*SDLDISPLAY_SCALE+1, y+wherey, p[0], p[1], p[2]);
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

  for (uint16_t y=r.top*SDLDISPLAY_SCALE; y<r.bottom*SDLDISPLAY_SCALE; y++) {
    for (uint16_t x=r.left*SDLDISPLAY_SCALE; x<r.right*SDLDISPLAY_SCALE; x++) {
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
  drawPixel(x*SDLDISPLAY_SCALE, y, color);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;

  // Pixel-doubling vertically and horizontally, based on scale
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      putpixel(renderer, x+xoff, yoff+y*SDLDISPLAY_SCALE, r, g, b);
    }
  }
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  // Pixel-doubling horizontally and vertically, based on scale
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      putpixel(renderer, x+xoff, yoff+y*SDLDISPLAY_SCALE, r, g, b);
    }
  }
}

void SDLDisplay::drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c)
{
  int8_t xsize = 8,
    ysize = 0x07;
  
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
  case M_PLAIN:
    onPixel = 0xFFFF;
    offPixel = 0x0000;
    break;
  }


  // This does not scale when drawing, because drawPixel scales.
  const unsigned char *ch = asciiToAppleGlyph(c);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (*ch & (1 << (x_off))) {
	drawUIPixel(x + x_off, y + y_off, onPixel);
      } else {
	drawUIPixel(x + x_off, y + y_off, offPixel);
      }
    }
    ch++;
  }
}

void SDLDisplay::drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font
  
  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?
    if (x >= 320) break; // FIXME constant - and pre-scaling, b/c that's in drawCharacter
  }
}

void SDLDisplay::clrScr(uint8_t coloridx)
{
  const uint8_t *rgbptr = &loresPixelColors[0][0];
  if (coloridx <= 16)
    rgbptr = loresPixelColors[coloridx];

  SDL_SetRenderDrawColor(renderer, rgbptr[0], rgbptr[1], rgbptr[2], 255); // select a color
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render
}

// This was called with the expectation that it can draw every one of
// the 560x192 pixels that could be addressed. The SDLDISPLAY_SCALE is
// basically half the X scale - so a 320-pixel-wide screen can show 40
// columns fine, which means that we need to be creative for 80 columns,
// which need to be alpha-blended...
void SDLDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  if (SDLDISPLAY_SCALE == 1) {
    // we need to alpha blend the X because there aren't enough screen pixels.
    // This takes advantage of the fact that we always call this linearly
    // for the 80-column text -- we never (?) do partial screen blits, but
    // always wind up redrawing the entirety. So we can look at the pixel in
    // the "shared" cell of RAM, and come up with a color between the two.
    if (x&1) {
      uint8_t origColor = videoBuffer[(y*SDLDISPLAY_SCALE)*SDLDISPLAY_WIDTH+(x>>1)*SDLDISPLAY_SCALE];

      uint8_t newColor = (uint16_t) (origColor + color) / 2;

      cacheDoubleWidePixel(x>>1,y,newColor);
      // Else if it's black, we leave whatever was in the other pixel.
    } else {
      // The even pixels always draw.
      cacheDoubleWidePixel(x>>1,y,color);
    }
    
  } else {
    // we have enough resolution to show all the pixels, so just do it
    x = (x * SDLDISPLAY_SCALE)/2;
    for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
      for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
	videoBuffer[(y*SDLDISPLAY_SCALE+yoff)*SDLDISPLAY_WIDTH+x+xoff] = color;
      }
    }
  }
}

// "DoubleWide" means "please double the X because I'm in low-res width mode"
void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)*SDLDISPLAY_WIDTH+x*SDLDISPLAY_SCALE+xoff] = color;
    }
  }
}

void SDLDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorB, uint8_t colorA)
{
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)*SDLDISPLAY_WIDTH+x*SDLDISPLAY_SCALE+2*xoff] = colorA;
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)*SDLDISPLAY_WIDTH+x*SDLDISPLAY_SCALE+1+2*xoff] = colorB;
    }
  }
}


