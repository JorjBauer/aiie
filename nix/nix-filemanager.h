#ifndef __NIX_FILEMANAGER_H
#define __NIX_FILEMANAGER_H

#include "filemanager.h"
#include <stdint.h>

class NixFileManager : public FileManager {
 public:
  NixFileManager();
  virtual ~NixFileManager();

  virtual int8_t openFile(const char *name);
  virtual void closeFile(int8_t fd);

  virtual const char *fileName(int8_t fd);
  
  virtual int16_t readDir(const char *where, const char *suffix, char *outputFN, int16_t startIdx, uint16_t maxlen);
  virtual void closeDir();

  void getRootPath(char *toWhere, int8_t maxLen);

  virtual bool setSeekPosition(int8_t fd, uint32_t pos);
  virtual void seekToEnd(int8_t fd);
  
  virtual int write(int8_t fd, const void *buf, int nbyte);
  virtual int read(int8_t fd, void *buf, int nbyte);
  virtual int lseek(int8_t fd, int offset, int whence);

  virtual void flush();
 private:
  int8_t numCached;
  
};

#endif
