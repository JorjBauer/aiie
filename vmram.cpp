#ifdef TEENSYDUINO
#include <Arduino.h>
#include "teensy-println.h"
#endif

#include "vmram.h"
#include <string.h>
#include "serialize.h"
#include "globals.h"

#ifdef TEENSYDUINO
EXTMEM uint8_t preallocatedRam[599*256];
#else
#include <stdio.h>
uint8_t preallocatedRam[599*256];
#endif

#ifndef TEENSYDUINO
#include <assert.h>
#else
#define assert(x) { if (!(x)) {print("assertion failed at "); println(__LINE__); delay(10000);} }
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

uint8_t *VMRam::memPtr(uint32_t addr)
{
  printf("Asked for preallocated RAM pointer at 0x%X\n", addr);
  printf("Base is 0x%llX\n", (unsigned long long) preallocatedRam);
  return &preallocatedRam[addr];
}

bool VMRam::Serialize(int8_t fd)
{
  uint32_t size = sizeof(preallocatedRam);
  serializeMagic(RAMMAGIC);
  serialize32(size);

  if (g_filemanager->write(fd, preallocatedRam, sizeof(preallocatedRam)) != sizeof(preallocatedRam))
    goto err;

  serializeMagic(RAMMAGIC);

  return true;

 err:
  return false;
}

bool VMRam::Deserialize(int8_t fd)
{
  deserializeMagic(RAMMAGIC);
  uint32_t size;
  deserialize32(size);

  if (g_filemanager->read(fd, preallocatedRam, size) != size)
    goto err;

  deserializeMagic(RAMMAGIC);

  return true;

 err:
  return false;
}

bool VMRam::Test()
{
  return true;
}
