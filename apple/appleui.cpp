#include "appleui.h"

#include "vm.h" // for DISPLAYHEIGHT. Probably not the most sensible.
#include "images.h"
#include "globals.h"

// FIXME: abstract and standardize the sizes, onscreen locations, and
// underlying bitmap types

AppleUI::AppleUI()
{
  redrawFrame = false;
  redrawDriveLatches = false;
  redrawDriveActivity = false;
  driveInserted[0] = driveInserted[1] = 0;
  driveActivity[0] = driveActivity[1] = 0;
}

AppleUI::~AppleUI()
{
}

void AppleUI::drawStaticUIElement(uint8_t element)
{
  // Only one static UI element right now...
  if (element != UIeOverlay)
    return;
  redrawFrame = true;
}

void AppleUI::drawOnOffUIElement(uint8_t element, bool state)
{
  if (element == UIeDisk1_state ||
      element == UIeDisk2_state) {
    driveInserted[element-UIeDisk1_state] = state;
    redrawDriveLatches = true;
  }
  else if (element == UIeDisk1_activity ||
	   element == UIeDisk2_activity) {
    driveActivity[element-UIeDisk1_activity] = state;
    redrawDriveActivity = true;
  }
}

void AppleUI::drawPercentageUIElement(uint8_t element, uint8_t percent)
{
  // only 1 element of this type
  if (element != UIePowerPercentage) {
    return;
  }
  // Temporarily disabled; the API for this needs updating for resolution-independent display coordinates
  //  drawBatteryStatus(percent);
}

void AppleUI::drawBatteryStatus(uint8_t percent)
{
  return; // *** FIXME: image and positioning not updated for new aspect ratio
  
  uint16_t xoff = 301;
  uint16_t yoff = 222;

  // the area around the apple is 12 wide; it's exactly 11 high the
  // color is 210/202/159

  float watermark = ((float)percent / 100.0) * 11;

  for (int y=0; y<11; y++) {
    uint8_t bgr = 210;
    uint8_t bgg = 202;
    uint8_t bgb = 159;

    if (11-y > watermark) {
      // black...
      bgr = bgg = bgb = 0;
    }

    for (int x=0; x<10; x++) {
#ifdef TEENSYDUINO
      uint8_t r = pgm_read_byte(&appleBitmap[(y * 10 + x)*4 + 0]);
      uint8_t g = pgm_read_byte(&appleBitmap[(y * 10 + x)*4 + 1]);
      uint8_t b = pgm_read_byte(&appleBitmap[(y * 10 + x)*4 + 2]);
      uint8_t a = pgm_read_byte(&appleBitmap[(y * 10 + x)*4 + 3]);
#else
      const uint8_t *p = &appleBitmap[(y * 10 + (x-1))*4];
      uint8_t r, g, b, a;
      r = p[0];
      g = p[1];
      b = p[2];
      a = p[3];
#endif

      // It's RGBA; blend w/ background color
      float alpha = (float)a / 255.0;
      r = (float)r * alpha + (bgr * (1.0 - alpha));
      g = (float)g * alpha + (bgg * (1.0 - alpha));
      b = (float)b * alpha + (bgb * (1.0 - alpha));

      uint16_t color16 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
      g_display->drawUIPixel(x+xoff, y+yoff, color16);
    }
  }
}

void AppleUI::blit()
{
  if (redrawFrame) {
    redrawFrame = false;
    g_display->drawImageOfSizeAt(displayBitmap, DBITMAP_WIDTH, DBITMAP_HEIGHT, 0, 0);
  }

  if (redrawDriveLatches) {
    redrawDriveLatches = false;
    const uint8_t *img;

    img = driveInserted[0] ? drive1LatchOpen : drive1LatchClosed;
    g_display->drawImageOfSizeAt(img, LATCH_WIDTH, LATCH_HEIGHT, LATCH_X, LATCH1_Y);

    img = driveInserted[1] ? drive2LatchOpen : drive2LatchClosed;
    g_display->drawImageOfSizeAt(img, LATCH_WIDTH, LATCH_HEIGHT, LATCH_X, LATCH2_Y);
  }

  if (redrawDriveActivity) {
    redrawDriveActivity = false;

    for (int y=0; y<LED_HEIGHT; y++) {
      for (int x=0; x<LED_WIDTH; x++) {
        g_display->drawUIPixel(x + LED_X, y + LED1_Y, driveActivity[0] ? 0xFA00 : 0x0000);
        
        g_display->drawUIPixel(x + LED_X, y + LED2_Y, driveActivity[1] ? 0xFA00 : 0x0000);
      }
    }
  }

}



