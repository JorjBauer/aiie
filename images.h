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

// Spacing and positioning of elements within the DBITMAP, used by AppleUI
#define LED_HEIGHT 9
#define LED_WIDTH 17
#define LED_X 48
#define LED1_Y 68
#define LED2_Y 117

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
