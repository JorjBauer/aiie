#include "mockingboard.h"
#include <string.h>

Mockingboard::Mockingboard()
{
}

Mockingboard::~Mockingboard()
{
}

void Mockingboard::Reset()
{
}

uint8_t Mockingboard::readSwitches(uint8_t s)
{
  // There are never any reads to the I/O switches
  return 0xFF;
}

void Mockingboard::writeSwitches(uint8_t s, uint8_t v)
{
  // There are never any writes to the I/O switches
}

void Mockingboard::loadROM(uint8_t *toWhere)
{
  // We don't need a ROM; we're going to work via direct interaction
  // with memory 0xC400 - 0xC4FF
}

uint8_t Mockingboard::read(uint16_t address)
{
  address &= 0xFF;
  if ( (address >= 0x00 && 
	address <= 0x0F) ||
       (address >= 0x80 &&
	address <= 0x8F) ) {
    uint8_t idx = (address & 0x80 ? 1 : 0);
    if (idx == 0) { // FIXME: just debugging; remove this 'if'
      return sy6522[idx].read(address & 0x0F);
    }
  }

  return 0xFF;
}

void Mockingboard::write(uint16_t address, uint8_t val)
{
  address &= 0xFF;
  if ( (address >= 0x00 && 
	address <= 0x0F) ||
       (address >= 0x80 &&
	address <= 0x8F) ) {
    uint8_t idx = (address & 0x80 ? 1 : 0);
    if (idx == 0) { // FIXME: just debugging; remove this 'if'
      return sy6522[idx].write(address & 0x0F, val);
    }
  }
}
  
void Mockingboard::update(uint32_t cycles)
{
  sy6522[0].update(cycles);
  // debugging: disabled the second update for the moment
  //  sy6522[1].update(cycles);
}

