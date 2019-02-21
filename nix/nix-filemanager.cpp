#include <string.h> // strcpy
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>

#include "nix-filemanager.h"

#define ROOTDIR "./disks/"

NixFileManager::NixFileManager()
{
  numCached = 0;
}

NixFileManager::~NixFileManager()
{
}

int8_t NixFileManager::openFile(const char *name)
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

void NixFileManager::closeFile(int8_t fd)
{
  // invalid fd provided?
  if (fd < 0 || fd >= numCached)
    return;

  // clear the name
  cachedNames[fd][0] = '\0';
}

const char *NixFileManager::fileName(int8_t fd)
{
  if (fd < 0 || fd >= numCached)
    return NULL;

  return cachedNames[fd];
}

int8_t NixFileManager::readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen)
{
  int idx = 1;
  if (strcmp(where, ROOTDIR)) {
    // First entry is always "../"
    if (startIdx == 0) {
      strcpy(outputFN, "../");
      return 0;
    }
  } else {
    idx = 0; // we skipped ROOTDIR
  }

  DIR *dirp = opendir(where);
  if (!dirp)
    return -1;

  struct dirent *dp;

  outputFN[0] = '\0';

  while ((dp = readdir(dirp)) != NULL) {
    if (dp->d_name[0] == '.') {
      // Skip any dot files (and dot directories)
      continue;
    }

    // FIXME: skip any non-files and non-directories

    if (suffix && !(dp->d_type & DT_DIR) && strlen(dp->d_name) >= 3) {
      // It's a valid file. If it doesn't match any of our suffixes,
      // then skip it.
      char pat[40];
      strncpy(pat, suffix, sizeof(pat)); // make a working copy of the suffixes

      char *fsuff = &dp->d_name[strlen(dp->d_name)-3];

      if (strstr(pat, ",")) {
	// We have a list of suffixes. Check each of them.

	bool matchesAny = false;
	char *tok = strtok((char *)pat, ",");
	while (tok) {
	  // FIXME: assumes 3 character suffixes
	  if (!strncasecmp(fsuff, tok, 3)) {
	    matchesAny = true;
	    break;
	  }
	  
	  tok = strtok(NULL, ",");
	}

	if (!matchesAny) {
	  continue;
	}
      } else {
	// One single suffix - check it
	if (strcasecmp(fsuff, suffix)) {
	  continue;
	}
      }
    }
    // If we get here, it's something we want to show.
    if (idx == startIdx) {
      // Fill in the reply
      strncpy(outputFN, dp->d_name, maxlen-1);

      if (dp->d_type & DT_DIR) {
	// suffix
	strcat(outputFN, "/");
      }
      break;
    }

    // Next!
    idx++;
  }

  // Exited the loop - all done.
  closedir(dirp);

  if (!outputFN[0]) {
    // didn't find any more
    return -1;
  }

  return idx;
}

void NixFileManager::seekBlock(int8_t fd, uint16_t block, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return;

  if (isNib) {
    fileSeekPositions[fd] = block * 416;
  } else {
    fileSeekPositions[fd] = block * 256;
  }
}


bool NixFileManager::readTrack(int8_t fd, uint8_t *toWhere, bool isNib)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd != -1) {
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

bool NixFileManager::readBlock(int8_t fd, uint8_t *toWhere, bool isNib)
{
  // open, seek, read, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd != -1) {
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

bool NixFileManager::writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib)
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
  if (ffd != -1) {
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

bool NixFileManager::writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib)
{
  // open, seek, write, close.
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  int ffd = open(cachedNames[fd], O_WRONLY);
  if (ffd != -1) {
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

uint8_t NixFileManager::readByteAt(int8_t fd, uint32_t pos)
{
  if (fd < 0 || fd >= numCached)
    return -1; // FIXME: error handling?

  if (cachedNames[fd][0] == 0)
    return -1; // FIXME: error handling?

  uint8_t v = 0;

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd != -1) {
    lseek(ffd, pos, SEEK_SET);
    ret = (read(ffd, &v, 1) == 1);
    close(ffd);
  }

  if (!ret) {
    printf("ERROR reading byte at %u: %d\n", pos, errno);
  }

  // FIXME: error handling?
  return v;
}

bool NixFileManager::writeByteAt(int8_t fd, uint8_t v, uint32_t pos)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  // open, seek, write, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_WRONLY|O_CREAT, 0644);
  if (ffd != -1) {
    lseek(ffd, pos, SEEK_SET);
    ret = (write(ffd, &v, 1) == 1);
    close(ffd);
  }

  return ret;
}

bool NixFileManager::writeByte(int8_t fd, uint8_t v)
{
  if (fd < 0 || fd >= numCached)
    return false;

  if (cachedNames[fd][0] == 0)
    return false;

  uint32_t pos = fileSeekPositions[fd];

  // open, seek, write, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_WRONLY|O_CREAT, 0644);
  if (ffd != -1) {
    lseek(ffd, pos, SEEK_SET);
    ret = (write(ffd, &v, 1) == 1);
    if (!ret) {
      printf("error writing: %d\n", errno);
    }
    close(ffd);
  } else {
    printf("Failed to open '%s' for writing: %d\n", 
	   cachedNames[fd], errno);
  }
  fileSeekPositions[fd]++;
  return ret;
}

uint8_t NixFileManager::readByte(int8_t fd)
{
  if (fd < 0 || fd >= numCached)
    return -1; // FIXME: error handling?

  if (cachedNames[fd][0] == 0)
    return -1; // FIXME: error handling?

  uint8_t v = 0;

  uint32_t pos = fileSeekPositions[fd];

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd != -1) {
    lseek(ffd, pos, SEEK_SET);
    ret = (read(ffd, &v, 1) == 1);
    close(ffd);
  }
  fileSeekPositions[fd]++;

  if (!ret) {
    printf("ERROR reading from pos %d: %d\n", pos, errno);
  }

  // FIXME: error handling?
  return v;
}

void NixFileManager::getRootPath(char *toWhere, int8_t maxLen)
{
  strcpy(toWhere, ROOTDIR);
  //  strncpy(toWhere, ROOTDIR, maxLen);
}

bool NixFileManager::setSeekPosition(int8_t fd, uint32_t pos)
{
  // This could be a whole lot simpler.
  bool ret = false;
  FILE *f = fopen(cachedNames[fd], "r");
  if (f) {
    fseeko(f, 0, SEEK_END);
    fileSeekPositions[fd] = ftello(f);

    if (pos < ftello(f)) {
      fileSeekPositions[fd] = pos;
      ret = true;
    }
    fclose(f);
  }

  return ret;
};


void NixFileManager::seekToEnd(int8_t fd)
{
  // This could just be a stat call...
  FILE *f = fopen(cachedNames[fd], "r");
  if (f) {
    fseeko(f, 0, SEEK_END);
    fileSeekPositions[fd] = ftello(f);
    fclose(f);
  }
}
