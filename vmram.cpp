#include "vmram.h"
#include <string.h>

#ifndef TEENSYDUINO
#include <assert.h>
#else
#define assert(x)
#endif

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
