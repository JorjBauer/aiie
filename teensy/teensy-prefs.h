#ifndef _TEENSY_PREFSSTORE_H
#define _TEENSY_PREFSSTORE_H

#include "prefsstore.h"

class TeensyPrefs : public PrefsStore {
 public:
  TeensyPrefs();
  virtual ~TeensyPrefs();

  virtual bool readPrefs(prefs_t *readTo);
  virtual bool writePrefs(prefs_t *newPrefs);
};

#endif
