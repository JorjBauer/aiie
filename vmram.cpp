#ifdef TEENSYDUINO
#include <Arduino.h>
#endif

#include "vmram.h"
#include <string.h>
#include "globals.h"

#ifndef TEENSYDUINO
#include <assert.h>
#else
#define assert(x) { if (!(x)) {Serial.print("assertion failed at "); Serial.println(__LINE__); delay(10000);} }
//#define assert(x) { }
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

uint8_t VMRam::readByte(uint32_t addr) 
{
  return preallocatedRam[addr]; 
}

void VMRam::writeByte(uint32_t addr, uint8_t value)
{ 
  preallocatedRam[addr] = value;
}

bool VMRam::Serialize(int8_t fd)
{
  uint32_t size = sizeof(preallocatedRam);
  uint8_t buf[5] = { RAMMAGIC,
		     (size >> 24) & 0xFF,
		     (size >> 16) & 0xFF,
		     (size >>  8) & 0xFF,
		     (size      ) & 0xFF };
  if (g_filemanager->write(fd, buf, 5) != 5)
    return false;

  if (g_filemanager->write(fd, preallocatedRam, sizeof(preallocatedRam)) != sizeof(preallocatedRam))
    return false;
  
  if (g_filemanager->write(fd, buf, 1) != 1)
    return false;

  return true;
}

bool VMRam::Deserialize(int8_t fd)
{
  uint8_t buf[5];
  if (g_filemanager->read(fd, buf, 5) != 5)
    return false;

  if (buf[0] != RAMMAGIC)
    return false;
  
  uint32_t size = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | buf[4];

  if (size != sizeof(preallocatedRam))
    return false;

  if (g_filemanager->read(fd, preallocatedRam, size) != size)
    return false;

  if (g_filemanager->read(fd, buf, 1) != 1)
    return false;
  if (buf[0] != RAMMAGIC)
    return false;

  return true;
}

bool VMRam::Test()
{
  return true;
}
