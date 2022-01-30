#include "images.h"

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
  #define PROGMEM
#endif

/* This is the wrapper for retrieving all the static images. It has
 * const static arrays for each image's data, and then a function that
 * knows all of the metadata for each one.
 *
 * To create the data array for a PNG or JPG image...
 *   use ImageMagick's "stream" utility to generate raw RGB
 *    $ stream -map rgb -storage-type char newimg.png newimg.raw
 *   then use util/genimage16.pl to generate the bytestream and copy/paste
 */

#include "image-8875-bg.h"
#include "image-8875-apple.h"
#include "image-8875-drivelatches.h"
#include "image-9341-bg.h"
#include "image-9341-drivelatches.h"
#include "image-9341-applebitmap.h"

bool getImageInfoAndData(uint8_t imgnum, uint16_t *width, uint16_t *height, uint8_t **dataptr)
{
  switch (imgnum) {
  case IMG_8875_SHELL:
    *width = 800;
    *height = 480;
    *dataptr = (uint8_t *)image_8875_bg;
    break;
  case IMG_8875_D1OPEN:
    *width = 62;
    *height = 11;
    *dataptr = (uint8_t *)image_d1_open;
    break;
  case IMG_8875_D1CLOSED:
    *width = 62;
    *height = 11;
    *dataptr = (uint8_t *)image_d1_closed;
    break;
    
  case IMG_8875_D2OPEN:
    *width = 62;
    *height = 11;
    *dataptr = (uint8_t *)image_d2_open;
    break;
  case IMG_8875_D2CLOSED:
    *width = 62;
    *height = 11;
    *dataptr = (uint8_t *)image_d2_closed;
    break;
    
  case IMG_8875_APPLEBATTERY:
    // FIXME
    return false;
    break;
  case IMG_9341_SHELL:
    *width = 320;
    *height = 240;
    *dataptr = (uint8_t *)img_9341_bg;
    break;
  case IMG_9341_D1OPEN: // d2 is the same constant
    *width = 43;
    *height = 20;
    *dataptr = (uint8_t *)image_9341_driveopen;
    break;
  case IMG_9341_D1CLOSED: // d2 is the same constant
    *width = 43;
    *height = 20;
    *dataptr = (uint8_t *)image_9341_driveclosed;
    break;
  case IMG_9341_APPLEBATTERY:
    // FIXME
    return false;
    break;
    
  default:
    // Don't know what this one is...
    return false;
    break;
  }

  return true;
}

