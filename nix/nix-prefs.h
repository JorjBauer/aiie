#ifndef _NIX_PREFSSTORE_H
#define _NIX_PREFSSTORE_H

#include "prefsstore.h"

class NixPrefs : public PrefsStore {
 public:
  NixPrefs();
  virtual ~NixPrefs();

  virtual bool readPrefs(prefs_t *readTo);
  virtual bool writePrefs(prefs_t *newPrefs);
  
 private:
  char *prefsFilePath;
};

#endif
