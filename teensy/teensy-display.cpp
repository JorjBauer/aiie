#include <ctype.h> // isgraph
#include "teensy-display.h"

#include "bios-font.h"
#include "appleui.h"

#define _clock 65000000

#define PIN_RST 8
#define PIN_DC 9
#define PIN_CS 10
#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_SCK 13

#define disp_x_size 239
#define disp_y_size 319

#include "globals.h"
#include "applevm.h"

ILI9341_t3 tft = ILI9341_t3(PIN_CS, PIN_DC, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);

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
  memset(videoBuffer, 0, sizeof(videoBuffer));

  tft.begin();
  tft.setRotation(3);
  tft.setClock(_clock);

  // Could set up an automatic DMA transfer here; cf.
  // https://forum.pjrc.com/threads/25778-Could-there-be-something-like-an-ISR-template-function/page4
  
  // LCD initialization complete

  clrScr();

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
  // FIXME: only fill the area that's got our "terminal"
  tft.fillScreen(ILI9341_BLACK);
}

void TeensyDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  tft.drawPixel(x,y,color);
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

void TeensyDisplay::blit(AiieRect r)
{
  // remember these are "starts at pixel number" values, where 0 is the first.
  #define HOFFSET 18
  #define VOFFSET 13
  
  uint8_t *vbufPtr;
  for (uint8_t y=r.top; y<=r.bottom; y++) {
    vbufPtr = &videoBuffer[y * TEENSY_DRUN + r.left];
    for (uint16_t x=r.left; x<=r.right; x++) {
      uint8_t colorIdx;
      if (!(x & 0x01)) {
	colorIdx = *vbufPtr >> 4;
      } else {
	// alpha the right-ish pixel over the left-ish pixel.
	colorIdx = *vbufPtr & 0x0F;
      }
      colorIdx <<= 1;

      uint16_t c;
      if (g_displayType == m_monochrome) {
	c = loresPixelColorsGreen[colorIdx];
      }
      else if (g_displayType == m_blackAndWhite) {
	c = loresPixelColorsWhite[colorIdx];
      } else {
	c = loresPixelColors[colorIdx];
      }

      drawPixel(x+HOFFSET,y+VOFFSET,c);

      if (x & 0x01) {
	// When we do the odd pixels, then move the pixel pointer to the next pixel
	vbufPtr++;
      }
    }
  }

  // draw overlay, if any
  if (overlayMessage[0]) {
    drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
  }
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
	drawPixel(x+x_off, y+y_off, onPixel);
      } else {
	drawPixel(x+x_off, y+y_off, offPixel);
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
      drawPixel(wherex+x, wherey+y, (((r&248)|g>>5) << 8) | ((g&28)<<3|b>>3));
    }
  }
}

// "DoubleWide" means "please double the X because I'm in low-res
// width mode". But we only have half the horizontal width required on
// the Teensy, so it's divided in half. And then we drop to 4-bit
// colors, so it's divided in half again.
void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  uint8_t b = videoBuffer[y*TEENSY_DRUN+(x>>1)];

  if (x & 1) {
    // Low nybble
    b = (b & 0xF0) | (color & 0x0F);
  } else {
    // High nybble
    b = (color << 4) | (b & 0x0F);
  }
  videoBuffer[y*TEENSY_DRUN+(x>>1)] = b;
}

// This exists for 4bpp optimization. We could totally call
// cacheDoubleWidePixel twice, but the (x&1) pfutzing is messy if
// we're just storing both halves anyway...
void TeensyDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, 
					   uint8_t colorA, uint8_t colorB)
{
  videoBuffer[y*TEENSY_DRUN+(x>>1)] = (colorB << 4) | colorA;
}

// This is the full 560-pixel-wide version -- and we only have 280
// pixels wide. So we'll divide x by 2. And then at 4bpp, we divide by
// 2 again.
// On odd-numbered X pixels, we also alpha-blend -- "black" means "clear"
void TeensyDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  if (x&1) {
    x >>= 1; // divide by 2, then this is mostly cacheDoubleWidePixel. Except...
    uint8_t b = videoBuffer[y*TEENSY_DRUN+(x>>1)];

    if (x & 1) {
      // Low nybble
      if (color == c_black)
	color = b & 0x0F;
      b = (b & 0xF0) | (color & 0x0F);
    } else {
      // High nybble
      if (color == c_black)
	color = (b & 0xF0) >> 4;
      b = (color << 4) | (b & 0x0F);
    }
    videoBuffer[y*TEENSY_DRUN+(x>>1)] = b;
  } else {
    cacheDoubleWidePixel(x/2, y, color);
  }
}
