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

// RGB
extern const uint8_t displayBitmap[];

// RGB
extern const uint8_t drive1LatchClosed[];

// RGB
extern const uint8_t drive1LatchOpen[];

// RGB
extern const uint8_t drive2LatchClosed[];

// RGB
extern const uint8_t drive2LatchOpen[];

// 10 x 11, RGBA
extern const uint8_t appleBitmap[];
