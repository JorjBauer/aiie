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

int NixFileManager::write(int8_t fd, const void *buf, int nbyte)
{
  if (fd < 0 || fd >= numCached)
    return -1;

  if (cachedNames[fd][0] == 0)
    return -1;

  uint32_t pos = fileSeekPositions[fd];

  // open, seek, write, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_WRONLY|O_CREAT, 0644);
  if (ffd != -1) {
    if (lseek(ffd, pos, SEEK_SET) == -1) {
      close(ffd);
      return -1;
    }
    ret = write(ffd, buf, nbyte);
    if (ret != nbyte) {
      printf("error writing: %d\n", errno);
    }
    close(ffd);
  } else {
    printf("Failed to open '%s' for writing: %d\n", 
	   cachedNames[fd], errno);
  }
  fileSeekPositions[fd]+=nbyte;
  return ret;
};

int NixFileManager::read(int8_t fd, void *buf, int nbyte)
{
  if (fd < 0 || fd >= numCached)
    return -1; // FIXME: error handling?

  if (cachedNames[fd][0] == 0)
    return -1; // FIXME: error handling?

  uint32_t pos = fileSeekPositions[fd];

  // open, seek, read, close.
  bool ret = false;
  int ffd = open(cachedNames[fd], O_RDONLY);
  if (ffd != -1) {
    if (lseek(ffd, pos, SEEK_SET) == -1) {
      close(ffd);
      return -1;
    }
    ret = read(ffd, buf, nbyte);
    close(ffd);
  }
  fileSeekPositions[fd]+=nbyte;

  if (ret != nbyte) {
    printf("ERROR reading from pos %d: %d\n", pos, errno);
  }

  return ret;
};

int NixFileManager::lseek(int8_t fd, int offset, int whence)
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
