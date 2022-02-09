#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"

#include "images.h"

#define RA8875_WIDTH 800
#define RA8875_HEIGHT 480
#define ILI9341_WIDTH 320
#define ILI9341_HEIGHT 240

// *** FIXME need a better blend
#define blendColors(a,b) (a | b)

extern bool use8875;

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
  driveIndicator[0] = driveIndicator[1] = true; // assume on so they will redraw the first time

  shellImage = NULL;
  d1OpenImage = d1ClosedImage = d2OpenImage = d2ClosedImage = NULL;
  appleImage = NULL;

  if (use8875) {
    videoBuffer = (uint32_t *)calloc(RA8875_HEIGHT * RA8875_WIDTH, sizeof(uint32_t));
    getImageInfoAndData(IMG_8875_SHELL, &shellWidth, &shellHeight, &shellImage);
    getImageInfoAndData(IMG_8875_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
    getImageInfoAndData(IMG_8875_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
    getImageInfoAndData(IMG_8875_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
    getImageInfoAndData(IMG_8875_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
    getImageInfoAndData(IMG_8875_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
  } else {
    videoBuffer = (uint32_t *)calloc(ILI9341_HEIGHT * ILI9341_WIDTH, sizeof(uint32_t));
    getImageInfoAndData(IMG_9341_SHELL, &shellWidth, &shellHeight, &shellImage);
    getImageInfoAndData(IMG_9341_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
    getImageInfoAndData(IMG_9341_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
  getImageInfoAndData(IMG_9341_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
  getImageInfoAndData(IMG_9341_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
  getImageInfoAndData(IMG_9341_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
  }
  
  
  // FIXME: abstract constants
  screen = SDL_CreateWindow("Aiie!",
			    SDL_WINDOWPOS_UNDEFINED,
			    SDL_WINDOWPOS_UNDEFINED,
                            use8875 ? RA8875_WIDTH : ILI9341_WIDTH,
                            use8875 ? RA8875_HEIGHT : ILI9341_HEIGHT,
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
                             use8875 ? RA8875_WIDTH : ILI9341_WIDTH,
                             use8875 ? RA8875_HEIGHT : ILI9341_HEIGHT);
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

void SDLDisplay::drawUIImage(uint8_t imageIdx)
{
  switch (imageIdx) {
  case IMG_SHELL:
    drawImageOfSizeAt(shellImage, shellWidth, shellHeight, 0, 0);
    break;
  case IMG_D1OPEN:
    drawImageOfSizeAt(d1OpenImage, driveWidth, driveHeight,
                      use8875 ? 4 : 55,
                      use8875 ? 67 : 216);
    break;
  case IMG_D1CLOSED:
    drawImageOfSizeAt(d1ClosedImage, driveWidth, driveHeight,
                      use8875 ? 4 : 189,
                      use8875 ? 116 : 216);
    break;
  case IMG_D2OPEN:
    drawImageOfSizeAt(d2OpenImage, driveWidth, driveHeight,
                      use8875 ? 4 : 189,
                      use8875 ? 116 : 216);
    break;
  case IMG_D2CLOSED:
    drawImageOfSizeAt(d2ClosedImage, driveWidth, driveHeight,
                      use8875 ? 4 : 189,
                      use8875 ? 116 : 216);
    break;
  case IMG_APPLEBATTERY:
    // FIXME ***
    break;
  }
}

void SDLDisplay::drawDriveActivity(bool drive0, bool drive1)
{
  if (drive0 != driveIndicator[0]) {
    printf("change d0\n");
    for (int y=0; y<(use8875 ? LED_HEIGHT_8875 : LED_HEIGHT_9341); y++) {
      for (int x=0; x<(use8875 ? LED_WIDTH_8875 : LED_WIDTH_9341); x++) {
        // FIXME this isn't working, not sure why
        drawPixel(x+(use8875 ? LED1_X_8875 : LED1_X_9341), y+(use8875 ? LED1_Y_8875 : LED1_Y_9341), drive0 ? 0xFA00 : 0x0000);
      }
    }
    driveIndicator[0] = drive0;
  }
  
  if (drive1 != driveIndicator[1]) {
    for (int y=0; y<(use8875 ? LED_HEIGHT_8875 : LED_HEIGHT_9341); y++) {
      for (int x=0; x<(use8875 ? LED_WIDTH_8875 : LED_WIDTH_9341); x++) {
        drawPixel(x+(use8875 ? LED2_X_8875 : LED2_X_9341), y+(use8875 ? LED2_Y_8875 : LED2_Y_9341), drive0 ? 0xFA00 : 0x0000);
      }
    }

    driveIndicator[1] = drive1;
  }
}

void SDLDisplay::drawImageOfSizeAt(const uint8_t *img,
				   uint16_t sizex, uint16_t sizey,
				   uint16_t wherex, uint16_t wherey)
{
  const uint8_t *p = img;
  for (uint16_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      uint16_t v = *p++;
      v<<=8;
      v |= *p++;
      videoBuffer[(y+wherey) * (use8875 ? RA8875_WIDTH : ILI9341_WIDTH) +
                  (x+wherex)] = color16To32(v);
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

  static uint32_t bufsize = 0;
  if (!bufsize) {
    bufsize = use8875 ? (RA8875_WIDTH*RA8875_HEIGHT*sizeof(uint32_t)) : (ILI9341_HEIGHT*ILI9341_WIDTH*sizeof(uint32_t));
  }
  memcpy(pixels, videoBuffer, bufsize);
  
  SDL_UnlockTexture(buffer);
  SDL_RenderCopy(renderer, buffer, NULL, NULL);
  SDL_RenderPresent(renderer);
}

inline void putpixel(SDL_Renderer *renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDrawPoint(renderer, x, y);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (use8875 && (x >= RA8875_WIDTH || y >= RA8875_HEIGHT))
    return;
  if ((!use8875) && (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT))
    return;
  
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;

  putpixel(renderer, x, y, r, g, b);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  if (use8875 && (x >= RA8875_WIDTH || y >= RA8875_HEIGHT))
    return;
  if ((!use8875) && (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT))
    return;

  putpixel(renderer, x, y, r, g, b);
}

void SDLDisplay::clrScr(uint8_t coloridx)
{
  const uint8_t *rgbptr = &loresPixelColors[0][0];
  if (coloridx <= 16)
    rgbptr = loresPixelColors[coloridx];

  uint32_t packedColor = packColor32(loresPixelColors[coloridx]);

  for (uint16_t y=0; y<(use8875 ? RA8875_HEIGHT : ILI9341_HEIGHT); y++) {
    for (uint16_t x=0; x<(use8875 ? RA8875_WIDTH : ILI9341_WIDTH); x++) {
      videoBuffer[y*(use8875 ? RA8875_WIDTH : ILI9341_WIDTH) + x] = packedColor;
    }
  }
}

void SDLDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      videoBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH +
                  x+SCREENINSET_8875_X] = packColor32(loresPixelColors[color]);
    }
  } else {
    if (x&1) {
      uint32_t origColor =videoBuffer[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH+(x>>1)+SCREENINSET_9341_X];
      if (g_displayType == m_blackAndWhite) {
        uint32_t blendedColor = blendColors(origColor, color);
        uint32_t luminance = luminanceFromRGB((blendedColor & 0xFF0000)>>16,
                                              (blendedColor & 0x00FF00)>> 8,
                                              (blendedColor & 0x0000FF));
        cacheDoubleWidePixel(x>>1,y,(uint32_t)((luminance >= g_luminanceCutoff) ? 0xFFFFFF : 0x000000));
      } else {
        cacheDoubleWidePixel(x>>1, y, color);
      }
      
    } else {
      // All of the even pixels get drawn...
      cacheDoubleWidePixel(x>>1, y, color);
    }
  }
}

// "DoubleWide" means "please double the X because I'm in low-res width mode"
void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      for (int xoff=0; xoff<2; xoff++) {
        videoBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH +
                    (x*2)+SCREENINSET_8875_X+xoff] = packColor32(loresPixelColors[color]);
      }
    }
  } else {
    videoBuffer[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH + (x) + SCREENINSET_9341_X] = packColor32(loresPixelColors[color]);
  }
}

void SDLDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint32_t packedColor)
{
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      for (int xoff=0; xoff<2; xoff++) {
        videoBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH +
                    (x*2)+SCREENINSET_8875_X+xoff] = packedColor;
      }
    }
  } else {
    videoBuffer[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH + (x) + SCREENINSET_9341_X] = packedColor;
  }
}

void SDLDisplay::windowResized(uint32_t w, uint32_t h)
{
  // Preserve the aspect ratio
  float aspectRatio = (float)w/(float)h;

  float expectedWidth = use8875 ? RA8875_WIDTH : ILI9341_WIDTH;
  float expectedHeight = use8875 ? RA8875_HEIGHT : ILI9341_HEIGHT;
  
  if (aspectRatio != ((float)expectedWidth)/((float)expectedHeight)) {
    if (aspectRatio > ((float)expectedWidth)/((float)expectedHeight)) {
      h = ((1.f * ((float)expectedHeight)) / ((float)expectedWidth)) * w;
    } else {
      w = (((float)expectedWidth)/((float)expectedHeight)) * h;
    }
  }
  
  SDL_SetWindowSize(screen, w, h);
}
