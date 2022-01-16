#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define DBITMAP_HEIGHT 480
#define DBITMAP_WIDTH 800
#define LATCH_HEIGHT (20*2)
#define LATCH_WIDTH  (43*2)
#define LATCH_XSPACING 340

// Spacing and positioning of elements within the DBITMAP, used by AppleUI
#define LED0_XPOS 312
#define LED0_YPOS 412
#define LED1_XPOS 649
#define LED1_YPOS 412
#define LED_HEIGHT 3
#define LED_WIDTH 16

// RGB
extern const uint8_t displayBitmap[];

// RGB
extern const uint8_t driveLatch[];

// RGB
extern const uint8_t driveLatchOpen[];

// 10 x 11, RGBA
extern const uint8_t appleBitmap[];
