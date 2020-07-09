#ifndef __PREFSSTORE_H
#define __PREFSSTORE_H

#include <stdint.h>

// Fun trivia: the Apple //e was in production from January 1983 to
// November 1993. And the 65C02 in them supported weird BCD math modes.
#define PREFSMAGIC 0x01831093
#define PREFSVERSION 3

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
  uint8_t debug;
  uint8_t speed;

  uint8_t invertPaddleX;
  uint8_t invertPaddleY;

  char reserved[MAXPATH]; // 255 is the Teensy MAXPATH size

  char disk1[MAXPATH];
  char disk2[MAXPATH];
  char hd1[MAXPATH];
  char hd2[MAXPATH];
} prefs_t;

class PrefsStore {
 public:
  PrefsStore() {};
  virtual ~PrefsStore() {};

  virtual bool readPrefs(prefs_t *readTo) = 0;
  virtual bool writePrefs(prefs_t *newPrefs) = 0;
};

#endif
