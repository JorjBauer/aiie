#include <string.h> // strcpy
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

#include "sdl-filemanager.h"


SDLFileManager::SDLFileManager()
{
  numCached = 0;
}

SDLFileManager::~SDLFileManager()
{
}

int8_t SDLFileManager::openFile(const char *name)
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

void SDLFileManager::closeFile(int8_t fd)
{
  // invalid fd provided?
  if (fd < 0 || fd >= numCached)
    return;

  // clear the name
  cachedNames[fd][0] = '\0';
}

const char *SDLFileManager::fileName(int8_t fd)
{
  if (fd < 0 || fd >= numCached)
    return NULL;

  return cachedNames[fd];
}

int8_t SDLFileManager::readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen)
{
  // not used in this version
  return -1;
}

void SDLFileManager::seekBlock(int8_t fd, uint16_t block, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return;

  if (isNib) {
    fileSeekPositions[fd] = block * 416;
  } else {
    fileSeekPositions[fd] = block * 256;
  }
}


bool SDLFileManager::readTrack(int8_t fd, uint8_t *toWhere, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd) {
    lseek(ffd, fileSeekPositions[fd], SEEK_SET);
    if (isNib) {
      ret = (read(ffd, toWhere, 0x1A00) == 0x1A00);
    } else {
      ret = (read(ffd, toWhere, 256 * 16) == 256 * 16);
    }
    close(ffd);
  }

  return ret;
}

bool SDLFileManager::readBlock(int8_t fd, uint8_t *toWhere, bool isNib)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd) {
    lseek(ffd, fileSeekPositions[fd], SEEK_SET);
    if (isNib) {
      ret = (read(ffd, toWhere, 416) == 416);
    } else {
      ret = (read(ffd, toWhere, 256) == 256);
    }
    close(ffd);
  }

  return ret;
}

bool SDLFileManager::writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // don't know how to do this without seeking through the nibblized
  // track data, so just give up for now
  if (isNib)
    return false;

  // open, seek, write, close.
  int ffd = open(cachedNames[fd], O_WRONLY);
  if (ffd) {
    if (lseek(ffd, fileSeekPositions[fd], SEEK_SET) != fileSeekPositions[fd]) {
      printf("ERROR: failed to seek to %lu\n", fileSeekPositions[fd]);
      return false;
    }
    if (write(ffd, fromWhere, 256) != 256) {
      printf("ERROR: failed to write 256 bytes\n");
      return false;
    }
    close(ffd);
  }
  return true;
}

bool SDLFileManager::writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  int ffd = open(cachedNames[fd], O_WRONLY);
  if (ffd) {
    if (lseek(ffd, fileSeekPositions[fd], SEEK_SET) != fileSeekPositions[fd]) {
      printf("ERROR: failed to seek to %lu\n", fileSeekPositions[fd]);
      return false;
    }
    int16_t wrsize = 256 * 16;
    if (isNib)
      wrsize = 0x1A00;

    if (write(ffd, fromWhere, wrsize) != wrsize) {
      printf("ERROR: failed to write bytes\n");
      return false;
    }
    close(ffd);
  }
  return true;
}

uint8_t SDLFileManager::readByteAt(int8_t fd, uint32_t pos)
{
  if (fd < 0 || fd >= numCached)
    return -1; // FIXME: error handling?

  if (cachedNames[fd][0] == 0)
    return -1; // FIXME: error handling?

  uint8_t v = 0;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd) {
    lseek(ffd, pos, SEEK_SET);
    ret = (read(ffd, &v, 1) == 1);
    close(ffd);
  }

  if (!ret) {
    printf("ERROR reading: %d\n", errno);
  }

  // FIXME: error handling?
  return v;
}

bool SDLFileManager::writeByteAt(int8_t fd, uint8_t v, uint32_t pos)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_WRONLY);
  if (ffd) {
    lseek(ffd, pos, SEEK_SET);
    ret = (write(ffd, &v, 1) == 1);
    close(ffd);
  }

  return ret;
}

