#include "woz-serializer.h"
#include "globals.h"

#include "serialize.h"

#ifdef TEENSYDUINO
#include "iocompat.h"
#endif

#define WOZMAGIC 0xAA

WozSerializer::WozSerializer() : Woz(false,0)
{
}

WozSerializer::~WozSerializer()
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
  // The disk image and its cache will be reloaded completely, so no
  // need to serialize tracks[] or diskinfo or imageType or metadata
  //
  // autoFlushTrackData is a configuration option, does not need
  
  serialize32(trackPointer);
  serialize32(trackBitCounter);
  serialize8(trackByte);
  serialize8(trackByteFromDataTrack);
  serialize8(trackBitIdx);
  serialize8(trackLoopCounter);
  serialize8(randData);
  serialize8(randPtr);
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
  deserialize8(trackByte);
  deserialize8(trackByteFromDataTrack);
  deserialize8(trackBitIdx);
  deserialize8(trackLoopCounter);
  deserialize8(randData);
  deserialize8(randPtr);
  
  deserializeMagic(WOZMAGIC);

  return true;

 err:
  return false;
}

bool WozSerializer::flush()
{
  // Flush the entire disk image if it's dirty. We could make this
  // smarter later.
  // FIXME hard-coded number of tracks?
  bool trackDirty = false;
  for (int i=0; i<160; i++) {
    if (tracks[i].dirty)
      trackDirty = true;
  }
  if (!trackDirty) {
    return true;
  }

  // The fd should still be open. If it's not, then we can't flush.
  if (fd == -1)
    return false;

  bool ret = true;

  switch (imageType) {
  case T_WOZ:
    ret = writeWozFile(fd, imageType);
    break;
  case T_DSK:
  case T_PO:
    ret = writeDskFile(fd, imageType);
    break;
  case T_NIB:
    ret = writeNibFile(fd);
    break;
    default:
      fprintf(stderr, "Error: unknown imageType; can't flush\n");
      ret = false;
      break;
  }
  g_filemanager->flush();

  return true;
}

bool WozSerializer::writeNextWozBit(uint8_t datatrack, uint8_t bit)
{
  return Woz::writeNextWozBit(datatrack, bit);
}

bool WozSerializer::writeNextWozByte(uint8_t datatrack, uint8_t b)
{
  return Woz::writeNextWozByte(datatrack, b);
}

uint8_t WozSerializer::nextDiskBit(uint8_t datatrack)
{
  return Woz::nextDiskBit(datatrack);
}

uint8_t WozSerializer::nextDiskByte(uint8_t datatrack)
{
  return Woz::nextDiskByte(datatrack);
}


