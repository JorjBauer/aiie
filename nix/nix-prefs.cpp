#include "nix-prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>

NixPrefs::NixPrefs()
{
  struct passwd *pw = getpwuid(getuid());

  char *homedir = pw->pw_dir;
  prefsFilePath = (char *)malloc(strlen(homedir) + 1 + strlen(".aiie") + 1);
  
  strcpy(prefsFilePath, homedir);
  strcat(prefsFilePath, "/");
  strcat(prefsFilePath, ".aiie");
}

NixPrefs::~NixPrefs()
{
  if (prefsFilePath)
    free(prefsFilePath);
}

bool NixPrefs::readPrefs(prefs_t *readTo)
{
  FILE *f = fopen(prefsFilePath, "r");
  if (!f)
    return false;

  if (fread(readTo, sizeof(prefs_t), 1, f) != 1) {
    fclose(f);
    return false;
  }

  fclose(f);
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

bool NixPrefs::writePrefs(prefs_t *newPrefs)
{
  FILE *f = fopen(prefsFilePath, "w");
  if (!f)
    return false;

  if (fwrite(newPrefs, sizeof(prefs_t), 1, f) != 1) {
    fclose(f);
    return false;
  }

  fclose(f);
  return true;
}
