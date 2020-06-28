#ifndef __TEENSYFILEMANAGER_H
#define __TEENSYFILEMANAGER_H

#include "filemanager.h"
#include <stdint.h>

class TeensyFileManager : public FileManager {
 public:
  TeensyFileManager();
  virtual ~TeensyFileManager();

  virtual int8_t openFile(const char *name);
  virtual void closeFile(int8_t fd);

  virtual const char *fileName(int8_t fd);

  virtual int8_t readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen);

  virtual void getRootPath(char *toWhere, int8_t maxLen);

  virtual bool setSeekPosition(int8_t fd, uint32_t pos);
  virtual void seekToEnd(int8_t fd);

  virtual int write(int8_t fd, const void *buf, int nbyte);
  virtual int read(int8_t fd, void *buf, int nbyte);
  virtual int lseek(int8_t fd, int offset, int whence);
  
 private:
  bool _prepCache(int8_t fd);

 private:
  bool enabled;

  int8_t numCached;
};

#endif
