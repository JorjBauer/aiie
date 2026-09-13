#include "appleui.h"

#include "vm.h"
#include "images.h" // for the image abstraction constants
#include "globals.h"

AppleUI::AppleUI()
{
  redrawFrame = true;
  redrawDriveLatches = true;
  redrawDriveActivity = true;
  driveEmpty[0] = driveEmpty[1] = true;
  driveActivity[0] = driveActivity[1] = 0;
  hdEmpty[0] = hdEmpty[1] = true;
  hdActivity[0] = hdActivity[1] = false;
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
    driveEmpty[element-UIeDisk1_state] = state;
    redrawDriveLatches = true;
  }
  else if (element == UIeDisk1_activity ||
	   element == UIeDisk2_activity) {
    driveActivity[element-UIeDisk1_activity] = state;
    redrawDriveActivity = true;
  }
  else if (element == UIeHD_state || element == UIeHD2_state) {
    hdEmpty[element == UIeHD2_state] = state;
    redrawDriveLatches = true;
  }
  else if (element == UIeHD_activity || element == UIeHD2_activity) {
    hdActivity[element == UIeHD2_activity] = state;
    redrawDriveActivity = true;
  }
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
  return; // *** FIXME: image and positioning not updated for new aspect ratio
  
  uint16_t xoff = 301;
  uint16_t yoff = 222;

  // the area around the apple is 12 wide; it's exactly 11 high the
  // color is 210/202/159

  static uint8_t *img = NULL;
  static uint16_t h,w;
  if (!img) {
    if (!getImageInfoAndData(IMG_APPLEBATTERY, &w, &h, &img)) {
      return;
    }
  }
  
  float watermark = ((float)percent / 100.0) * h;

  for (int y=0; y<h; y++) {
    uint8_t bgr = 210;
    uint8_t bgg = 202;
    uint8_t bgb = 159;

    if ((h-y) > watermark) {
      // black...
      bgr = bgg = bgb = 0;
    }

    uint16_t w = w;
    for (int x=0; x<w; x++) {
#ifdef TEENSYDUINO
      uint8_t r = pgm_read_byte(&img[(y * w + x)*4 + 0]);
      uint8_t g = pgm_read_byte(&img[(y * w + x)*4 + 1]);
      uint8_t b = pgm_read_byte(&img[(y * w + x)*4 + 2]);
      uint8_t a = pgm_read_byte(&img[(y * w + x)*4 + 3]);
#else
      const uint8_t *p = &img[(y * w + (x-1))*4];
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
      g_display->drawPixel(x+xoff, y+yoff, color16);
    }
  }
}

void AppleUI::blit()
{
  if (redrawFrame) {
    redrawFrame = false;
    g_display->drawUIImage(IMG_SHELL);
  }

  if (redrawDriveLatches) {
    redrawDriveLatches = false;
    // A latch is drawn only when there is a card behind it. The "closed"
    // images are the pale, empty drive; the "open" ones the dark, loaded
    // drive.
    bool diskCard = (g_slotDiskII != 0);
    bool hdCard = (g_slotHD32 != 0);
    g_display->drawUIImage(!diskCard ? IMG_D1BLANK : driveEmpty[0] ? IMG_D1CLOSED : IMG_D1OPEN);
    g_display->drawUIImage(!diskCard ? IMG_D2BLANK : driveEmpty[1] ? IMG_D2CLOSED : IMG_D2OPEN);
    // One mounted image shows as a single "HD"; a second one turns the pair
    // into "HD1" and "HD2".
    bool twoHDs = hdCard && !hdEmpty[1];
    g_display->drawUIImage(!hdCard ? IMG_HDBLANK :
			   twoHDs ? (hdEmpty[0] ? IMG_HD1EMPTY : IMG_HD1LOADED) :
			   (hdEmpty[0] ? IMG_HDEMPTY : IMG_HDLOADED));
    g_display->drawUIImage(!twoHDs ? IMG_HD2BLANK : IMG_HD2LOADED);
    redrawDriveActivity = true; // these overlap
  }

  if (redrawDriveActivity) {
    redrawDriveActivity = false;
    g_display->drawDriveActivity(g_slotDiskII ? (driveActivity[0] ? 1 : 0) : -1,
				 g_slotDiskII ? (driveActivity[1] ? 1 : 0) : -1,
				 g_slotHD32 ? (hdActivity[0] ? 1 : 0) : -1,
				 (g_slotHD32 && !hdEmpty[1]) ? (hdActivity[1] ? 1 : 0) : -1);
  }

}



