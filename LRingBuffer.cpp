#include "LRingBuffer.h"
#include <stdlib.h>
#include "globals.h"

#define RINGBUFFERMAGIC '0'

LRingBuffer::LRingBuffer(int16_t length)
{
  this->buffer = (uint8_t *)malloc(length);
  this->max = length;
  this->fill = 0;
  this->ptr = 0;
  this->cursor = 0;
}

LRingBuffer::~LRingBuffer()
{
  free (this->buffer);
}

bool LRingBuffer::Serialize(int8_t fd)
{
  g_filemanager->writeByte(fd, RINGBUFFERMAGIC);

  g_filemanager->writeByte(fd, (max >> 8) & 0xFF);
  g_filemanager->writeByte(fd, (max     ) & 0xFF);

  g_filemanager->writeByte(fd, (ptr >> 8) & 0xFF);
  g_filemanager->writeByte(fd, (ptr     ) & 0xFF);

  g_filemanager->writeByte(fd, (fill >> 8) & 0xFF);
  g_filemanager->writeByte(fd, (fill     ) & 0xFF);

  g_filemanager->writeByte(fd, (cursor >> 8) & 0xFF);
  g_filemanager->writeByte(fd, (cursor     ) & 0xFF);

  for (uint16_t i=0; i<max; i++) {
    g_filemanager->writeByte(fd, buffer[i]);
  }

  g_filemanager->writeByte(fd, RINGBUFFERMAGIC);
  
  return true;
}

bool LRingBuffer::Deserialize(int8_t fd)
{
  if (g_filemanager->readByte(fd) != RINGBUFFERMAGIC)
    return false;

  max = g_filemanager->readByte(fd);
  max <<= 8;
  max |= g_filemanager->readByte(fd);

  ptr = g_filemanager->readByte(fd);
  ptr <<= 8;
  ptr |= g_filemanager->readByte(fd);

  fill = g_filemanager->readByte(fd);
  fill <<= 8;
  fill |= g_filemanager->readByte(fd);

  cursor = g_filemanager->readByte(fd);
  cursor <<= 8;
  cursor |= g_filemanager->readByte(fd);

  if (buffer)
    free(buffer);

  buffer = (uint8_t *)malloc(max);
  
  for (uint16_t i=0; i<max; i++) {
    buffer[i] = g_filemanager->readByte(fd);
  }

  if (g_filemanager->readByte(fd) != RINGBUFFERMAGIC)
    return false;

  return true;
}

void LRingBuffer::clear()
{
  this->fill = 0;
}

bool LRingBuffer::isFull()
{
  return (this->max == this->fill);
}

bool LRingBuffer::hasData()
{
  return (this->fill != 0);
}

bool LRingBuffer::addByte(uint8_t b)
{
  if (this->max == this->fill)
    return false;

  int idx = (this->ptr + this->fill) % this->max;
  this->buffer[idx] = b;
  this->fill++;
  return true;
}

bool LRingBuffer::replaceByte(uint8_t b)
{
  if (cursor < fill) {
    buffer[cursor] = b;
    cursor++;
    if (cursor >= fill) {
      cursor = 0;
    }
    return true;
  }
  return false;
}


bool LRingBuffer::addBytes(uint8_t *b, int count)
{
  for (int i=0; i<count; i++) {
    if (!addByte(b[i]))
      return false;
  }
  return true;
}

uint8_t LRingBuffer::consumeByte()
{
  if (this->fill == 0)
    return 0;
  
  uint8_t ret = this->buffer[this->ptr];
  this->fill--;
  this->ptr++;
  this->ptr %= this->max;
  return ret;
}

uint8_t LRingBuffer::peek(int16_t idx)
{
  if (!this->fill)
    return 0; // No data in buffer; nothing to see

  uint16_t p = (this->ptr + idx) % this->fill;
  return this->buffer[p];
}

int16_t LRingBuffer::count()
{
  return this->fill;
}

uint16_t LRingBuffer::getPeekCursor()
{
  return this->cursor;
}

void LRingBuffer::setPeekCursor(int16_t idx)
{
  this->cursor = idx;
}

void LRingBuffer::resetPeekCursor()
{
  this->cursor = 0;
}

uint8_t LRingBuffer::peekNext()
{
  uint8_t ret = peek(cursor);
  cursor++;
  if (cursor >= fill) {
    cursor = 0;
  }
  return ret;
}
