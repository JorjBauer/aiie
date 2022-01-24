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
 *   then use util/genimage.pl to generate the bytestream and copy/paste
 */

#include "image-8875-bg.h"
#include "image-8875-apple.h"
#include "image-8875-drivelatches.h"

const bool getImageInfoAndData(uint8_t imgnum, uint16_t *width, uint16_t *height, const uint8_t **dataptr)
{
  switch (imgnum) {
  case IMG_SHELL:
    *width = 800;
    *height = 480;
    *dataptr = image_8875_bg;
    break;
  case IMG_D1OPEN:
    *width = 62;
    *height = 11;
    *dataptr = image_d1_open;
    break;
  case IMG_D1CLOSED:
    *width = 62;
    *height = 11;
    *dataptr = image_d1_closed;
    break;
    
  case IMG_D2OPEN:
    *width = 62;
    *height = 11;
    *dataptr = image_d2_open;
    break;
  case IMG_D2CLOSED:
    *width = 62;
    *height = 11;
    *dataptr = image_d2_closed;
    break;
    
  case IMG_APPLEBATTERY:
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

