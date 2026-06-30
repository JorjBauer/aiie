#ifndef __TEENSY_SELFUPDATE_H
#define __TEENSY_SELFUPDATE_H

// Reflash the Teensy from an Intel-HEX firmware image on the SD card.
//
// Reads 'path' (e.g. "/AIIE.HEX"), buffers it into the spare upper region
// of program flash, verifies it is a Teensy 4.1 image, then moves it into
// place and reboots -- so on SUCCESS THIS FUNCTION DOES NOT RETURN.
//
// Returns false (machine left running) if the file is missing, malformed,
// too large, or not built for this board. Progress and errors are shown on
// g_display. Intended to be driven from the BIOS, where the CPU is already
// halted and the user has explicitly confirmed the update.
bool teensySelfUpdateFromSD(const char *path);

#endif
