#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H

#include <stdint.h>

#define MAXFILES 4    // how many results we can simultaneously manage
#define DIRPAGESIZE 10 // how many results in one readDir
#ifndef MAXPATH
  #define MAXPATH 255
#endif

#define FMMAGIC 'F'

class FileManager {
 public:
  virtual ~FileManager() {};

  virtual bool SerializeFile(int8_t outfd, int8_t fd) {
    writeByte(outfd, FMMAGIC);
    
    if (fd == -1 ||
	cachedNames[fd][0] == '\0') {
      
      // No file to cache; we're done
      writeByte(outfd, 0);
      writeByte(outfd, FMMAGIC);

      return true;
    }
    
    // have a file to cache; set a marker and continue
    writeByte(outfd, 1);

    int8_t l = 0;
    char *p = cachedNames[fd];
    while (*p) {
      l++;
      p++;
    }
    
    writeByte(outfd, l);
    for (int i=0; i<l; i++) {
      writeByte(outfd, cachedNames[fd][i]);
    }
    
    writeByte(outfd, (fileSeekPositions[fd] >> 24) & 0xFF);
    writeByte(outfd, (fileSeekPositions[fd] >> 16) & 0xFF);
    writeByte(outfd, (fileSeekPositions[fd] >>  8) & 0xFF);
    writeByte(outfd, (fileSeekPositions[fd]      ) & 0xFF);
    
    writeByte(outfd, FMMAGIC);

    return true;
  }

  virtual int8_t DeserializeFile(int8_t infd) {
    if (readByte(infd) != FMMAGIC)
      return -1;
    
    if (readByte(infd) == 0) {
      // No file was cached. Verify footer and we're done without error
      
      if (readByte(infd) != FMMAGIC) {
	// FIXME: no way to raise this error.
	return -1;
      }
      
      return -1;
    }
    
    char buf[MAXPATH];
    int8_t l = readByte(infd);
    for (int i=0; i<l; i++) {
      buf[i] = readByte(infd);
    }
    buf[l] = '\0';
    
    int8_t ret = openFile(buf);
    if (ret == -1)
      return ret;
    
    fileSeekPositions[ret] = readByte(infd);
    fileSeekPositions[ret] <<= 8;
    fileSeekPositions[ret] = readByte(infd);
    fileSeekPositions[ret] <<= 8;
    fileSeekPositions[ret] = readByte(infd);
    fileSeekPositions[ret] <<= 8;
    fileSeekPositions[ret] = readByte(infd);
    
    if (readByte(infd) != FMMAGIC)
      return -1;
    
    return ret;
  }

  virtual int8_t openFile(const char *name) = 0;
  virtual void closeFile(int8_t fd) = 0;

  virtual void truncate(int8_t fd) = 0;

  virtual const char *fileName(int8_t fd) = 0;

  virtual int8_t readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen) = 0;
  virtual void seekBlock(int8_t fd, uint16_t block, bool isNib = false) = 0;
  virtual bool readTrack(int8_t fd, uint8_t *toWhere, bool isNib = false) = 0;
  virtual bool readBlock(int8_t fd, uint8_t *toWhere, bool isNib = false) = 0;
  virtual bool writeBlock(int8_t fd, uint8_t *fromWhere, bool isNib = false) = 0;
  virtual bool writeTrack(int8_t fd, uint8_t *fromWhere, bool isNib = false) = 0;

  virtual uint8_t readByteAt(int8_t fd, uint32_t pos) = 0;
  virtual bool writeByteAt(int8_t fd, uint8_t v, uint32_t pos) = 0;
  virtual uint8_t readByte(int8_t fd) = 0;
  virtual bool writeByte(int8_t fd, uint8_t v) = 0;

  virtual void getRootPath(char *toWhere, int8_t maxLen) = 0;

  virtual uint32_t getSeekPosition(int8_t fd) {
    return fileSeekPositions[fd];
  };

  virtual bool setSeekPosition(int8_t fd, uint32_t pos) = 0;
  virtual void seekToEnd(int8_t fd) = 0;

 protected:
  unsigned long fileSeekPositions[MAXFILES];
  char cachedNames[MAXFILES][MAXPATH];

};

#endif
