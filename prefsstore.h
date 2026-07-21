#ifndef __PREFSSTORE_H
#define __PREFSSTORE_H

#include <stdint.h>

// Fun trivia: the Apple //e was in production from January 1983 to
// November 1993. And the 65C02 in them supported weird BCD math modes.
#define PREFSMAGIC 0x01831093
#define PREFSVERSION 13

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

  // v10+: WiFi credentials for the Teensy's ESP co-processor (Uthernet MAC-RAW).
  // Carved from reserved so the disk/hd path offsets and the footer do not move.
  char wifiSSID[33];
  char wifiPass[64];

  // v11+: inbound NAT (Uthernet) port forwarding. natFwd is a comma-separated
  // list of Apple listen ports to expose to the outside (e.g. "6580,23"); the
  // host/ESP port is derived per-platform (SDL bumps privileged ports by
  // natPortOffset to avoid needing root; Teensy uses the port as-is). Carved
  // from reserved so the disk/hd path offsets and footer do not move.
  uint16_t natPortOffset;
  char natFwd[48];

  // v12+: user-mode NAT subnet as a dotted network address (e.g. "10.0.2.0").
  // A /24 is assumed: the gateway is .2, the advertised DNS is .3, and the
  // Apple's DHCP lease is .15. Carved from reserved so the disk/hd path offsets
  // and the footer do not move.
  char natSubnet[16];

  // v13+: upstream DNS resolver the user-mode NAT proxies name lookups to (the
  // Apple's queries to the advertised .3 are forwarded here; the SDL hardware
  // path advertises this address directly). Dotted, e.g. "8.8.8.8". Carved from
  // reserved so the disk/hd path offsets and the footer do not move.
  char natDns[16];

  char reserved[MAXPATH - 4 - 5 - 1 - 2 - 1 - 33 - 64 - 2 - 48 - 16 - 16]; // 255 is the Teensy MAXPATH size (less fields above)

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
