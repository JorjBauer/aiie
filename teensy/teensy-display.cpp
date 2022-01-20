#include <ctype.h> // isgraph

#include "teensy-display.h"
#include "iocompat.h"

#include "appleui.h"
// FIXME should be able to omit this include and rely on the xterns, which
// would prove it's linking properly
#include "font.h"
extern const unsigned char ucase_glyphs[512];
extern const unsigned char lcase_glyphs[256];
extern const unsigned char mousetext_glyphs[256];
extern const unsigned char interface_glyphs[256];

#include "globals.h"
#include "applevm.h"

#include "images.h"

#ifndef RA8875_HEIGHT
#define RA8875_HEIGHT 480
#endif

DMAMEM uint8_t dmaBuffer[RA8875_HEIGHT][RA8875_WIDTH];

#include <SPI.h>
#define _clock 20000000u // FIXME bring this up - it's under the default now

#define PIN_RST 8
#define PIN_DC 9
#define PIN_CS 0
#define PIN_MOSI 26
#define PIN_MISO 1
#define PIN_SCK 27

// RGB map of each of the lowres colors
const uint16_t loresPixelColors[16] = { 0x0000, // 0 black
					 0xC006, // 1 magenta
					 0x0010, // 2 dark blue
					 0xA1B5, // 3 purple
					 0x0480, // 4 dark green
					 0x6B4D, // 5 dark grey
					 0x1B9F, // 6 med blue
					 0x0DFD, // 7 light blue
					 0x92A5, // 8 brown
					 0xF8C5, // 9 orange
					 0x9555, // 10 light gray
					 0xFCF2, // 11 pink
					 0x07E0, // 12 green
					 0xFFE0, // 13 yellow
					 0x87F0, // 14 aqua
					 0xFFFF  // 15 white
};

#define RGBto565(r,g,b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define _565toR(c) ( ((c) & 0xF800) >> 8 )
#define _565toG(c) ( ((c) & 0x07E0) >> 3 )
#define _565toB(c) ( ((c) & 0x001F) << 3 )
#define RGBto332(r,g,b) ((((r) & 0xE0)) | (((g) & 0xE0) >> 3) | ((b) >> 6))
#define luminanceFromRGB(r,g,b) ( ((r)*0.2126) + ((g)*0.7152) + ((b)*0.0722) )
#define _565To332(c) ((((c) & 0xe000) >> 8) | (((c) & 0x700) >> 6) | (((c) & 0x18) >> 3))


RA8875_t4 tft = RA8875_t4(PIN_CS, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);

TeensyDisplay::TeensyDisplay()
{
  //  tft.begin(Adafruit_800x480);
  tft.begin(_clock);
  tft.fillWindow();
  tft.setFrameBuffer((uint8_t *)dmaBuffer);
  
  driveIndicator[0] = driveIndicator[1] = false;
  driveIndicatorDirty = true;
}

TeensyDisplay::~TeensyDisplay()
{
}

void TeensyDisplay::flush()
{
  tft.updateScreenAsync(false);
}

void TeensyDisplay::redraw()
{
  g_ui->drawStaticUIElement(UIeOverlay);

  if (g_vm && g_ui) {
    g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }
}

void TeensyDisplay::drawImageOfSizeAt(const uint8_t *img, 
				      uint16_t sizex, uint16_t sizey, 
				      uint16_t wherex, uint16_t wherey)
{
  uint8_t r, g, b;

  for (uint16_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      r = pgm_read_byte(&img[(y*sizex + x)*3 + 0]);
      g = pgm_read_byte(&img[(y*sizex + x)*3 + 1]);
      b = pgm_read_byte(&img[(y*sizex + x)*3 + 2]);
      dmaBuffer[y+wherey][x+wherex] = RGBto332(r,g,b);
    }
  }
}

void TeensyDisplay::blit()
{
  // Start updates, if they're not running already
  //  if (!tft.asyncUpdateActive())
  //    tft.updateScreenAsync(true);
  // DEBUGGING: not refreshing every time so I can see the machine boot
  static uint32_t ctr = 0;
  if (((ctr++) & 0x0F) == 0)
    tft.updateScreenAsync(false);
  
  // draw overlay, if any, occasionally
  {
    static uint32_t nextMessageTime = 0;
    if (millis() >= nextMessageTime) {
      if (overlayMessage[0]) {
	drawString(M_SELECTDISABLED, 1, RA8875_HEIGHT - (16 + 12), overlayMessage);
      }
      nextMessageTime = millis() + 10; // DEBUGGING FIXME make 1000 again
    }
  }
}

void TeensyDisplay::blit(AiieRect r)
{
}

void TeensyDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  dmaBuffer[y][x] = _565To332(color);
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  dmaBuffer[y][x] = _565To332(color);
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t color16 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

  drawPixel(x,y,color16);
}

void TeensyDisplay::drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c)
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

void TeensyDisplay::drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font                                                 

  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?                                                   
    if (x >= 320) break; // FIXME constant - and pre-scaling, b/c that's in drawCharacter           
  }
}

void TeensyDisplay::clrScr(uint8_t coloridx)
{
  if (coloridx == c_black) {
    tft.fillWindow();
  } else if (coloridx == c_white) {
    tft.fillWindow(loresPixelColors[c_white]);
  } else {
    uint16_t color16 = loresPixelColors[c_black];
    if (coloridx < 16)
      color16 = loresPixelColors[coloridx];
    tft.fillWindow(color16);
  }
}

inline uint16_t blendColors(uint16_t a, uint16_t b)
{
  // Straight linear average doesn't work well for inverted text, because the
  // whites overwhelm the blacks.
  //return ((uint32_t)a + (uint32_t)b)/2;

#if 0
  // Testing a logarithmic color scale. My theory was that, since our
  // colors here are mostly black or white, it would be reasonable to
  // use a log scale of the average to bump up the brightness a
  // little. In practice, it's not really legible.
  return RGBto565(  (uint8_t)(logfn((_565toR(a) + _565toR(b))/2)),
		    (uint8_t)(logfn((_565toG(a) + _565toG(b))/2)),
		    (uint8_t)(logfn((_565toB(a) + _565toB(b))/2))  );
#endif
  
  // Doing an R/G/B average works okay for legibility. It's not great for
  // inverted text.
  return RGBto565(  (_565toR(a) + _565toR(b))/2,
		    (_565toG(a) + _565toG(b))/2,
		    (_565toB(a) + _565toB(b))/2  );

}

void TeensyDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  for (int yoff=0; yoff<2; yoff++) {
    dmaBuffer[(y*2)+SCREENINSET_Y+yoff][x+SCREENINSET_X] = color;
  }
}


void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint16_t color16)
{
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      dmaBuffer[(y*2)+SCREENINSET_Y+yoff][(x*2)+SCREENINSET_X+xoff] = _565To332(color16);
    }
  }
}

// "DoubleWide" means "please double the X because I'm in low-res
// width mode".
void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  uint16_t color16;
  color16 = loresPixelColors[(( color & 0x0F )     )];

  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      dmaBuffer[(y*2)+SCREENINSET_Y+yoff][(x*2)+SCREENINSET_X+xoff] = _565To332(color16);
    }
  }
}

// This exists for 4bpp optimization. We could totally call
// cacheDoubleWidePixel twice, but the (x&1) pfutzing is messy if
// we're just storing both halves anyway...
void TeensyDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, 
					   uint8_t colorA, uint8_t colorB)
{
  for (int yoff=0; yoff<2; yoff++) {
    for (int xoff=0; xoff<2; xoff++) {
      dmaBuffer[(y*2)+SCREENINSET_Y+yoff][(x*2)+SCREENINSET_X+xoff] = _565To332(colorA);
      dmaBuffer[(y*2)+SCREENINSET_Y+yoff][(x+1)*2+SCREENINSET_X+xoff] = _565To332(colorB);
    }
  }
}

inline double logfn(double x)
{
  // At a value of x=255, log(base 1.022)(x) is 254.636.
  return log(x)/log(1.022);
}

uint32_t TeensyDisplay::frameCount()
{
  return 0;
}
