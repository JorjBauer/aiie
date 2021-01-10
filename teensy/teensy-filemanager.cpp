#include <Arduino.h>
#include <wchar.h>
#include "teensy-filemanager.h"
#include <string.h> // strcpy
#include "teensy-println.h"

/* FIXME globals */
static SdFat sd;
static FsFile cacheFile;
static FsFile outerDir;

#include "iocompat.h"

TeensyFileManager::TeensyFileManager()
{
  numCached = 0;

  // FIXME: used to have 'enabled = sd.begin()' here, but we weren't
  // using the enabled flag, so I've removed it to save the RAM for
  // now; but eventually we need better error handling here
  sd.begin(SdioConfig(FIFO_SDIO));
}

TeensyFileManager::~TeensyFileManager()
{
}

int8_t TeensyFileManager::openFile(const char *name)
{
  if (cacheFd != -1) {
    cacheFile.close();
    cacheFd = -1;
  }

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
  if (cacheFd != -1) {
    cacheFile.close();
    cacheFd = -1;
  }

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

void TeensyFileManager::closeDir()
{
  // FIXME: this should Threads::Scope lock, but it's being called
  // from readDir, so that would block
  if (outerDir) {
    outerDir.close();
  }
}

// suffix may be comma-separated
int16_t TeensyFileManager::readDir(const char *where, const char *suffix, char *outputFN, int16_t startIdx, uint16_t maxlen)
{
  // First entry is always "../" if we're in a subdir of the root
  if (startIdx == 0 || !outerDir) {
    if (outerDir)
      closeDir();
    char buf[255];
    strcpy(buf, where);
    if (strlen(buf) > 1 && buf[strlen(buf)-1] == '/') {
      buf[strlen(buf)-1] = 0;
    }
    if (!outerDir.open(buf, O_RDONLY)) {
      return -1;
    }
    if (strcmp(where, "/")) { // FIXME: is this correct for the root?
      strcpy(outputFN, "../");
      return 0;
    }
  }

  outputFN[0] = '\0';

  FsFile e;
  while (e.openNext(&outerDir, O_RDONLY)) {

    // Skip MAC fork files
    // FIXME: strncpy
    strcpy(outputFN, e.name()); // and we need maxlen-1 for trailing '/' on directories
    e.getName(outputFN, maxlen-1); // -1 for trailing '/' on directories

    if (outputFN[0] == '.') {
      e.close();
      continue;
    }

    // skip anything that has the wrong suffix and isn't a directory
    if (suffix && !e.isDirectory() && strlen(outputFN) >= 3) {
      const char *fsuff = &outputFN[strlen(outputFN)-3];
      if (strstr(suffix, ","))  {
	// multiple suffixes to check - all must be 3 chars long, FIXME
	bool matchesAny = false;
	const char *p = suffix;
	while (p && *p && strlen(p)) {
	  if (!strncasecmp(fsuff, p, 3)) {
	    matchesAny = true;
	    break;
	  }
	  p = strstr(p, ",");
	  if (p) p++;
	}
	if (!matchesAny) {
	  e.close();
	  continue;
	}
      } else {
	// one suffix to check
	if (strcasecmp(fsuff, suffix)) {
	  e.close();
	  continue;
	}
      }
    }

    if (e.isDirectory()) {
      strcat(outputFN, "/");
    }
    e.close();
    
    return startIdx;
  }

  /* Reached the end of the files */
  
  closeDir();
  return -1;
}

bool TeensyFileManager::_prepCache(int8_t fd)
{
  if (cacheFd == -1 ||
      cacheFd != fd) {

    // Not our cached file, or we have no cached file
    if (cacheFd != -1) {
      // Close the old one if we had one
      cacheFile.close();
      cacheFd = -1;
    }

    // Open the new one
    cacheFile.open(cachedNames[fd], O_CREAT|O_RDWR);
    if (!cacheFile) {
      printf("failed to open cacheFile\n");
      return false;
    }
    cacheFd = fd; // cache is live
  }

  return true;
}

void TeensyFileManager::getRootPath(char *toWhere, int8_t maxLen)
{
  strcpy(toWhere, "/A2DISKS/");
  //  strncpy(toWhere, "/A2DISKS/", maxLen);
}

// FIXME: this should be private
bool TeensyFileManager::setSeekPosition(int8_t fd, uint32_t pos)
{
  seekToEnd(fd);
  uint32_t endPos = getSeekPosition(fd);
  if (pos >= endPos) {
    return false;
  }

  fileSeekPositions[fd] = pos;
  return true;
}

// FIXME: this should be private
void TeensyFileManager::seekToEnd(int8_t fd)
{
  FsFile f;
  f.open(cachedNames[fd], O_RDONLY);
  if (!f) {
    return;
  }

  fileSeekPositions[fd] = f.size();
  f.close();
}

int TeensyFileManager::write(int8_t fd, const void *buf, int nbyte)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached) {
    printf("no fd\n");
    return -1;
  }

  if (cachedNames[fd][0] == 0) {
    printf("no name\n");
    return -1;
  }

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];

  if (!cacheFile.seek(pos)) {
    printf("can't seek to %d\n", pos);
    return -1;
  }

  if (cacheFile.write((const uint8_t *)buf, (size_t)nbyte) != (size_t)nbyte) {
    printf("can't write\n");
    return -1;
  }

  fileSeekPositions[fd] += nbyte;
  return nbyte;
};

int TeensyFileManager::read(int8_t fd, void *buf, int nbyte)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached) {
    return -1;
  }

  if (cachedNames[fd][0] == 0) {
    return -1;
  }

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];
  if (!cacheFile.seek(pos)) {
    return -1;
  }

  fileSeekPositions[fd] += nbyte;

  if (cacheFile.read(buf, nbyte) != nbyte) {
    return -1;
  }
  
  return nbyte;
};

int TeensyFileManager::lseek(int8_t fd, int offset, int whence)
{
  if (whence == SEEK_CUR && offset == 0) {
    return fileSeekPositions[fd];
  }
  if (whence == SEEK_SET) {
    if (!setSeekPosition(fd, offset))
      return -1;
    return offset;
  }
  // Other cases not supported yet                                                                                                      
  return -1;
};

