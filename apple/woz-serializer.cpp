#include "woz-serializer.h"
#include "globals.h"

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
  // FIXME: if trackDirty is set, we MUST flush first before exiting!
  
  g_filemanager->writeByte(fd, WOZMAGIC);

  // We need the internal state about data but not much else
  g_filemanager->writeByte(fd,
			   (trackPointer >> 24) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackPointer >> 16) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackPointer >>  8) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackPointer      ) & 0xFF);
  
  g_filemanager->writeByte(fd,
			   (trackBitCounter >> 24) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackBitCounter >> 16) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackBitCounter >>  8) & 0xFF);
  g_filemanager->writeByte(fd,
			   (trackBitCounter      ) & 0xFF);

  g_filemanager->writeByte(fd, trackByte);
  g_filemanager->writeByte(fd, trackBitIdx);
  g_filemanager->writeByte(fd, trackLoopCounter);
  
  g_filemanager->writeByte(fd, WOZMAGIC);
  return true;
}

bool WozSerializer::Deserialize(int8_t fd)
{
  // Before deserializing, the caller has to re-load the right disk image!
  if (g_filemanager->readByte(fd) != WOZMAGIC)
    return false;
  
  trackPointer = g_filemanager->readByte(fd);
  trackPointer <<= 8; trackPointer |= g_filemanager->readByte(fd);
  trackPointer <<= 8; trackPointer |= g_filemanager->readByte(fd);
  trackPointer <<= 8; trackPointer |= g_filemanager->readByte(fd);

  trackBitCounter = g_filemanager->readByte(fd);
  trackBitCounter <<= 8; trackBitCounter  |= g_filemanager->readByte(fd);
  trackBitCounter <<= 8; trackBitCounter  |= g_filemanager->readByte(fd);
  trackBitCounter <<= 8; trackBitCounter  |= g_filemanager->readByte(fd);
  
  trackByte = g_filemanager->readByte(fd);
  trackBitIdx = g_filemanager->readByte(fd);
  trackLoopCounter = g_filemanager->readByte(fd);
  
  if (g_filemanager->readByte(fd) != WOZMAGIC)
    return false;
  
  return true;
}

