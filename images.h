#ifndef __IMAGES_H
#define __IMAGES_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define SCREENINSET_8875_X (121)
#define SCREENINSET_8875_Y (47)
#define SCREENINSET_9341_X (18)
#define SCREENINSET_9341_Y (13)

// drive activity LED positions
#define LED_HEIGHT_8875 9
#define LED_WIDTH_8875 17
#define LED1_X_8875 48
#define LED2_X_8875 48
#define LED1_Y_8875 68
#define LED2_Y_8875 117

#define LED_HEIGHT_9341 1
#define LED_WIDTH_9341 6
#define LED1_X_9341 125
#define LED2_X_9341 (125+135)
#define LED1_Y_9341 213
#define LED2_Y_9341 213

// WiFi signal indicator: the round "dot + fanning arcs" symbol. X,Y is the DOT at
// the base of the symbol (center-bottom); the three arcs fan up-and-out from it,
// out to radius 3*RSTEP. Drawn on a black background, only when a Uthernet card is
// installed. X,Y are set so the black box sits flush in the top-right corner (box
// right edge = screen right, box top = screen top); nudge here to taste.
#define WIFI_X_9341     311  // 320-wide screen: 319 - boxHalfWidth(2*RSTEP+2=8)
#define WIFI_Y_9341     10   // RSTEP*3 + 1, so the box top lands on y=0
#define WIFI_RSTEP_9341 3    // radius step between arcs (arcs at 3,6,9)
#define WIFI_DOTR_9341  1    // dot radius

#define WIFI_X_8875     785  // 800-wide screen: 799 - boxHalfWidth(2*RSTEP+2=14)
#define WIFI_Y_8875     19   // RSTEP*3 + 1
#define WIFI_RSTEP_8875 6    // arcs at 6,12,18
#define WIFI_DOTR_8875  2

// These are the ABSTRACTED constants that AppleUI uses to tell the
// display what it wants redrawn via drawUIImage(uint8_t imageIdx)
enum {
  IMG_SHELL = 0, // previously displayBitmap
  IMG_D1OPEN = 1,
  IMG_D1CLOSED = 2,
  IMG_D2OPEN = 3,
  IMG_D2CLOSED = 4,
  IMG_APPLEBATTERY = 5
};

// These are the DISPLAY-SPECIFIC constants that are used to retrieve
// a specific image from storage from within drawUIImage itself
enum {
  IMG_8875_SHELL = 0,
  IMG_8875_D1OPEN = 1,
  IMG_8875_D1CLOSED = 2,
  IMG_8875_D2OPEN = 3,
  IMG_8875_D2CLOSED = 4,
  IMG_8875_APPLEBATTERY = 5,
  IMG_9341_SHELL = 6,
  IMG_9341_D1OPEN = 7,
  IMG_9341_D1CLOSED = 8,
  IMG_9341_D2OPEN = 7,   // repeat of d1; they're the same image
  IMG_9341_D2CLOSED = 8,
  IMG_9341_APPLEBATTERY = 9
};

bool getImageInfoAndData(uint8_t imgnum, uint16_t *width, uint16_t *height, uint8_t **dataptr);

#endif
