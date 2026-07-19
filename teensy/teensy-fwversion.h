#ifndef __TEENSY_FWVERSION_H
#define __TEENSY_FWVERSION_H

// Firmware version string, used by the SD-card self-update to show the user
// "installed X -> new Y" before it commits.
//
// The version is the build timestamp: it is automatic (no manual bumping) and
// directly answers the question that matters for SD updates -- "is this the
// build I just compiled?". It reflects the last time the translation unit that
// defines the blob (teensy.ino) was compiled, so do a clean build if you need
// it guaranteed fresh.
//
// The value is embedded in the firmware image behind AIIE_FW_MAGIC so the
// updater can locate it by scanning a candidate /AIIE.HEX image (which is just
// the raw program), not only in the running firmware. Layout in flash:
//
//     "AiiEFWV" 0x1e <version text> 0x1e
//
// 0x1e (ASCII record separator) is used as a delimiter that will not appear in
// a __DATE__/__TIME__ string.
#define AIIE_FW_MAGIC    "AiiEFWV\x1e"
#define AIIE_FW_VERSION  __DATE__ " " __TIME__

// The running firmware's version (defined in teensy.ino alongside the blob).
extern const char *g_fwVersion;

#endif
