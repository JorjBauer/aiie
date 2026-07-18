#ifndef __PREFSSTORE_H
#define __PREFSSTORE_H

#include <stdint.h>

// Fun trivia: the Apple //e was in production from January 1983 to
// November 1993. And the 65C02 in them supported weird BCD math modes.
#define PREFSMAGIC 0x01831093
#define PREFSVERSION 9

#ifndef MAXPATH
#define MAXPATH 255
#endif

// The Teensy 3.6 has 4096 bytes of flash. We want this to stay under
// that size.
typedef struct _prefs {
  uint32_t magic;
  uint16_t prefsSize;
  uint8_t version;

  uint8_t volume;
  uint8_t displayType;
  uint8_t luminanceCutoff;
  
  uint8_t debug;
  uint8_t speed;

  uint8_t invertPaddleX;
  uint8_t invertPaddleY;

  uint16_t windowWidth;
  uint16_t windowHeight;

  uint8_t slotDiskII;
  uint8_t slotParallel;
  uint8_t slotHD32;
  uint8_t slotMouse;
  uint8_t slotMockingboard;

  uint8_t ramworksSize; // v7+: aux expansion size in MB (0/1/3/16)

  // v8+: speed in half-speed steps, like 'speed', but wide enough for
  // 128x (256 steps) and up. 'speed' saturates at 255 (127.5x) and is
  // still written for older readers. Carved out of 'reserved' so the
  // struct layout is unchanged.
  uint16_t speed16;

  // v9+: Uthernet (W5100) card slot; 0 = disabled. Carved from reserved after
  // speed16 so the offsets of the disk/hd paths and the footer do not move.
  uint8_t slotUthernet;

  char reserved[MAXPATH - 4 - 5 - 1 - 2 - 1]; // 255 is the Teensy MAXPATH size (less fields above)

  char disk1[MAXPATH];
  char disk2[MAXPATH];
  char hd1[MAXPATH];
  char hd2[MAXPATH];

  uint32_t magicFooter;
} prefs_t;

class PrefsStore {
 public:
  PrefsStore() {};
  virtual ~PrefsStore() {};

  virtual bool readPrefs(prefs_t *readTo) = 0;
  virtual bool writePrefs(prefs_t *newPrefs) = 0;
};

#endif
