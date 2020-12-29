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

  if (readTo->magic != PREFSMAGIC) {
    return false;
  }
  if (readTo->prefsSize != sizeof(prefs_t)) {
    return false;
  }
  if (readTo->version != PREFSVERSION) {
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
