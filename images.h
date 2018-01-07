#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define DBITMAP_HEIGHT 240
#define DBITMAP_WIDTH 320
// RGB
extern const uint8_t displayBitmap[];

// 43 x 20 RGB
extern const uint8_t driveLatch[];

// 43 x 20 RGB
extern const uint8_t driveLatchOpen[];

// 10 x 11, RGBA
extern const uint8_t appleBitmap[];
