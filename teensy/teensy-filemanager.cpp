#include <Arduino.h>
#include <wchar.h>
#include "ff.h"                   // File System
#include "teensy-filemanager.h"
#include <string.h> // strcpy


// FIXME: globals are yucky.
DIR dir;
FILINFO fno;
FIL fil;

static TCHAR *char2tchar( const char *charString, int nn, TCHAR *output)
{
  int ii;
  for (ii=0; ii<nn; ii++) {
    output[ii] = (TCHAR)charString[ii];
    if (!charString[ii])
      break;
  }
  return output;
}

static char * tchar2char( const TCHAR * tcharString, int nn, char * charString)
{   int ii;
  for(ii = 0; ii<nn; ii++)
    { charString[ii] = (char)tcharString[ii];
      if(!charString[ii]) break;
    }
  return charString;
}


TeensyFileManager::TeensyFileManager()
{
  numCached = 0;
}

TeensyFileManager::~TeensyFileManager()
{
}

int8_t TeensyFileManager::openFile(const char *name)
{
  // See if there's a hole to re-use...
  for (int i=0; i<numCached; i++) {
    if (cachedNames[i][0] == '\0') {
      strncpy(cachedNames[i], name, MAXPATH-1);
      cachedNames[i][MAXPATH-1] = '\0'; // safety: ensure string terminator
      fileSeekPositions[i] = 0;
      return i;
    }
  }

  // check for too many open files
  if (numCached >= MAXFILES)
    return -1;


  // No, so we'll add it to the end
  strncpy(cachedNames[numCached], name, MAXPATH-1);
  cachedNames[numCached][MAXPATH-1] = '\0'; // safety: ensure string terminator
  fileSeekPositions[numCached] = 0;

  numCached++;
  return numCached-1;
}

void TeensyFileManager::closeFile(int8_t fd)
{
  // invalid fd provided?
  if (fd < 0 || fd >= numCached)
    return;

  // clear the name
  cachedNames[fd][0] = '\0';
}

const char *TeensyFileManager::fileName(int8_t fd)
{
  if (fd < 0 || fd >= numCached)
    return NULL;

  return cachedNames[fd];
}

// suffix may be comma-separated
int8_t TeensyFileManager::readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen)
{
  //  ... open, read, save next name, close, return name. Horribly
  //  inefficient but hopefully won't break the sd layer. And if it
  //  works then we can make this more efficient later.

  // First entry is always "../"
  if (startIdx == 0) {
      strcpy(outputFN, "../");
      return 0;
  }

  int8_t idxCount = 1;
  TCHAR buf[MAXPATH];
  char2tchar(where, MAXPATH, buf);
  buf[strlen(where)-1] = '\0'; // this library doesn't want trailing slashes
  FRESULT rc = f_opendir(&dir, buf);
  if (rc) {
    Serial.printf("f_opendir '%s' failed: %d\n", where, rc);
    return -1;
  }

  while (1) {
    rc = f_readdir(&dir, &fno);
    if (rc || !fno.fname[0]) {
      // No more - all done.
      f_closedir(&dir);
      return -1;
    }

    if (fno.fname[0] == '.' || fno.fname[0] == '_' || fno.fname[0] == '~') {
      // skip MAC fork files and any that have been deleted :/
      continue;
    }

    // skip anything that has the wrong suffix
    char fn[MAXPATH];
    tchar2char(fno.fname, MAXPATH, fn);
    if (suffix && !(fno.fattrib & AM_DIR) && strlen(fn) >= 3) {
      const char *fsuff = &fn[strlen(fn)-3];
      if (strstr(suffix, ","))  {
	// multiple suffixes to check
	bool matchesAny = false;
	const char *p = suffix;
	while (p && strlen(p)) {
	  if (!strncasecmp(fsuff, p, 3)) {
	    matchesAny = true;
	    break;
	  }
	  p = strstr(p, ",")+1;
	}
	if (matchesAny)
	  continue;
      } else {
	// one suffix to check
	if (strcasecmp(fsuff, suffix))
	  continue;
      }
    }

    if (idxCount == startIdx) {
      if (fno.fattrib & AM_DIR) {
	strcat(fn, "/");
      }
      strncpy(outputFN, fn, maxlen);
      f_closedir(&dir);
      return idxCount;
    }

    idxCount++;
  }

  /* NOTREACHED */
}

void TeensyFileManager::seekBlock(int8_t fd, uint16_t block, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return;

  fileSeekPositions[fd] = block * (isNib ? 416 : 256);
}


bool TeensyFileManager::readTrack(int8_t fd, uint8_t *toWhere, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  TCHAR buf[MAXPATH];
  char2tchar(cachedNames[fd], MAXPATH, buf);
  FRESULT rc = f_open(&fil, (TCHAR*) buf, FA_READ);
  if (rc) {
    Serial.println("failed to open");
    return false;
  }

  rc = f_lseek(&fil, fileSeekPositions[fd]);
  if (rc) {
    Serial.println("readTrack: seek failed");
    f_close(&fil);
    return false;
  }

  UINT v;
  f_read(&fil, toWhere, isNib ? 0x1a00 : (256 * 16), &v);
  f_close(&fil);
  return (v == (isNib ? 0x1a00 : (256 * 16)));
}

bool TeensyFileManager::readBlock(int8_t fd, uint8_t *toWhere, bool isNib)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  TCHAR buf[MAXPATH];
  char2tchar(cachedNames[fd], MAXPATH, buf);
  FRESULT rc = f_open(&fil, (TCHAR*) buf, FA_READ);
  if (rc) {
    Serial.println("failed to open");
    return false;
  }

  rc = f_lseek(&fil, fileSeekPositions[fd]);
  if (rc) {
    Serial.println("readBlock: seek failed");
    f_close(&fil);
    return false;
  }
  UINT v;
  f_read(&fil, toWhere, isNib ? 416 : 256, &v);
  f_close(&fil);
  return (v == (isNib ? 416 : 256));
}

bool TeensyFileManager::writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // can't write just a single block of a nibblized track
  if (isNib)
    return false;

  // open, seek, write, close.
  TCHAR buf[MAXPATH];
  char2tchar(cachedNames[fd], MAXPATH, buf);
  FRESULT rc = f_open(&fil, (TCHAR*) buf, FA_WRITE);
  rc = f_lseek(&fil, fileSeekPositions[fd]);
  UINT v;
  f_write(&fil, fromWhere, 256, &v);
  f_close(&fil);
  return (v == 256);
}

bool TeensyFileManager::writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  TCHAR buf[MAXPATH];
  char2tchar(cachedNames[fd], MAXPATH, buf);
  FRESULT rc = f_open(&fil, (TCHAR*) buf, FA_WRITE);
  rc = f_lseek(&fil, fileSeekPositions[fd]);
  UINT v;
  f_write(&fil, fromWhere, isNib ? 0x1a00 : (256*16), &v);
  f_close(&fil);
  return (v == (isNib ? 0x1a00 : (256*16)));
}

