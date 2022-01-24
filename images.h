#ifndef __IMAGES_H
#define __IMAGES_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define DBITMAP_HEIGHT 480
#define DBITMAP_WIDTH 800
#define LATCH_HEIGHT 11
#define LATCH_WIDTH  62
#define LATCH_X 4
#define LATCH1_Y 67
#define LATCH2_Y 116

#define SCREENINSET_X (121)
#define SCREENINSET_Y (47)

// Spacing and positioning of elements within the DBITMAP, used by AppleUI
#define LED_HEIGHT 9
#define LED_WIDTH 17
#define LED_X 48
#define LED1_Y 68
#define LED2_Y 117

enum {
  IMG_SHELL = 0, // previously displayBitmap
  IMG_D1OPEN = 1,
  IMG_D1CLOSED = 2,
  IMG_D2OPEN = 3,
  IMG_D2CLOSED = 4,
  IMG_APPLEBATTERY = 5
};

const bool getImageInfoAndData(uint8_t imgnum, uint16_t *width, uint16_t *height, const uint8_t **dataptr);

#endif
