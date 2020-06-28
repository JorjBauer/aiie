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

  virtual void truncate(int8_t fd);

  virtual const char *fileName(int8_t fd);

  virtual int8_t readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen);
  virtual void seekBlock(int8_t fd, uint16_t block, bool isNib = false);
  virtual bool readTrack(int8_t fd, uint8_t *toWhere, bool isNib = false);
  virtual bool readBlock(int8_t fd, uint8_t *toWhere, bool isNib = false);
  virtual bool writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib = false);
  virtual bool writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib = false);

  virtual uint8_t readByteAt(int8_t fd, uint32_t pos);
  virtual bool writeByteAt(int8_t fd, uint8_t v, uint32_t pos);

  virtual uint8_t readByte(int8_t fd);
  virtual bool writeByte(int8_t fd, uint8_t v);

  virtual void getRootPath(char *toWhere, int8_t maxLen);

  virtual bool setSeekPosition(int8_t fd, uint32_t pos);
  virtual void seekToEnd(int8_t fd);
  
 private:
  bool _prepCache(int8_t fd);

 private:
  bool enabled;

  int8_t numCached;
};

#endif
