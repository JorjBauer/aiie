#include <ctype.h> // isgraph
#include "sdl-display.h"

#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"

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

  driveIndicator[0] = driveIndicator[1] = true; // assume on so they will redraw the first time

  shellImage = NULL;
  d1OpenImage = d1ClosedImage = d2OpenImage = d2ClosedImage = NULL;
  appleImage = NULL;

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

void SDLDisplay::drawDriveActivity(bool drive0, bool drive1)
{
  if (drive0 != driveIndicator[0]) {
    printf("change d0\n");
    for (int y=0; y<LED_HEIGHT_8875; y++) {
      for (int x=0; x<LED_WIDTH_8875; x++) {
        // FIXME this isn't working, not sure why
        drawPixel(x+LED1_X_8875, y+LED1_Y_8875, 0xFF, 0, 0); ///*drive0 ?*/ 0xFA00/* : 0x0000*/);
      }
    }
    driveIndicator[0] = drive0;
  }
  
  if (drive1 != driveIndicator[1]) {
    for (int y=0; y<LED_HEIGHT_8875; y++) {
      for (int x=0; x<LED_WIDTH_8875; x++) {
        drawPixel(x+LED2_X_8875, y+LED2_Y_8875, drive0 ? 0xFA00 : 0x0000);
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
      videoBuffer[(y+wherey)][(x+wherex)] = color16To32(v);
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

inline void putpixel(SDL_Renderer *renderer, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDrawPoint(renderer, x, y);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (x >= SDL_WIDTH || y >= SDL_HEIGHT) return; // make sure it's onscreen
  
  uint8_t
    r = (color & 0xF800) >> 8,
    g = (color & 0x7E0) >> 3,
    b = (color & 0x1F) << 3;

  putpixel(renderer, x, y, r, g, b);
}

void SDLDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  if (x < SDL_WIDTH && y < SDL_HEIGHT) {
    putpixel(renderer, x, y, r, g, b);
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
