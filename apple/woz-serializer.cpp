#include "woz-serializer.h"
#include "globals.h"

#include "serialize.h"

#ifdef TEENSYDUINO
#include "iocompat.h"
#endif

#define WOZMAGIC 0xD5

WozSerializer::WozSerializer() : Woz(0,0)
{
}

const char *WozSerializer::diskName()
{
  if (fd != -1) {
    return g_filemanager->fileName(fd);
  }
  return "";
}

bool WozSerializer::Serialize(int8_t fd)
{
  // If we're being asked to serialize, make sure we've flushed any data first
  flush();

  serializeMagic(WOZMAGIC);
  serialize32(trackPointer);
  serialize32(trackBitCounter);
  serialize32(lastReadPointer);
  serialize8(trackByte);
  serialize8(trackBitIdx);
  serialize8(trackLoopCounter);
  serializeMagic(WOZMAGIC);
  
  return true;

 err:
  return false;
}

bool WozSerializer::Deserialize(int8_t fd)
{
  // Before deserializing, the caller has to re-load the right disk image!
  deserializeMagic(WOZMAGIC);
  deserialize32(trackPointer);
  deserialize32(trackBitCounter);
  deserialize32(lastReadPointer);
  deserialize8(trackByte);
  deserialize8(trackBitIdx);
  deserialize8(trackLoopCounter);
  deserializeMagic(WOZMAGIC);
  
  return true;

 err:
  return false;
}

