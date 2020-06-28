#include <Arduino.h>
#include <wchar.h>
#include <SdFat.h>
#include "teensy-filemanager.h"
#include <string.h> // strcpy


// FIXME: globals are yucky.
SdFatSdio sd;
File file;

int8_t rawFd;
File rawFile;

TeensyFileManager::TeensyFileManager()
{
  numCached = 0;

  enabled = sd.begin();
}

TeensyFileManager::~TeensyFileManager()
{
}

int8_t TeensyFileManager::openFile(const char *name)
{
  if (rawFd != -1) {
    rawFile.close();
    rawFd = -1;
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
  if (rawFd != -1) {
    rawFile.close();
    rawFd = -1;
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
  File f = sd.open(where);

  while (1) {
    File e = f.openNextFile();
    if (!e) {
      // No more - all done.
      f.close();
      return -1;
    }

    // Skip MAC fork files
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
	while (*p && strlen(p)) {
	  if (!strncasecmp(fsuff, p, 3)) {
	    matchesAny = true;
	    break;
	  }
	  p = strstr(p, ",")+1;
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

    if (idxCount == startIdx) {
      if (e.isDirectory()) {
      	strcat(outputFN, "/");
      }
      e.close();
      f.close();
      return idxCount;
    }

    idxCount++;
  }

  /* NOTREACHED */
}

bool TeensyFileManager::_prepCache(int8_t fd)
{
  if (rawFd == -1 ||
      rawFd != fd) {

    // Not our cached file, or we have no cached file
    if (rawFd != -1) {
      // Close the old one if we had one
      Serial.print("closing old cache file ");
      Serial.println(rawFd);
      rawFile.close();
      rawFd = -1;
    }

    Serial.println("opening new cache file");
    // Open the new one
    rawFile = sd.open(cachedNames[fd], O_RDWR | O_CREAT);
    if (!rawFile) {
      Serial.print("_prepCache: failed to open ");
      Serial.println(cachedNames[fd]);
      return false;
    }
    rawFd = fd; // cache is live
    Serial.print("New cache file is ");
    Serial.println(fd);
  } else {
    //    Serial.println("reopning same cache");
  }

  return true; // FIXME error handling
}

void TeensyFileManager::getRootPath(char *toWhere, int8_t maxLen)
{
  strcpy(toWhere, "/A2DISKS/");
  //  strncpy(toWhere, "/A2DISKS/", maxLen);
}

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


void TeensyFileManager::seekToEnd(int8_t fd)
{
  File f = sd.open(cachedNames[fd], FILE_READ);
  if (!f) {
    Serial.println("failed to open");
    return;
  }

  fileSeekPositions[fd] = f.fileSize();

  f.close();
}

int TeensyFileManager::write(int8_t fd, const void *buf, int nbyte)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached) {
    Serial.println("failed write - invalid fd");
    return -1;
  }

  if (cachedNames[fd][0] == 0) {
    Serial.println("failed write - no cache name");
    return -1;
  }

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];

  if (!rawFile.seek(pos)) {
    return -1;
  }

  if (rawFile.write(buf, nbyte) != nbyte) {
    return -1;
  }

  fileSeekPositions[fd] += nbyte;
  rawFile.close();
  return nbyte;
};

int TeensyFileManager::read(int8_t fd, void *buf, int nbyte)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return -1;

  if (cachedNames[fd][0] == 0)
    return -1;

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];

  if (!rawFile.seek(pos)) {
    Serial.print("readByte: seek failed to byte ");
    Serial.println(pos);
    return -1;
  }

  if (rawFile.read(buf, nbyte) != nbyte)
    return -1;

  fileSeekPositions[fd] += nbyte;
  rawFile.close();
  
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

