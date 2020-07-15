#include <ctype.h> // isgraph
#include <DMAChannel.h>

#include "teensy-display.h"

#include "bios-font.h"
#include "appleui.h"
#include <SPI.h>

#define _clock 75000000


#define PIN_RST 8
#define PIN_DC 9
#define PIN_CS 10
#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_SCK 13

// Inside the 320x240 display, the Apple display is 280x192.
// (That's half the "correct" width, b/c of double-hi-res.)
#define apple_display_w 280
#define apple_display_h 192

// Inset inside the apple2 "frame" where we draw the display
// remember these are "starts at pixel number" values, where 0 is the first.
#define HOFFSET 18
#define VOFFSET 13

#include "globals.h"
#include "applevm.h"

DMAMEM uint16_t dmaBuffer[240][320]; // 240 rows, 320 columns

#define RGBto565(r,g,b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define _565toR(c) ( ((c) & 0xF800) >> 8 )
#define _565toG(c) ( ((c) & 0x07E0) >> 5 )
#define _565toB(c) ( ((c) & 0x001F) )

//ILI9341_t3 tft = ILI9341_t3(PIN_CS, PIN_DC, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);
ILI9341_t3n tft = ILI9341_t3n(PIN_CS, PIN_DC, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);

DMAChannel dmatx;
DMASetting dmaSetting;

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

const uint16_t loresPixelColorsGreen[16] = { 0x0000, 
					      0x0140, 
					      0x0040, 
					      0x0280, 
					      0x0300, 
					      0x0340, 
					      0x0300, 
					      0x0480, 
					      0x02C0, 
					      0x0240, 
					      0x0500, 
					      0x0540, 
					      0x0580, 
					      0x0700, 
					      0x0680, 
					      0x07C0 
};

const uint16_t loresPixelColorsWhite[16] = { 0x0000, 
					     0x2945, 
					     0x0841, 
					     0x528A, 
					     0x630C, 
					     0x6B4D, 
					     0x630C, 
					     0x9492, 
					     0x5ACB, 
					     0x4A49, 
					     0xA514, 
					     0xAD55, 
					     0xB596, 
					     0xE71C, 
					     0xD69A, 
					     0xFFDF
};

TeensyDisplay::TeensyDisplay()
{
  memset(dmaBuffer, 0x80, sizeof(dmaBuffer));

  tft.begin(_clock);
  tft.setRotation(3);
  tft.setFrameBuffer((uint16_t *)dmaBuffer);
  tft.useFrameBuffer(true);
  tft.fillScreen(ILI9341_BLACK);

  driveIndicator[0] = driveIndicator[1] = false;
  driveIndicatorDirty = true;
}

TeensyDisplay::~TeensyDisplay()
{
}

void TeensyDisplay::redraw()
{
  g_ui->drawStaticUIElement(UIeOverlay);

  if (g_vm) {
    g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }
}

void TeensyDisplay::clrScr()
{
  memset(dmaBuffer, 0x00, sizeof(dmaBuffer));
}

void TeensyDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  // These pixels are just cached in the buffer; they're not drawn directly.
  dmaBuffer[y][x] = color;
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  tft.drawPixel(x,y,color);
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t color16 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

  drawPixel(x,y,color16);
}

void TeensyDisplay::flush()
{
  blit({0,0,191,279});
}

void TeensyDisplay::blit()
{
  // Start DMA transfers if they aren't running
  if (!tft.asyncUpdateActive())
    tft.updateScreenAsync(true);
  
  // draw overlay, if any, occasionally
  {
    static uint32_t nextMessageTime = 0;
    if (millis() >= nextMessageTime) {
      if (overlayMessage[0]) {
	drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
      }
      nextMessageTime = millis() + 1000;
    }
  }
}

void TeensyDisplay::blit(AiieRect r)
{
  // Nothing to do here, since we're regularly blitting the whole screen via DMA
}

void TeensyDisplay::drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c)
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
    uint8_t ch = pgm_read_byte(&BiosFont[temp]);
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (ch & (1 << (7-x_off))) {
	dmaBuffer[y+y_off][x+x_off] = onPixel;
      } else {
	dmaBuffer[y+y_off][x+x_off] = offPixel;
      }
    }
    temp++;
  }
}

void TeensyDisplay::drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font

  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?
  }
}

void TeensyDisplay::drawImageOfSizeAt(const uint8_t *img, 
				      uint16_t sizex, uint8_t sizey, 
				      uint16_t wherex, uint8_t wherey)
{
  uint8_t r, g, b;

  for (uint8_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      r = pgm_read_byte(&img[(y*sizex + x)*3 + 0]);
      g = pgm_read_byte(&img[(y*sizex + x)*3 + 1]);
      b = pgm_read_byte(&img[(y*sizex + x)*3 + 2]);
      dmaBuffer[y+wherey][x+wherex] = RGBto565(r,g,b);
    }
  }
}

// "DoubleWide" means "please double the X because I'm in low-res
// width mode". But we only have half the horizontal width required on
// the Teensy, so it's divided in half.
void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  uint16_t color16;
  color16 = loresPixelColors[(( color & 0x0F )     )];
  dmaBuffer[y+VOFFSET][x+HOFFSET] = color16;
}

// This exists for 4bpp optimization. We could totally call
// cacheDoubleWidePixel twice, but the (x&1) pfutzing is messy if
// we're just storing both halves anyway...
void TeensyDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, 
					   uint8_t colorA, uint8_t colorB)
{
  // FIXME: Convert 4-bit colors to 16-bit colors?
  dmaBuffer[y+VOFFSET][x+  HOFFSET] = loresPixelColors[colorB&0xF];
  dmaBuffer[y+VOFFSET][x+1+HOFFSET] = loresPixelColors[colorA&0xF];
}

inline double logfn(double x)
{
  // At a value of x=255, log(base 1.022)(x) is 254.636.
  return log(x)/log(1.022);
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

// This is the full 560-pixel-wide version -- and we only have 280
// pixels in our buffer b/c the display is only 320 pixels wide
// itself. So we'll divide x by 2. On odd-numbered X pixels, we also
// blend the colors of the two virtual pixels that share an onscreen
// pixel
void TeensyDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
#if 0
  static uint8_t previousColor = 0;
#endif
  if (x&1) {
    // Blend the two pixels. This takes advantage of the fact that we
    // always call this linearly for 80-column text drawing -- we never
    // do partial screen blits, but always draw at least a whole character.
    // So we can look at the pixel in the "shared" cell of RAM, and come up
    // with a color between the two.

#if 1
    // This is straight blending, R/G/B average
    uint16_t origColor = dmaBuffer[y+VOFFSET][(x>>1)+HOFFSET];
    uint16_t newColor = loresPixelColors[color];
    cacheDoubleWidePixel(x>>1, y, blendColors(origColor, newColor));
#endif

#if 0
  // The model we use for the SDL display works better, strangely - it keeps
  // the lores pixel index color (black, magenda, dark blue, purple, dark
  // green, etc.) until render time; so when it does the blend here, it's
  // actually blending in a very nonlinear way - e.g. "black + white / 2"
  // is actually "black(0) + white(15) / 2 = 15/2 = 7 (light blue)". Weird,
  // but definitely legible in a mini laptop SDL window with the same scale.
  // Unfortunately, it doesn't translate well to a ILI9341 panel; the pixels
  // are kind of muddy and indistinct, so the blue spills over and makes it
  // very difficult to read.
    uint8_t origColor = previousColor;
    uint8_t newColor = (uint16_t)(origColor + color) / 2;
    cacheDoubleWidePixel(x>>1, y, (uint16_t)color + (uint16_t)previousColor/2);
#endif
  } else {
#if 0
    previousColor = color; // used for blending
#endif
    cacheDoubleWidePixel(x>>1, y, color);
  }
}

uint32_t TeensyDisplay::frameCount()
{
  return tft.frameCount();
}
