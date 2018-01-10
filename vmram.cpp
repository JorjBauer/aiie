#include "vmram.h"
#include <string.h>
#include "globals.h"

#ifndef TEENSYDUINO
#include <assert.h>
#else
#define assert(x)
#endif

// Serializing token for RAM data
#define RAMMAGIC 'R'

VMRam::VMRam() {memset(preallocatedRam, 0, sizeof(preallocatedRam)); }

VMRam::~VMRam() { }

void VMRam::init()
{
  for (uint32_t i=0; i<sizeof(preallocatedRam); i++) {
    preallocatedRam[i] = 0;
  }
}

uint8_t VMRam::readByte(uint32_t addr) { assert(addr < sizeof(preallocatedRam)); return preallocatedRam[addr]; }

void VMRam::writeByte(uint32_t addr, uint8_t value) { assert(addr < sizeof(preallocatedRam)); preallocatedRam[addr] = value; }

bool VMRam::Serialize(int8_t fd)
{
  g_filemanager->writeByte(fd, RAMMAGIC);
  uint32_t size = sizeof(preallocatedRam);
  g_filemanager->writeByte(fd, (size >> 24) & 0xFF);
  g_filemanager->writeByte(fd, (size >> 16) & 0xFF);
  g_filemanager->writeByte(fd, (size >>  8) & 0xFF);
  g_filemanager->writeByte(fd, (size      ) & 0xFF);

  for (uint32_t pos = 0; pos < sizeof(preallocatedRam); pos++) {
    g_filemanager->writeByte(fd, preallocatedRam[pos]);
  }

  g_filemanager->writeByte(fd, RAMMAGIC);

  return true;
}

bool VMRam::Deserialize(int8_t fd)
{
  if (g_filemanager->readByte(fd) != RAMMAGIC) {
    return false;
  }

  uint32_t size = 0;
  size = g_filemanager->readByte(fd);
  size <<= 8;
  size |= g_filemanager->readByte(fd);
  size <<= 8;
  size |= g_filemanager->readByte(fd);
  size <<= 8;
  size |= g_filemanager->readByte(fd);

  if (size != sizeof(preallocatedRam)) {
    return false;
  }

  for (uint32_t pos = 0; pos < sizeof(preallocatedRam); pos++) {
    preallocatedRam[pos] = g_filemanager->readByte(fd);
  }

  if (g_filemanager->readByte(fd) != RAMMAGIC) {
    return false;
  }

  return true;
}
