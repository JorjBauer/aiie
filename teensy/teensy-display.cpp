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

#include "RA8875_t4.h"
#include "ILI9341_wrap.h"

#include "images.h"

// We have two handles to the DMA buffer - the actual data
// (dmaBuffer), which is the larger of the two sizes at 800*480 bytes
// (=38400); or dmaBuffer16, which is the same pointer for (320*240 =
// 76800) 16-bit words (using 153600 bytes of RAM). Since the 800*480
// is larger we'll construct a buffer using that value and then use it
// either as dmaBuffer or dmaBuffer16.
DMAMEM uint8_t dmaBuffer[RA8875_HEIGHT * RA8875_WIDTH] __attribute__((aligned(32)));
uint16_t *dmaBuffer16 = NULL; // At runtime, this points to dmaBuffer.

#include <SPI.h>

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

#define blendColors(a,b) RGBto565( (_565toR(a) + _565toR(b))/2, (_565toG(a) + _565toG(b))/2, (_565toB(a) + _565toB(b))/2  )


BaseDisplay *tft = NULL;

TeensyDisplay::TeensyDisplay()
{
  dmaBuffer16 = (uint16_t *)dmaBuffer;
  
  driveIndicator[0] = driveIndicator[1] = false;
  driveIndicatorDirty = true;

  shellImage = NULL;
  d1OpenImage = d1ClosedImage = d2OpenImage = d2ClosedImage = NULL;
  appleImage = NULL;
  
  // FIXME abstract pin number, don't hard code it
  pinMode(11, INPUT);
  digitalWrite(11, HIGH); // turn on pull-up

  if (digitalRead(11)) {
    // Default: use older, small ILI display if pin 11 is not connected to ground
    Serial.println("    using ILI9341 display");
    tft = new ILI9341_Wrap(PIN_CS, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO, PIN_DC);
    use8875 = false;

    // Load the 9341 images
    getImageInfoAndData(IMG_9341_SHELL, &shellWidth, &shellHeight, &shellImage);
    getImageInfoAndData(IMG_9341_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
    getImageInfoAndData(IMG_9341_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
    getImageInfoAndData(IMG_9341_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
    getImageInfoAndData(IMG_9341_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
    getImageInfoAndData(IMG_9341_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
    
    tft->begin(50000000u);
  } else {
    // If someone grounded pin 11, then use the new RA8875 display
    Serial.println("    using RA8875 display");
    tft = new RA8875_t4(PIN_CS, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);
    use8875 = true;

    // Load the 8875 images
    getImageInfoAndData(IMG_8875_SHELL, &shellWidth, &shellHeight, &shellImage);
    getImageInfoAndData(IMG_8875_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
    getImageInfoAndData(IMG_8875_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
    getImageInfoAndData(IMG_8875_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
    getImageInfoAndData(IMG_8875_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
    getImageInfoAndData(IMG_8875_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
    // 30MHz: solid performance, 9 FPS
    // 57.5MHz: solid performance, 14/15 FPS
    // 60MHz: unexpected palatte shifts & (may be audio overruns, needs checking since bumping up buffer sizes) 17 FPS
    // And I can't get the SPI bus working at 80MHz or higher. Not sure why yet...
    tft->begin(57500000);
  }

  tft->setFrameBuffer((uint8_t *)dmaBuffer);
  tft->fillWindow();
}

TeensyDisplay::~TeensyDisplay()
{
}

void TeensyDisplay::flush()
{
}

void TeensyDisplay::redraw()
{
  if (g_ui) {
    g_ui->drawStaticUIElement(UIeOverlay);

    if (g_vm) {
      g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
      g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
    }
  }
}

// Take one of the abstracted image constants, figure out which one it
// is based on the current display, and display it via
// drawImageOfSizeAt.

void TeensyDisplay::drawUIImage(uint8_t imageIdx)
{
  switch (imageIdx) {
  case IMG_SHELL:
    drawImageOfSizeAt(shellImage, shellWidth, shellHeight, 0, 0);
    break;
  case IMG_D1OPEN:
    drawImageOfSizeAt(d1OpenImage, driveWidth, driveHeight, 55, 216);
    break;
  case IMG_D1CLOSED:
    drawImageOfSizeAt(d1ClosedImage, driveWidth, driveHeight, 55, 216);
    break;
  case IMG_D2OPEN:
    drawImageOfSizeAt(d2OpenImage, driveWidth, driveHeight, 189, 216);
    break;
  case IMG_D2CLOSED:
    drawImageOfSizeAt(d2ClosedImage, driveWidth, driveHeight, 189, 216);
    break;
  case IMG_APPLEBATTERY:
    // FIXME ***
    break;
  }
}

// *** this probably needs to be private now FIXME
void TeensyDisplay::drawImageOfSizeAt(const uint8_t *img, 
				      uint16_t sizex, uint16_t sizey, 
				      uint16_t wherex, uint16_t wherey)
{
  uint8_t r, g, b;

  uint8_t *p = img;
  for (uint16_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      uint16_t v = pgm_read_byte(p++);
      v <<= 8;
      v |= pgm_read_byte(p++);
      if (use8875) {
        dmaBuffer[(y+wherey)*RA8875_WIDTH + x+wherex] = _565To332(v);
      } else {
        dmaBuffer16[(y+wherey)*ILI9341_WIDTH + x+wherex] = v;
      }
    }
  }
}

void TeensyDisplay::blit()
{
  // Start updates, if they're not running already
  if (!tft->asyncUpdateActive())
    tft->updateScreenAsync(true);

  static uint32_t ctr = 0;
  
  // draw overlay, if any, occasionally
  {
    static uint32_t nextMessageTime = 0;
    if (millis() >= nextMessageTime) {
      if (overlayMessage[0]) {
	drawString(M_SELECTDISABLED, 1, (RA8875_HEIGHT - 18)/2, overlayMessage); // FIXME this /2 is clunky b/c drawString winds up doubling
      }
      nextMessageTime = millis() + 1000;
    }
  }
}

void TeensyDisplay::blit(AiieRect r)
{
}

// FIXME do we still need 2 different methods for drawing pixels
void TeensyDisplay::drawUIPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (use8875) {
    if (x < RA8875_WIDTH && y < RA8875_HEIGHT)
      dmaBuffer[y*RA8875_WIDTH+x] = _565To332(color);
  } else {
    if (x < ILI9341_WIDTH && y < ILI9341_HEIGHT)
      dmaBuffer16[y*ILI9341_WIDTH+x] = color;
  }
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (use8875) {
    if (x < RA8875_WIDTH && y < RA8875_HEIGHT)
      dmaBuffer[y*RA8875_WIDTH+x] = _565To332(color);
  } else {
    if (x < ILI9341_WIDTH && y < ILI9341_HEIGHT)
      dmaBuffer16[y*ILI9341_WIDTH+x] = color;
  }
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

  const unsigned char *ch = asciiToAppleGlyph(c);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (use8875) {
        for (int8_t ys=0; ys<2; ys++) {
          for (int8_t xs=0; xs<2; xs++) {
            if (*ch & (1 << (x_off))) {
              drawUIPixel((x + x_off)*2+xs, (y + y_off)*2+ys, onPixel);
            } else {
              drawUIPixel((x + x_off)*2+xs, (y + y_off)*2+ys, offPixel);
            }
          }
        }
      } else {
        if (*ch & (1 << (x_off))) {
          drawUIPixel((x + x_off), (y + y_off), onPixel);
        } else {
          drawUIPixel((x + x_off), (y + y_off), offPixel);
        }
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
    x += xsize;
    if (x >= (RA8875_WIDTH-xsize)/2) break; // FIXME this is a pre-scaled number, b/c drawCharacter is scaling. Klutzy.
  }
}

void TeensyDisplay::clrScr(uint8_t coloridx)
{
  uint16_t color16 = loresPixelColors[c_black];
  if (use8875) {
    uint8_t c = _565To332(color16);
    for (uint16_t y=0; y<RA8875_HEIGHT; y++) {
      for (uint16_t x=0; x<RA8875_WIDTH; x++) {
        // This could be faster - make one line, then memcpy the line to
        // the other lines? Or just use memset since it's 8bpp
        dmaBuffer[y*RA8875_WIDTH+x] = c;
      }
    }
  } else {
    if (coloridx == c_black) {
      memset(dmaBuffer16, 0x00, ILI9341_HEIGHT * ILI9341_WIDTH * 2);
    } else if (coloridx == c_white) {
      memset(dmaBuffer16, 0xFF, ILI9341_HEIGHT * ILI9341_WIDTH * 2);
    } else {
      if (coloridx < 16)
        color16 = loresPixelColors[coloridx];
      // This could be faster - make one line, then memcpy the line to
      // the other lines?
      for (uint16_t y=0; y<ILI9341_HEIGHT; y++) {
        for (uint16_t x=0; x<ILI9341_WIDTH; x++) {
          dmaBuffer16[y*ILI9341_WIDTH+x] = color16;
        }
      }
    }
  }
}

void TeensyDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t color)
{
  if (use8875) {
    // The 8875 display doubles vertically
    for (int yoff=0; yoff<2; yoff++) {
      dmaBuffer[((y*2)+SCREENINSET_8875_Y+yoff) * RA8875_WIDTH +x+SCREENINSET_8875_X] = _565To332(loresPixelColors[color]);
    }
  } else {
    // The 9341 is half the width we need, so this jumps through hoops
    // to reduce the resolution in a way that's reasonable by blending
    // pixels
    if (x&1) {
      uint16_t origColor = dmaBuffer16[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH+(x>>1)+SCREENINSET_9341_X];
      uint16_t newColor = (uint16_t) loresPixelColors[color];
      if (g_displayType == m_blackAndWhite) {
        // There are four reasonable decisions here: if either pixel              
        // *was* on, then it's on; if both pixels *were* on, then it's            
        // on; and if the blended value of the two pixels were on, then           
        // it's on; or if the blended value of the two is above some              
        // certain overall brightness, then it's on. This is the last of          
        // those - where the brightness cutoff is defined in the bios as          
        // g_luminanceCutoff.                                                     
        uint16_t blendedColor = blendColors(origColor, newColor);
        uint16_t luminance = luminanceFromRGB(_565toR(blendedColor),
                                              _565toG(blendedColor),
                                              _565toB(blendedColor));
        cacheDoubleWidePixel(x>>1,y,(uint16_t)((luminance >= g_luminanceCutoff) ? 0xFFFF : 0x0000));
      } else {
        cacheDoubleWidePixel(x>>1,y,color);
        // Else if it's black, we leave whatever was in the other pixel.          
      }
    } else {
      // All of the even pixels get drawn...
      cacheDoubleWidePixel(x>>1,y,color);
    }
  }
}

void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint16_t color16)
{
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      for (int xoff=0; xoff<2; xoff++) {
        dmaBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH+(x*2)+SCREENINSET_8875_X+xoff] = _565To332(color16);
      }
    }
  } else {
    dmaBuffer16[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH + (x) + SCREENINSET_9341_X] = color16;
  }
}

// "DoubleWide" means "please double the X because I'm in low-res
// width mode".
void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t color)
{
  uint16_t color16;
  color16 = loresPixelColors[(( color & 0x0F )     )];
  
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      for (int xoff=0; xoff<2; xoff++) {
        dmaBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH+(x*2)+SCREENINSET_8875_X+xoff] = _565To332(color16);
      }
    }
  } else {
    dmaBuffer16[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH + (x) + SCREENINSET_9341_X] = color16;
  }
}

// This exists for 4bpp optimization. We could totally call
// cacheDoubleWidePixel twice, but the (x&1) pfutzing is messy if
// we're just storing both halves anyway...
void TeensyDisplay::cache2DoubleWidePixels(uint16_t x, uint16_t y, 
					   uint8_t colorA, uint8_t colorB)
{
  if (use8875) {
    for (int yoff=0; yoff<2; yoff++) {
      for (int xoff=0; xoff<2; xoff++) {
        dmaBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH+(x*2)+SCREENINSET_8875_X+xoff] = _565To332(colorA);
        dmaBuffer[((y*2)+SCREENINSET_8875_Y+yoff)*RA8875_WIDTH+(x+1)*2+SCREENINSET_8875_X+xoff] = _565To332(colorB);
      }
    }
  } else {
    dmaBuffer16[(y+SCREENINSET_8875_Y)*ILI9341_WIDTH + (x)+SCREENINSET_9341_X] = colorA;
    dmaBuffer16[(y+SCREENINSET_8875_Y)*ILI9341_WIDTH + ((x+1))+SCREENINSET_9341_X] = colorB;
  }
}

uint32_t TeensyDisplay::frameCount()
{
  return tft->frameCount();
}
