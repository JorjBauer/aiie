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

void TeensyFileManager::truncate(int8_t fd)
{
  /* Not used in the code anywhere, yet, and unimplemented here... */
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
  File f = sd.open(cachedNames[fd], FILE_READ);
  if (!f) {
    Serial.println("failed to open");
    return false;
  }

  if (!f.seek(fileSeekPositions[fd])) {
    Serial.println("readTrack: seek failed");
    f.close();
    return false;
  }

  int nRead = f.read(toWhere, isNib ? 0x1a00 : (256 * 16));
  f.close();
  return (nRead == (isNib ? 0x1a00 : (256 * 16)));
}

bool TeensyFileManager::readBlock(int8_t fd, uint8_t *toWhere, bool isNib)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  File f = sd.open(cachedNames[fd], FILE_READ);
  if (!f) {
    Serial.println("failed to open");
    return false;
  }

  if (!f.seek(fileSeekPositions[fd])) {
    Serial.println("readBlock: seek failed");
    f.close();
    return false;
  }

  int nRead = f.read(toWhere, isNib ? 416 : 256);
  f.close();
  return (nRead == (isNib ? 416 : 256));
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
  File f = sd.open(cachedNames[fd], FILE_WRITE);
  if (!f ||
      !f.seek(fileSeekPositions[fd])) {
    f.close();
    return false;
  }

  int nWritten = f.write(fromWhere, 256);
  f.close();
  return (nWritten == 256);
}

bool TeensyFileManager::writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  File f = sd.open(cachedNames[fd], FILE_WRITE);
  if (!f)
    return false;

  if (!f.seek(fileSeekPositions[fd])) {
    f.close();
    return false;
  }

  int nWritten = f.write(fromWhere, isNib ? 0x1a00 : (256*16));
  f.close();
  return (nWritten == (isNib ? 0x1a00 : (256*16)));
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

uint8_t TeensyFileManager::readByteAt(int8_t fd, uint32_t pos)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  _prepCache(fd);

  if (!rawFile.seek(pos)) {
    Serial.print("readByteAt: seek failed to pos ");
    Serial.println(pos);
    Serial.println("Trying to continue anyway");
    //    return false;
  }
  uint8_t b;
  return (rawFile.read(&b, 1) == 1);
}

bool TeensyFileManager::writeByteAt(int8_t fd, uint8_t v, uint32_t pos)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  _prepCache(fd);

  if (!rawFile.seek(pos))
    return false;

  return (rawFile.write(&v, 1) == 1);
}

// FIXME: the semantics of this are wrong - lots of 'return false' for a uint8_t
uint8_t TeensyFileManager::readByte(int8_t fd)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];

  if (!rawFile.seek(pos)) {
    Serial.print("readByte: seek failed to byte ");
    Serial.println(pos);
    return false;
  }

  uint8_t b;
  rawFile.read(&b, 1);
  fileSeekPositions[fd]++;

  // FIXME: check v == 1 & handle error
  return b;
}

bool TeensyFileManager::writeByte(int8_t fd, uint8_t v)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached) {
    Serial.println("failed writeByte - invalid fd");
    return false;
  }

  if (cachedNames[fd][0] == 0) {
    Serial.println("failed writeByte - no cache name");
    return false;
  }

  _prepCache(fd);

  uint32_t pos = fileSeekPositions[fd];

  if (!rawFile.seek(pos)) {
    return false;
  }

  if (rawFile.write(&v, 1) != 1) {
    return false;
  }

  fileSeekPositions[fd]++;

  return true;
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

