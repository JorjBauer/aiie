#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"
// FIXME should be able to omit this include and rely on the xterns, which
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
					  { 0xAC, 0x12, 0x4C }, // magenta
					  { 0x00, 0x07, 0x83 }, // dark blue
					  { 0xAA, 0x1A, 0xD1 }, // purple
					  { 0x00, 0x83, 0x2F }, // dark green
					  { 0x9F, 0x97, 0x7E }, // drak grey
					  { 0x00, 0x8A, 0xB5 }, // medium blue
					  { 0x9F, 0x9E, 0xFF }, // light blue
					  { 0x7A, 0x5F, 0x00 }, // brown
					  { 0xFF, 0x72, 0x47 }, // orange
					  { 0x78, 0x68, 0x7F }, // light gray
					  { 0xFF, 0x7A, 0xCF }, // pink
					  { 0x6F, 0xE6, 0x2C }, // green
					  { 0xFF, 0xF6, 0x7B }, // yellow
					  { 0x6C, 0xEE, 0xB2 }, // aqua
					  { 0xFF, 0xFF, 0xFF } // white
};

#define color16To32(x) ( (((x) & 0xF800) << 8) | (((x) & 0x07E0) << 5) | (((x) & 0x001F)<<3) )
#define packColor32(x) ( (x[0] << 16) | (x[1] << 8) | (x[2]) )
#define unpackRed(x) ( ((x) & 0xFF0000) >> 16 )
#define unpackGreen(x) ( ((x) & 0xFF00) >> 8 )
#define unpackBlue(x) ( ((x) & 0xFF) )
// FIXME this blend could be optimized - and it's a dumb blend that
// just averages RGB values individually, rather than trying to find a
// sane blend of 2 pixels. Needs thought.
#define blendPackedColor(x,y) ( (((unpackRed(x) + unpackRed(y))/2) << 16) + (((unpackGreen(x) + unpackGreen(y))/2) << 8) + ((unpackBlue(x) + unpackBlue(y))/2) )
#define luminanceFromRGB(r,g,b) ( ((r)*0.2126) + ((g)*0.7152) + ((b)*0.0722) )

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
  blit();
  SDL_RenderPresent(renderer);
}

void SDLDisplay::redraw()
{
  // primarily for the device, where it's in and out of the
  // bios. Draws the background image.
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
  // Whole-screen blit
  AiieRect r = { 0,0,191,279 };
  blit(r);
}

// Blit the videoBuffer to the display device, in the rect given
void SDLDisplay::blit(AiieRect r)
{
  // The dimensions here are the dimensions of the variable screen --
  // not the border. We don't support updating the border *except* by
  // drawing directly to the screen device...

  for (uint16_t y=r.top*SDLDISPLAY_SCALE; y<r.bottom*SDLDISPLAY_SCALE; y++) {
    for (uint16_t x=r.left*SDLDISPLAY_SCALE; x<r.right*SDLDISPLAY_SCALE; x++) {
      uint32_t colorIdx = videoBuffer[y][x];

      uint8_t r, g, b;
      r = (colorIdx & 0xFF0000) >> 16;
      g = (colorIdx & 0x00FF00) >>  8;
      b = (colorIdx & 0x0000FF);

      if (g_displayType == m_monochrome) {
	//	float fv = 0.2125 * r + 0.7154 * g + 0.0721 * b;
	//	r = b = 0;
	//	g = fv;
      }
      else if (g_displayType == m_blackAndWhite) {
	// Used to reduce to B&W in this driver, but now it's up in the apple display
	//	float fv = 0.2125 * r + 0.7154 * g + 0.0721 * b;
	//	r = g = b = fv;
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
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[y*SDLDISPLAY_SCALE+yoff][x*SDLDISPLAY_SCALE+xoff] = color16To32(color);
    }
  }
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

  for (uint16_t y=0; y<SDLDISPLAY_HEIGHT; y++) {
    for (uint16_t x=0; x<SDLDISPLAY_WIDTH; x++) {
      videoBuffer[y][x] = packColor32(loresPixelColors[coloridx]);
    }
  }

  SDL_SetRenderDrawColor(renderer, rgbptr[0], rgbptr[1], rgbptr[2], 255); // select a color
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render
}

// This was called with the expectation that it can draw every one of
// the 560x192 pixels that could be addressed. If TEENSYDISPLAY_SCALE
// is 1, then we have half of that horizontal resolution - so we need
// to be creative and blend neighboring pixels together.
void SDLDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
#if SDLDISPLAY_SCALE == 1
  // This is the case where we need to blend together neighboring
  // pixels, because we don't have enough physical screen resoultion.
  
  if (x&1) {
    uint32_t origColor = videoBuffer[y][(x>>1)*SDLDISPLAY_SCALE];
    uint32_t newColor = packColor32(loresPixelColors[color]);
    if (g_displayType == m_blackAndWhite) {
      // There are four reasonable decisions here: if either pixel
      // *was* on, then it's on; if both pixels *were* on, then it's
      // on; and if the blended value of the two pixels were on, then
      // it's on; or if the blended value of the two is above some
      // certain overall brightness, then it's on. This is the last of
      // those - where the brightness cutoff is defined in the bios as
      // g_luminanceCutoff.
      uint32_t blendedColor = blendPackedColor(origColor, newColor);
      uint32_t luminance = luminanceFromRGB(unpackRed(blendedColor),
					    unpackGreen(blendedColor),
					    unpackBlue(blendedColor));
      cacheDoubleWidePixel(x>>1,y,(uint32_t)((luminance >= g_luminanceCutoff) ? 0xFFFFFF : 0x000000));
    } else {
      cacheDoubleWidePixel(x>>1,y,(uint32_t)blendPackedColor(origColor, newColor));
    }
    // Else if it's black, we leave whatever was in the other pixel.
  } else {
    // The even pixels always draw.
    cacheDoubleWidePixel(x>>1,y,color);
  }
#else
  // we have enough resolution to show all the pixels, so just do it
  x = (x * SDLDISPLAY_SCALE)/2;
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)][x+xoff] = packColor32(loresPixelColors[color]);
    }
  }
#endif
  
}

// "DoubleWide" means "please double the X because I'm in low-res width mode"
void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)][x*SDLDISPLAY_SCALE+xoff] = packColor32(loresPixelColors[color]);
    }
  }
}

void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint32_t packedColor)
{
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)][x*SDLDISPLAY_SCALE+xoff] = packedColor;
    }
  }
}

void SDLDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorB, uint8_t colorA)
{
  for (int yoff=0; yoff<SDLDISPLAY_SCALE; yoff++) {
    for (int xoff=0; xoff<SDLDISPLAY_SCALE; xoff++) {
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)][x*SDLDISPLAY_SCALE+2*xoff] = packColor32(loresPixelColors[colorA]);
      videoBuffer[(y*SDLDISPLAY_SCALE+yoff)][x*SDLDISPLAY_SCALE+1+2*xoff] = packColor32(loresPixelColors[colorB]);
    }
  }
}


