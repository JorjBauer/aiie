#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H

#include <stdint.h>

#define MAXFILES 4    // how many results we can simultaneously manage
#define DIRPAGESIZE 10 // how many results in one readDir
#ifndef MAXPATH
  #define MAXPATH 255
#endif

class FileManager {
 public:
  virtual ~FileManager() {};

  virtual int8_t openFile(const char *name) = 0;
  virtual void closeFile(int8_t fd) = 0;

  virtual const char *fileName(int8_t fd) = 0;

  virtual int8_t readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen) = 0;
  virtual void seekBlock(int8_t fd, uint16_t block, bool isNib = false) = 0;
  virtual bool readTrack(int8_t fd, uint8_t *toWhere, bool isNib = false) = 0;
  virtual bool readBlock(int8_t fd, uint8_t *toWhere, bool isNib = false) = 0;
  virtual bool writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib = false) = 0;
  virtual bool writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib = false) = 0;

  virtual uint8_t readByteAt(int8_t fd, uint32_t pos) = 0;
  virtual bool writeByteAt(int8_t fd, uint8_t v, uint32_t pos) = 0;
};

#endif
