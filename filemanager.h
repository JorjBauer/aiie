#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H

#include <stdint.h>

#define MAXFILES 4    // how many results we can simultaneously manage
#define DIRPAGESIZE 10 // how many results in one readDir
#ifndef MAXPATH
  #define MAXPATH 255
#endif

#define FMMAGIC 'F'

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

class FileManager {
 public:
  virtual ~FileManager() {};

#define writeByte(fd,x) {static uint8_t c = x; write(outfd, &c, 1);}
  
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
    uint8_t b;
    if (read(infd, &b, 1) != 1)
      return -1;
    if (b != FMMAGIC)
      return -1;
    
    if (read(infd, &b, 1) != 1)
      return -1;
    
    if (b == 0) {
      // No file was cached. Verify footer and we're done without error

      if (read(infd, &b, 1) != 1)
	return -1;
      if (b != FMMAGIC) {
	// FIXME: no way to raise this error.
	return -1;
      }
      
      return -1;
    }
    
    char buf[MAXPATH];
    if (read(infd, &b, 1) != 1)
      return -1;    

    int8_t len = b;
    for (int i=0; i<len; i++) {
      if (read(infd, &buf[i], 1) != 1)
	return false;
    }
    buf[len] = '\0';
    
    int8_t ret = openFile(buf);
    if (ret == -1)
      return ret;
    
    if (read(infd, &b, 1) != 1)
      return -1;    
    fileSeekPositions[ret] <<= 8; fileSeekPositions[ret] |= b;
    if (read(infd, &b, 1) != 1)
      return -1;    
    fileSeekPositions[ret] <<= 8; fileSeekPositions[ret] |= b;
    if (read(infd, &b, 1) != 1)
      return -1;    
    fileSeekPositions[ret] <<= 8; fileSeekPositions[ret] |= b;
    if (read(infd, &b, 1) != 1)
      return -1;    
    fileSeekPositions[ret] <<= 8; fileSeekPositions[ret] |= b;

    if (read(infd, &b, 1) != 1)
      return -1;    
    if (b != FMMAGIC)
      return -1;
    
    return ret;
  }

  virtual int8_t openFile(const char *name) = 0;
  virtual void closeFile(int8_t fd) = 0;

  virtual const char *fileName(int8_t fd) = 0;

  virtual int8_t readDir(const char *where, const char *suffix, char *outputFN, int8_t startIdx, uint16_t maxlen) = 0;

  virtual void getRootPath(char *toWhere, int8_t maxLen) = 0;

  virtual uint32_t getSeekPosition(int8_t fd) {
    return fileSeekPositions[fd];
  };

  virtual bool setSeekPosition(int8_t fd, uint32_t pos) = 0;
  virtual void seekToEnd(int8_t fd) = 0;

  virtual int write(int8_t fd, const void *buf, int nbyte) = 0;
  virtual int read(int8_t fd, void *buf, int nbyte) = 0;
  virtual int lseek(int8_t fd, int offset, int whence) = 0;
  
 protected:
  unsigned long fileSeekPositions[MAXFILES];
  char cachedNames[MAXFILES][MAXPATH];

};

#undef writeByte

#endif
