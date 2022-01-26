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

#include "images.h"

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

  shellImage = NULL;
  d1OpenImage = d1ClosedImage = d2OpenImage = d2ClosedImage = NULL;
  appleImage = NULL;

  // Load the 9341 images                                                                                                              
  getImageInfoAndData(IMG_8875_SHELL, &shellWidth, &shellHeight, &shellImage);
  getImageInfoAndData(IMG_8875_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
  getImageInfoAndData(IMG_8875_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
  getImageInfoAndData(IMG_8875_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
  getImageInfoAndData(IMG_8875_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
  getImageInfoAndData(IMG_8875_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
  
  // FIXME: abstract constants
  screen = SDL_CreateWindow("Aiie!",
			    SDL_WINDOWPOS_UNDEFINED,
			    SDL_WINDOWPOS_UNDEFINED,
			    SDL_WIDTH, SDL_HEIGHT,
			    SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);

  // SDL_RENDERER_SOFTWARE because, at least on my Mac, this has some
  // serious issues with hardware accelerated drawing (flashing and crashing).
  renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_SOFTWARE);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // set to white
  SDL_RenderClear(renderer); // clear it to the selected color
  SDL_RenderPresent(renderer); // perform the render

  buffer = SDL_CreateTexture(renderer,
                           SDL_PIXELFORMAT_RGB888,
                           SDL_TEXTUREACCESS_STREAMING, 
                           SDL_WIDTH,
                           SDL_HEIGHT);
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

void SDLDisplay::drawUIImage(uint8_t imageIdx)
{
  switch (imageIdx) {
  case IMG_SHELL:
    drawImageOfSizeAt(shellImage, shellWidth, shellHeight, 0, 0);
    break;
  case IMG_D1OPEN:
    drawImageOfSizeAt(d1OpenImage, driveWidth, driveHeight, 4, 67);
    break;
  case IMG_D1CLOSED:
    drawImageOfSizeAt(d1ClosedImage, driveWidth, driveHeight, 4, 67);
    break;
  case IMG_D2OPEN:
    drawImageOfSizeAt(d2OpenImage, driveWidth, driveHeight, 4, 116);
    break;
  case IMG_D2CLOSED:
    drawImageOfSizeAt(d2ClosedImage, driveWidth, driveHeight, 4, 116);
    break;
  case IMG_APPLEBATTERY:
    // FIXME ***                                                                                                                         
    break;
  }
}

void SDLDisplay::drawImageOfSizeAt(const uint8_t *img,
				   uint16_t sizex, uint16_t sizey,
				   uint16_t wherex, uint16_t wherey)
{
  for (uint16_t y=0; y<sizey; y++) {
    const uint8_t *p = &img[(y * sizex)*3];
    for (uint16_t x=0; x<sizex; x++) {
      videoBuffer[(y+wherey)][(x+wherex)] = packColor32(p);
      p += 3;
    }
  }
}

void SDLDisplay::blit()
{
  uint32_t *pixels = NULL;
  int pitch = 0;
  SDL_LockTexture(buffer,
		  NULL,      // NULL means the *whole texture* here.
		  (void **)&pixels,
		  &pitch);
  // FIXME what if pitch isn't as expected? Should be width*4
  
  memcpy(pixels, videoBuffer, sizeof(videoBuffer));
  
  SDL_UnlockTexture(buffer);
  SDL_RenderCopy(renderer, buffer, NULL, NULL);
  SDL_RenderPresent(renderer);
}

// Blit the videoBuffer to the display device, in the rect given
void SDLDisplay::blit(AiieRect r)
{
  blit();
  /*
  if (overlayMessage[0]) {
    drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
  }

  SDL_RenderPresent(renderer);
  */
}

inline void putpixel(SDL_Renderer *renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDrawPoint(renderer, x, y);
}

void SDLDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (x >= SDL_WIDTH || y >= SDL_HEIGHT) return; // make sure it's onscreen
  
  videoBuffer[y][x] = color16To32(color);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;

  for (int yoff=0; yoff<2; yoff++) {
    putpixel(renderer, x, (y*2)+yoff, r, g, b);
  }
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  for (int yoff=0; yoff<2; yoff++) {
    putpixel(renderer, x, (y*2)+yoff, r, g, b);
  }
}

void SDLDisplay::drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c)
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

  // scale up the font
  const unsigned char *ch = asciiToAppleGlyph(c);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (*ch & (1 << (x_off))) {
        for (int8_t ys=0; ys<2; ys++) {
          for (int8_t xs=0; xs<2; xs++) {
            drawUIPixel((x + x_off)*2+xs, (y+y_off)*2 + ys, onPixel);
          }
        }
      } else {
        for (int8_t ys=0; ys<2; ys++) {
          for (int8_t xs=0; xs<2; xs++) {
            drawUIPixel((x + x_off)*2+xs, (y+y_off)*2 + ys, offPixel);
          }
        }
      }
    }
    ch++;
  }
}

void SDLDisplay::drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font
  
  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize;
  }
}

void SDLDisplay::clrScr(uint8_t coloridx)
{
  const uint8_t *rgbptr = &loresPixelColors[0][0];
  if (coloridx <= 16)
    rgbptr = loresPixelColors[coloridx];

  uint32_t packedColor = packColor32(loresPixelColors[coloridx]);
  
  for (uint16_t y=0; y<SDL_HEIGHT; y++) {
    for (uint16_t x=0; x<SDL_WIDTH; x++) {
      videoBuffer[y][x] = packedColor;
    }
  }
}

void SDLDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  for (int yoff=0; yoff<2; yoff++) {
    videoBuffer[(y*2)+SCREENINSET_8875_Y+yoff][x+SCREENINSET_8875_X] = packColor32(loresPixelColors[color]);
  }
}

// "DoubleWide" means "please double the X because I'm in low-res width mode"
void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      videoBuffer[(y*2)+SCREENINSET_8875_Y+yoff][(x*2)+SCREENINSET_8875_X+xoff] = packColor32(loresPixelColors[color]);
    }
  }
}

void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint32_t packedColor)
{
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      videoBuffer[(y*2)+SCREENINSET_8875_Y+yoff][(x*2)+SCREENINSET_8875_X+xoff] = packedColor;
    }
  }
}

void SDLDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, uint8_t colorB, uint8_t colorA)
{
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      videoBuffer[(y*2)+SCREENINSET_8875_Y+yoff][(x*2)+SCREENINSET_8875_X+xoff] = packColor32(loresPixelColors[colorA]);
      videoBuffer[(y*2)+SCREENINSET_8875_Y+yoff][(x+1)*2+SCREENINSET_8875_X+xoff] = packColor32(loresPixelColors[colorB]);
    }
  }
}

void SDLDisplay::windowResized(uint32_t w, uint32_t h)
{
  // Preserve the aspect ratio
  float aspectRatio = (float)w/(float)h;
  if (aspectRatio != ((float)SDL_WIDTH)/((float)SDL_HEIGHT)) {
    if (aspectRatio > ((float)SDL_WIDTH)/((float)SDL_HEIGHT)) {
      h = ((1.f * ((float)SDL_HEIGHT)) / ((float)SDL_WIDTH)) * w;
    } else {
      w = (((float)SDL_WIDTH)/((float)SDL_HEIGHT)) * h;
    }
  }
  
  SDL_SetWindowSize(screen, w, h);
}
