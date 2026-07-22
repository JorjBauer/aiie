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
//     "AiiEFWV" 0x1e <version text> 0x00
//
// The 0x1e (ASCII record separator) inside the magic separates the tag from the
// version text; it will not appear in a __DATE__/__TIME__ string. The version
// text itself is NUL-terminated (the string blob's implicit terminator), which
// is both what the updater's scanner stops on and what lets the running firmware
// point g_fwVersion straight into the blob for the "Installed:" line. (The
// scanner also stops on a trailing 0x1e, so images from older builds that
// delimited the version with 0x1e still read correctly.)
#define AIIE_FW_MAGIC    "AiiEFWV\x1e"
#define AIIE_FW_VERSION  __DATE__ " " __TIME__

// The running firmware's version (defined in teensy.ino alongside the blob).
extern const char *g_fwVersion;

#endif
