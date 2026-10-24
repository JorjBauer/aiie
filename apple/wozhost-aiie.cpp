#include "wozhost-aiie.h"
#include "wozhost.h"
#include "wozmsg.h"
#include "globals.h"

#ifdef TEENSYDUINO
#include <Arduino.h>
#endif

// The file manager doesn't have open modes. It opens whatever it's
// given for reading and writing, and writes don't truncate. That's the
// same thing the library got from it before the hooks existed.
static int fmOpen(const char *path, wozOpenMode mode, void *ctx)
{
  (void)mode; (void)ctx;
  return g_filemanager->openFile(path);
}

static int fmClose(int fd, void *ctx)
{
  (void)ctx;
  g_filemanager->closeFile(fd);
  return 0;
}

static long fmRead(int fd, void *buf, size_t nbyte, void *ctx)
{
  (void)ctx;
  return g_filemanager->read(fd, buf, (int)nbyte);
}

static long fmWrite(int fd, const void *buf, size_t nbyte, void *ctx)
{
  (void)ctx;
  return g_filemanager->write(fd, buf, (int)nbyte);
}

static long fmSeek(int fd, long offset, int whence, void *ctx)
{
  (void)ctx;
  return g_filemanager->lseek(fd, (int)offset, whence);
}

#ifdef TEENSYDUINO
// Track data lives in the PSRAM on the Teensy; there isn't room for it
// in the internal RAM
static void *xMalloc(size_t size, void *ctx) { (void)ctx; return extmem_malloc(size); }
static void *xCalloc(size_t count, size_t size, void *ctx) { (void)ctx; return extmem_calloc(count, size); }
static void *xRealloc(void *ptr, size_t size, void *ctx) { (void)ctx; return extmem_realloc(ptr, size); }
static void xFree(void *ptr, void *ctx) { (void)ctx; extmem_free(ptr); }

static void serialSink(wozMsgLevel level, const char *msg, void *ctx)
{
  (void)level; (void)ctx;
  Serial.println(msg);
  Serial.flush();
  Serial.send_now();
}
#endif

void installWozHost()
{
  wozHost h = {};
  h.open = fmOpen;
  h.close = fmClose;
  h.read = fmRead;
  h.write = fmWrite;
  h.seek = fmSeek;
  // WozSerializer writes a dirty image back through the fd it was loaded
  // from (flush), and gets the disk's name from it (diskName), so it has
  // to stay open
  h.keepFilesOpen = true;
#ifdef TEENSYDUINO
  h.malloc = xMalloc;
  h.calloc = xCalloc;
  h.realloc = xRealloc;
  h.free = xFree;
  // Reading a whole WOZ back in off the SD card to checksum it takes too
  // long, and the spec allows a 0 CRC
  h.skipChecksum = true;
  wozSetMsgSink(serialSink, NULL);
#endif
  wozSetHost(&h);
}
