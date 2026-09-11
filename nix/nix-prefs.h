#ifndef _NIX_PREFSSTORE_H
#define _NIX_PREFSSTORE_H

#include "prefsstore.h"

class NixPrefs : public PrefsStore {
 public:
  // WHERE THE PREFERENCES LIVE, for the whole process. Default is
  // ~/.aiie. Set it once from the command line (--prefs) before anything
  // reads or writes: every NixPrefs is built fresh at each use, so a
  // per-object path would have to be threaded through three call sites
  // that have no business knowing about it.
  //
  // A SEPARATE FILE IS WHAT MAKES A SCRATCH INSTANCE SAFE. Any quit
  // writes preferences, so an instance started for a test otherwise
  // saves its own disks and window size over the real ones.
  static void setPath(const char *path);

  NixPrefs();
  virtual ~NixPrefs();

  virtual bool readPrefs(prefs_t *readTo);
  virtual bool writePrefs(prefs_t *newPrefs);
  
 private:
  char *prefsFilePath;
};

#endif
