#include "appleui.h"

#include "vm.h" // for DISPLAYHEIGHT. Probably not the most sensible.
#include "images.h"
#include "globals.h"

// FIXME: abstract and standardize the sizes, onscreen locations, and
// underlying bitmap types

AppleUI::AppleUI()
{
}

AppleUI::~AppleUI()
{
}

void AppleUI::drawStaticUIElement(uint8_t element)
{
  // Only one static UI element right now...
  if (element != UIeOverlay)
    return;

  g_display->drawImageOfSizeAt(displayBitmap, DBITMAP_WIDTH, DBITMAP_HEIGHT, 0, 0);
}

void AppleUI::drawOnOffUIElement(uint8_t element, bool state)
{
  uint16_t xoff = 55;
  uint8_t yoff = 216;
  uint16_t xsize;
  uint8_t ysize;
  const uint8_t *img;

  switch (element) {
  case UIeDisk1_state:
    xoff = 55;
    yoff = 216;
    xsize = 43;
    ysize = 20;
    img = state ? driveLatchOpen : driveLatch;
    break;
  case UIeDisk2_state:
    xoff = 55+134;
    yoff = 216;
    xsize = 43;
    ysize = 20;
    img = state ? driveLatchOpen : driveLatch;
  break;
  case UIeDisk1_activity:
  case UIeDisk2_activity:
    {
      uint16_t xoff = 125;
      uint16_t yoff = 213;
      if (element == UIeDisk2_activity)
	xoff += 135;
      for (int x=0; x<6; x++) {
	// Can't draw this from inside the interrupt; might already be
	// drawing the screen from outside the interrupt. Temporary
	// hack - remove this completely; FIXME: update diskii.cpp to
	// queue it somehow, for drawing in a maintenance function, to
	// be called from the main thread and not the interrupt

	//	g_display->drawPixel(x + xoff, yoff, state ? 0xF800 : 0x8AA9);
	//	g_display->drawPixel(x + xoff, yoff + 1, state ? 0xF800 : 0x8AA9);
      }
    }

    return;
  default:
    return;
  }

  g_display->drawImageOfSizeAt(img, xsize, ysize, xoff, yoff);
}

void AppleUI::drawPercentageUIElement(uint8_t element, uint8_t percent)
{
  // only 1 element of this type
  if (element != UIePowerPercentage) {
    return;
  }
  drawBatteryStatus(percent);
}

void AppleUI::drawBatteryStatus(uint8_t percent)
{
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

      g_display->drawPixel(x+xoff, y+yoff, r, g, b);
    }
  }
}



