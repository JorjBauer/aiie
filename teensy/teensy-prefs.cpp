#include "teensy-prefs.h"

#include <Arduino.h>
#include <EEPROM.h>

TeensyPrefs::TeensyPrefs()
{
}

TeensyPrefs::~TeensyPrefs()
{
}

bool TeensyPrefs::readPrefs(prefs_t *readTo)
{
  uint8_t *pp = (uint8_t *)readTo;
  for (uint16_t i=0; i<sizeof(prefs_t); i++) {
    *pp++ = EEPROM.read(i);
  }

  if (readTo->magic != PREFSMAGIC ||
      readTo->magicFooter != PREFSMAGIC) {
    return false;
  }
  if (readTo->prefsSize != sizeof(prefs_t)) {
    return false;
  }
  // ONE VERSION BACK IS STILL READ, matching nix/nix-prefs.cpp. Fields are
  // only ever carved out of 'reserved', so the layout does not move and an
  // older blob's fields all still mean what they meant; each new field is
  // gated on its own version test at the call site. Insisting on an exact
  // match threw away every setting on the board when it ran a
  // firmware with one field more, which is a lot of retyping for a byte
  // that was already zero.
  if (readTo->version != PREFSVERSION &&
      readTo->version != PREFSVERSION - 1) {
    return false;
  }

  return true;
}

bool TeensyPrefs::writePrefs(prefs_t *newPrefs)
{
  uint8_t *pp = (uint8_t *)newPrefs;
  for (uint16_t i=0; i<sizeof(prefs_t); i++) {
    EEPROM.write(i, *pp++);
  }

  return true;
}
