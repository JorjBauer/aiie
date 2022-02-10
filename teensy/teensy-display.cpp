#include <ctype.h> // isgraph

#include "teensy-display.h"
#include "iocompat.h"

#include "appleui.h"

#include "globals.h"
#include "applevm.h"

#include "RA8875_t4.h"
#include "ILI9341_wrap.h"

#include "images.h"

uint8_t *dmaBuffer = NULL;
uint16_t *dmaBuffer16 = NULL;

#include <SPI.h>

#define PIN_RST 8
#define PIN_DC 9
#define PIN_CS 0
#define PIN_MOSI 26
#define PIN_MISO 1
#define PIN_SCK 27

TeensyDisplay::TeensyDisplay()
{
  driveIndicator[0] = driveIndicator[1] = true; // assume on so they will redraw immediately the first time

  shellImage = NULL;
  d1OpenImage = d1ClosedImage = d2OpenImage = d2ClosedImage = NULL;
  appleImage = NULL;
  
  // FIXME abstract pin number, don't hard code it
  pinMode(11, INPUT_PULLUP);
  delay(10); // let it rise before reading it
  
  if (digitalRead(11)) {
    // Default: use older, smaller but faster, ILI display if pin 11 is not connected to ground
    Serial.println("    using ILI9341 display");
    use8875 = false;
    
    dmaBuffer16 = (uint16_t *)malloc((320*240)*2+32); // malloc() happens in the DMAMEM area (RAM2)
    // And we have to be sure dmaBuffer16 is 32-byte aligned for DMA purposes
    // so we intentionally alloc'd an extra 32 bytes in order to shift here
    dmaBuffer16 = (uint16_t *)(((uintptr_t)dmaBuffer16 + 32) &
                               ~((uintptr_t)(31)));
  
    tft = new ILI9341_Wrap(PIN_CS, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO, PIN_DC);

    // Load the 9341 images
    getImageInfoAndData(IMG_9341_SHELL, &shellWidth, &shellHeight, &shellImage);
    getImageInfoAndData(IMG_9341_D1OPEN, &driveWidth, &driveHeight, &d1OpenImage);
    getImageInfoAndData(IMG_9341_D1CLOSED, &driveWidth, &driveHeight, &d1ClosedImage);
    getImageInfoAndData(IMG_9341_D2OPEN, &driveWidth, &driveHeight, &d2OpenImage);
    getImageInfoAndData(IMG_9341_D2CLOSED, &driveWidth, &driveHeight, &d2ClosedImage);
    getImageInfoAndData(IMG_9341_APPLEBATTERY, &appleImageWidth, &appleImageHeight, &appleImage);
    
    tft->begin(50000000u);
    tft->setFrameBuffer((uint8_t *)dmaBuffer16);
  } else {
    // If someone grounded pin 11, then use the new RA8875 display
    Serial.println("    using RA8875 display");
    use8875 = true;

    dmaBuffer = (uint8_t *)malloc(800*480+32); // malloc() happens in the DMAMEM area (RAM2)
    // And we have to be sure dmaBuffer is 32-byte aligned for DMA purposes
    // so we intentionally alloc'd an extra 32 bytes in order to shift here
    dmaBuffer = (uint8_t *)(((uintptr_t)dmaBuffer + 32) &
                            ~((uintptr_t)(31)));
    
    tft = new RA8875_t4(PIN_CS, PIN_RST, PIN_MOSI, PIN_SCK, PIN_MISO);

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
    tft->setFrameBuffer((uint8_t *)dmaBuffer);
  }

  tft->fillWindow();
}

TeensyDisplay::~TeensyDisplay()
{
  /* FIXME: we mucked with these after alloc to align them, so we can't free them from their offset addresses; need to keep track of the original malloc'd address instead
  if (dmaBuffer)
    free(dmaBuffer);
  if (dmaBuffer16)
    free(dmaBuffer16);
  */
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
    drawImageOfSizeAt(d1OpenImage, driveWidth, driveHeight,
                      use8875 ? 4 : 55,
                      use8875 ? 67 : 216);
    break;
  case IMG_D1CLOSED:
    drawImageOfSizeAt(d1ClosedImage, driveWidth, driveHeight,
                      use8875 ? 4 : 55,
                      use8875 ? 67 : 216);
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

void TeensyDisplay::drawDriveActivity(bool drive0, bool drive1)
{
  // FIXME this could be much more efficient; it's doing a lot of checking use8875 in the middle of a loop
  
  if (drive0 != driveIndicator[0]) {
    for (int y=0; y<(use8875 ? LED_HEIGHT_8875 : LED_HEIGHT_9341); y++) {
      for (int x=0; x<(use8875 ? LED_WIDTH_8875 : LED_WIDTH_9341); x++) {
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
      tft->drawPixel(x+wherex, y+wherey, v);
    }
  }
}

void TeensyDisplay::blit()
{
  if (!tft)
    return;
  
  // Start updates, if they're not running already
  if (!tft->asyncUpdateActive())
    tft->updateScreenAsync(true);

  static uint32_t ctr = 0;
  
  // draw overlay, if any, occasionally
  {
    static uint32_t nextMessageTime = 0;
    if (millis() >= nextMessageTime) {
      if (overlayMessage[0]) {
        if (use8875) {
          drawString(M_SELECTDISABLED, 1, RA8875_HEIGHT-18, overlayMessage);
        } else {
          drawString(M_SELECTDISABLED, 1, ILI9341_HEIGHT - (16+12), overlayMessage);
        }
      }
      nextMessageTime = millis() + 1000;
    }
  }
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  tft->drawPixel(x,y,color);
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t color16 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

  tft->drawPixel(x,y,color16);
}

void TeensyDisplay::clrScr(uint8_t coloridx)
{
  tft->fillWindow(coloridx);
}

void TeensyDisplay::cachePixel(uint16_t x, uint16_t y, uint8_t coloridx)
{
  tft->cacheApplePixel(x,y,coloridx);
}

// "DoubleWide" means "please double the X because I'm in low-res
// width mode".
void TeensyDisplay::cacheDoubleWidePixel(uint16_t x, uint16_t y, uint8_t coloridx)
{
  tft->cacheDoubleWideApplePixel(x, y, coloridx);
}

uint32_t TeensyDisplay::frameCount()
{
  if (!tft)
    return 0;
  
  return tft->frameCount();
}
