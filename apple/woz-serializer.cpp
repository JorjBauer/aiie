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
  /*
  serializeMagic(WOZMAGIC);

  imageType 8
    autoFlushTrackData bool
    diskinfo ??? can this be regen'd?
    trackInfo tracks[160] -- has a dirty flag in it :/
  
  serialize32(trackPointer);
  serialize32(trackBitCounter);
  serialize8(trackByte);
    trackByteFromDataTrack 8
  serialize8(trackBitIdx);
  serialize8(trackLoopCounter);

  metadata randData randPtr
  
  serializeMagic(WOZMAGIC);
  */
  return true;

 err:
  return false;
}

bool WozSerializer::Deserialize(int8_t fd)
{
  // Before deserializing, the caller has to re-load the right disk image!
  /*
  deserializeMagic(WOZMAGIC);
  deserialize32(trackPointer);
  deserialize32(trackBitCounter);
  deserialize32(lastReadPointer);
  deserialize8(trackByte);
  deserialize8(trackBitIdx);
  deserialize8(trackLoopCounter);

...
  have to serialize/deserialize all of tracks[*] now
    and the dirty flag is in there
      tracks[datatrack].dirty = true;
...

  
  deserializeMagic(WOZMAGIC);
  */
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


