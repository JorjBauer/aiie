#include "woz.h"
#include <string.h>
#include "crc32.h"
#include "nibutil.h"
#include "version.h"
#include "globals.h"

// Block number we start packing data bits after (Woz 2.0 images)
#define STARTBLOCK 3

#define PREP_SECTION(f, t) {      \
  uint32_t type = t;              \
  if (!write32(f, type))          \
    return false;                 \
  if (!write32(f, 0))             \
    return false;                 \
  curpos = g_filemanager->getSeekPosition(f);    \
 }

#define END_SECTION(f) {                    \
  uint32_t endpos = g_filemanager->getSeekPosition(f); \
  g_filemanager->setSeekPosition(f, curpos-4); \
  uint32_t chunksize = endpos - curpos;     \
  if (!write32(f, chunksize))               \
    return false;                           \
  g_filemanager->seekToEnd(f);              \
  }

Woz::Woz()
{
  trackPointer = 0;
  trackBitIdx = 0x80;
  trackBitCounter = 0;
  trackLoopCounter = 0;
  metaData = NULL;

  fh = -1;
  autoFlushTrackData = false;

  memset(&quarterTrackMap, 255, sizeof(quarterTrackMap));
  memset(&di, 0, sizeof(diskInfo));
  memset(&tracks, 0, sizeof(tracks));
  randPtr = 0;
}

Woz::~Woz()
{
  if (fh != -1) {
    g_filemanager->closeFile(fh);
    fh = -1;
  }

  for (int i=0; i<160; i++) {
    if (tracks[i].trackData) {
      free(tracks[i].trackData);
      tracks[i].trackData = NULL;
    }
  }

  if (metaData) {
    free(metaData);
    metaData = NULL;
  }
}


uint8_t Woz::getNextWozBit(uint8_t track)
{
  if (trackBitIdx == 0x80) {
    if (!tracks[track].trackData) {
      readAndDecodeTrack(track, fh);
    }
    // need another byte out of the track stream
    trackByte = tracks[track].trackData[trackPointer++];
  }

  if (trackBitCounter >= tracks[track].bitCount) {
    trackPointer = 0;
    trackBitIdx = 0x80;
    trackLoopCounter++;
    trackByte = tracks[track].trackData[trackPointer++];
    trackBitCounter = 0;
  }
  trackBitCounter++;

  uint8_t ret = (trackByte & trackBitIdx) ? 1 : 0;

  trackBitIdx >>= 1;
  if (!trackBitIdx) {
    trackBitIdx = 0x80;
  }

  return ret;
}

uint8_t Woz::fakeBit()
{
  // 30% should be 1s, but I'm not biasing the data here, so this is
  // more like 50% 1s.

  if (randPtr == 0) {
    randPtr = 0x80;
    randData = (uint8_t) ((float)256*rand()/(RAND_MAX+1.0));
  }

  uint8_t ret = (randData & randPtr) ? 1 : 0;
  randPtr >>= 1;

  return ret;
}

uint8_t Woz::nextDiskBit(uint8_t track)
{
  if (track == 0xFF)
    return fakeBit();

  static uint8_t head_window = 0;
  head_window <<= 1;
  head_window |= getNextWozBit(track);
  if ((head_window & 0x0f) != 0x00) {
    return (head_window & 0x02) >> 1;
  } else {
    return fakeBit();
  }
}

uint8_t Woz::nextDiskByte(uint8_t track)
{
  uint8_t d = 0;
  while ((d & 0x80) == 0) {
    d <<= 1;
    d |= nextDiskBit(track);
  }
  return d;
}

static bool write8(uint8_t fh, uint8_t v)
{
  if (!g_filemanager->writeByte(fh, v))
    return false;
  return true;
}

static bool write16(uint8_t fh, uint16_t v)
{
  if (!write8(fh, v & 0xFF))
    return false;
  v >>= 8;
  if (!write8(fh, v & 0xFF))
    return false;
  return true;
}

static bool write32(uint8_t fh, uint32_t v)
{
  for (int i=0; i<4; i++) {
    if (!write8(fh, v&0xFF))
      return false;
    v >>= 8;
  }
  return true;
}

static bool read8(uint8_t fd, uint8_t *toWhere)
{
  // FIXME: no error checking
  *toWhere = g_filemanager->readByte(fd);

  return true;
}

static bool read16(uint8_t fh, uint16_t *toWhere)
{
  uint16_t ret = 0;
  for (int i=0; i<2; i++) {
    uint8_t r;
    if (!read8(fh, &r)) {
      return false;
    }
    ret >>= 8;
    ret |= (r<<8);
  }

  *toWhere = ret;

  return true;
}

static bool read32(uint8_t fh, uint32_t *toWhere)
{
  uint32_t ret = 0;
  for (int i=0; i<4; i++) {
    uint8_t r;
    if (!read8(fh, &r)) {
      return false;
    }
    ret >>= 8;
    ret |= (r<<24);
  }

  *toWhere = ret;

  return true;
}

bool Woz::writeFile(uint8_t version, const char *filename)
{
  int8_t fh = -1; // filehandle (-1 == closed)
  bool retval = false;
  uint32_t tmp32; // scratch 32-bit value
  off_t crcPos, endPos;
  off_t curpos; // used in macros to dynamically tell what size the chunks are
  uint32_t crcDataSize;
  uint8_t *crcData = NULL;


  if (version > 2 || !version) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: version must be 1 or 2\n");
#endif
    goto done;
  }

  fh = g_filemanager->openFile(filename);
  if (fh==-1) {
#ifndef TEENSYDUINO
    printf("ERROR: Unable to open output file\n");
#endif
    goto done;
  }
  
  // header
  if (version == 1) {
    tmp32 = 0x315A4F57;
  } else {
    tmp32 = 0x325A4F57;
  }
  if (!write32(fh, tmp32)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write\n");
#endif
    goto done;
  }
  tmp32 = 0x0A0D0AFF;
  if (!write32(fh, tmp32)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write\n");
#endif
    goto done;
  }

  // We'll come back and write the checksum later
  crcPos = g_filemanager->getSeekPosition(fh);
  tmp32 = 0;
  if (!write32(fh, tmp32)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write\n");
#endif
    goto done;
  }

  PREP_SECTION(fh, 0x4F464E49); // 'INFO'
  if (!writeInfoChunk(version, fh)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write INFO chunk\n");
#endif
    goto done;
  }
  END_SECTION(fh);

  PREP_SECTION(fh, 0x50414D54); // 'TMAP'
  if (!writeTMAPChunk(version, fh)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write TMAP chunk\n");
#endif
    goto done;
  }
  END_SECTION(fh);

  PREP_SECTION(fh, 0x534B5254); // 'TRKS'
  if (!writeTRKSChunk(version, fh)) {
    fprintf(stderr, "ERROR: failed to write TRKS chunk\n");
    goto done;
  }
  END_SECTION(fh);

  // Write the metadata if we have any
  if (metaData) {
    PREP_SECTION(fh, 0x4154454D); // 'META'
    for (int i=0; i<strlen(metaData); i++) {
      if (!write8(fh, metaData[i])) {
#ifndef TEENSYDUINO
	fprintf(stderr, "ERROR: failed to write META chunk\n");
#endif
	goto done;
      }
    }
    END_SECTION(fh);
  }

  // FIXME: missing the WRIT chunk, if it exists

  // Fix up the checksum
  endPos = g_filemanager->getSeekPosition(fh);
  crcDataSize = endPos-crcPos-4;
  crcData = (uint8_t *)malloc(crcDataSize);
  if (!crcData) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to malloc crc data chunk\n");
#endif
    goto done;
  }
    
  // Read the data in for checksumming
  // FIXME: no error checking on seek
  g_filemanager->setSeekPosition(fh, crcPos+4);

  for (int i=0; i<crcDataSize; i++) {
    if (!read8(fh, &crcData[i])) {
#ifndef TEENSYDUINO
      fprintf(stderr, "ERROR: failed to read in data for checksum [read %d, wanted %d]\n", tmp32, crcDataSize);
#endif
      goto done;
    }
  }
    
  tmp32 = compute_crc_32(crcData, crcDataSize);
  // Write it back out
  g_filemanager->setSeekPosition(fh, crcPos);
  if (!write32(fh, tmp32)) {
#ifndef TEENSYDUINO
    fprintf(stderr, "ERROR: failed to write CRC\n");
#endif
    goto done;
  }

  retval = true;

 done:
  if (crcData)
    free(crcData);
  if (fh != -1) {
    g_filemanager->closeFile(fh);
  }
  return retval;
}

void Woz::_initInfo()
{
  di.version = 2;
  di.diskType = 1;
  di.writeProtected = 0;
  di.synchronized = 0;
  di.cleaned = 0;
  sprintf(di.creator, "%.32s", VERSION_STRING);
  di.diskSides = 1;
  di.bootSectorFormat = 0;
  di.optimalBitTiming = 32;
  di.compatHardware = 0;
  di.requiredRam = 0;
  di.largestTrack = 13;

  // reset all the track data
  for (int i=0; i<160; i++) {
    memset(&tracks[i], 0, sizeof(trackInfo));
  }
  // Construct a default quarter-track mapping
  for (int i=0; i<140; i++) {
    if ((i+1)/4 < 35) {
      quarterTrackMap[i] = ((i-2) % 4 == 0) ? 0xFF : ((i+1)/4);
    } else {
      quarterTrackMap[i] = 0xFF;
    }
  }
}

bool Woz::readAndDecodeTrack(uint8_t track, int8_t fh)
{
  // If we're going to malloc a new one, then find all the other ones
  // that might be malloc'd and purge them if we're autoFlushTrackData==true
  if (autoFlushTrackData == true) {
    for (int i=0; i<160; i++) {
      if (tracks[i].trackData) {
	free(tracks[i].trackData);
	tracks[i].trackData = NULL;
      }
    }
  }


  if (imageType == T_WOZ) {
    return readWozTrackData(fh, track);
  } else if (imageType == T_PO || 
      imageType == T_DSK) {
    static uint8_t sectorData[256*16];

    g_filemanager->setSeekPosition(fh, 256*16*track);
    
    for (int i=0; i<256*16; i++) {
      // FIXME: no error checking
      sectorData[i] = g_filemanager->readByte(fh);
    }
    
    tracks[track].trackData = (uint8_t *)calloc(NIBTRACKSIZE, 1);
    if (!tracks[track].trackData) {
#ifndef TEENSYDUINO
      fprintf(stderr, "Failed to malloc track data\n");
#endif
      return false;
    }
    tracks[track].startingBlock = STARTBLOCK + 13*track;
    tracks[track].blockCount = 13;
    uint32_t sizeInBits = nibblizeTrack(tracks[track].trackData, sectorData, imageType, track);
    tracks[track].bitCount = sizeInBits; // ... reality.

    return true;
  }
  else if (imageType == T_NIB) {
    tracks[track].trackData = (uint8_t *)malloc(NIBTRACKSIZE);
    if (!tracks[track].trackData) {
#ifndef TEENSYDUINO
      printf("Failed to malloc track data\n");
#endif
      return false;
    }

    g_filemanager->setSeekPosition(fh, NIBTRACKSIZE * track);
    for (int i=0; i<NIBTRACKSIZE; i++) {
      // FIXME: no error checking
      tracks[track].trackData[i] = g_filemanager->readByte(fh);
    }

    tracks[track].startingBlock = STARTBLOCK + 13*track;
    tracks[track].blockCount = 13;
    tracks[track].bitCount = NIBTRACKSIZE*8;

    return true;
  }

#ifndef TEENSYDUINO
  printf("ERROR: don't know how we reached this point\n");
#endif
  return false;
}

bool Woz::readDskFile(const char *filename, bool preloadTracks, uint8_t subtype)
{
  bool retval = false;
  autoFlushTrackData = !preloadTracks;

  imageType = subtype;

  fh = g_filemanager->openFile(filename);
  if (fh == -1) {
#ifndef TEENSYDUINO
    perror("Unable to open input file");
#endif
    goto done;
  }

  _initInfo();

  // Now read in the 35 tracks of data from the DSK file and convert them to NIB
  if (preloadTracks) {
    for (int track=0; track<35; track++) {
      if (!readAndDecodeTrack(track, fh)) {
	goto done;
      }
    }
  }      

  retval = true;

 done:
  return retval;
}

bool Woz::readNibFile(const char *filename, bool preloadTracks)
{
  bool ret = false;
  autoFlushTrackData = !preloadTracks;

  imageType = T_NIB;

  fh = g_filemanager->openFile(filename);
  if (fh == -1) {
#ifndef TEENSYDUINO
    perror("Unable to open input file");
#endif
    goto done;
  }
  
  _initInfo();

  if (preloadTracks) {
    for (int track=0; track<35; track++) {
      if (!readAndDecodeTrack(track, fh)) {
	goto done;
      }
    }
  }
  ret = true;

 done:
  return ret;
}

bool Woz::readWozFile(const char *filename, bool preloadTracks)
{
  imageType = T_WOZ;
  autoFlushTrackData = !preloadTracks;

  fh = g_filemanager->openFile(filename);
  if (fh == -1) {
#ifndef TEENSYDUINO
    perror("Unable to open input file");
#endif
    return false;
  }

  // Header
  uint32_t h;
  read32(fh, &h);
  if (h == 0x325A4F57 || h == 0x315A4F57) {
#ifndef TEENSYDUINO
    printf("WOZ%c disk image\n", (h & 0xFF000000)>>24);
#endif
  } else {
#ifndef TEENSYDUINO
    printf("Unknown disk image type; can't continue\n");
#endif
    return false;
  }

  uint32_t tmp;
  if (!read32(fh, &tmp)) {
#ifndef TEENSYDUINO
    printf("Read failure\n");
#endif
    return false;
  }
  if (tmp != 0x0A0D0AFF) {
#ifndef TEENSYDUINO
    printf("WOZ header failure; exiting\n");
#endif
    return false;
  }
  uint32_t crc32;
  read32(fh, &crc32);
  //  printf("Disk crc32 should be 0x%X\n", crc32);
  // FIXME: check CRC. Remember that 0x00 means "don't check CRC"
  
  uint32_t fpos = 12;
  uint8_t haveData = 0;

#define cINFO 1
#define cTMAP 2
#define cTRKS 4

  while (1) {
    if (!g_filemanager->setSeekPosition(fh, fpos))
      break;

    uint32_t chunkType;
    if (!read32(fh, &chunkType)) {
      printf("Failed to read chunktype; breaking from loop\n");
      break;
    }
    uint32_t chunkDataSize;
    read32(fh, &chunkDataSize);

    bool isOk;
    printf("reading chunk type 0x%X\n", chunkType);
    switch (chunkType) {
    case 0x4F464E49: // 'INFO'
      isOk = parseInfoChunk(fh, chunkDataSize);
      haveData |= cINFO;
      break;
    case 0x50414D54: // 'TMAP'
      isOk = parseTMAPChunk(fh, chunkDataSize);
      haveData |= cTMAP;
      break;
    case 0x534B5254: // 'TRKS'
      isOk = parseTRKSChunk(fh, chunkDataSize);
      haveData |= cTRKS;
      break;
    case 0x4154454D: // 'META'
      isOk = parseMetaChunk(fh, chunkDataSize);
      break;
    default:
#ifndef TEENSYDUINO
      printf("Unknown chunk type 0x%X; failed to read woz file\n", chunkType);
#endif
      return false;
    }

    if (!isOk) {
#ifndef TEENSYDUINO
      printf("Chunk parsing [0x%X] failed; exiting\n", chunkType);
#endif
      return false;
    }
    
    fpos += chunkDataSize + 8; // 8 bytes for the ChunkID and the ChunkSize
  }

  if (haveData != 0x07) {
#ifndef TEENSYDUINO
    printf("ERROR: missing one or more critical sections\n");
#endif
    return false;
  }

  if (preloadTracks) {
    for (int i=0; i<40*4; i++) {
      if (!readQuarterTrackData(fh, i)) {
#ifndef TEENSYDUINO
	printf("Failed to read QTD for track %d\n", i);
#endif
	return false;
      }
    }
  }

#ifndef TEENSYDUINO
  printf("File read successful\n");
#endif
  return true;
}

bool Woz::readFile(const char *filename, bool preloadTracks, uint8_t forceType)
{
  if (forceType == T_AUTO) {
    // Try to determine type from the file extension
    const char *p = strrchr(filename, '.');
    if (!p) {
#ifndef TEENSYDUINO
      printf("Unable to determine file type of '%s'\n", filename);
#endif
      return false;
    }
    if (strcasecmp(p, ".woz") == 0) {
      forceType = T_WOZ;
    } else if (strcasecmp(p, ".dsk") == 0 ||
	       strcasecmp(p, ".do") == 0) {
      forceType = T_DSK;
    } else if (strcasecmp(p, ".po")  == 0) {
      forceType = T_PO;
    } else if (strcasecmp(p, ".nib") == 0) {
      forceType = T_NIB;
    } else {
#ifndef TEENSYDUINO
      printf("Unable to determine file type of '%s'\n", filename);
#endif
      return false;
    }
  }

  switch (forceType) {
  case T_WOZ:
#ifndef TEENSYDUINO
    printf("reading woz file %s\n", filename);
#endif
    return readWozFile(filename, preloadTracks);
  case T_DSK:
  case T_PO:
#ifndef TEENSYDUINO
    printf("reading DSK file %s\n", filename);
#endif
    return readDskFile(filename, preloadTracks, forceType);
  case T_NIB:
#ifndef TEENSYDUINO
    printf("reading NIB file %s\n", filename);
#endif
    return readNibFile(filename, preloadTracks);
  default:
#ifndef TEENSYDUINO
    printf("Unknown disk type; unable to read\n");
#endif
    return false;
  }
}

bool Woz::parseTRKSChunk(int8_t fh, uint32_t chunkSize)
{
  if (di.version == 2) {
    printf("v2 parse\n");
    for (int i=0; i<160; i++) {
      if (!read16(fh, &tracks[i].startingBlock))
	return false;
      if (!read16(fh, &tracks[i].blockCount))
	return false;
      if (!read32(fh, &tracks[i].bitCount))
	return false;
      tracks[i].startingByte = 0; // v1-specific
    }
    return true;
  }

  printf("v1 parse\n");
  // V1 parsing
  uint32_t ptr = 0;
  uint8_t trackNumber = 0;
  while (ptr < chunkSize) {
    tracks[trackNumber].startingByte = trackNumber * 6656 + 256;
    tracks[trackNumber].startingBlock = 0; // v2-specific
    tracks[trackNumber].blockCount = 13;
    g_filemanager->setSeekPosition(fh, (trackNumber * 6656 + 256) + 6648); // FIXME: no error checking
    uint16_t numBits;
    if (!read16(fh, &numBits)) {
      return false;
    }
    tracks[trackNumber].bitCount = numBits;
    ptr += 6656;
    trackNumber++;
  }

  return true;
}

bool Woz::parseTMAPChunk(int8_t fh, uint32_t chunkSize)
{
  if (chunkSize != 0xa0) {
#ifndef TEENSYDUINO
    printf("TMAP chunk is the wrong size; aborting\n");
#endif
    return false;
  }

  for (int i=0; i<40*4; i++) {
    if (!read8(fh, (uint8_t *)&quarterTrackMap[i]))
      return false;
    chunkSize--;
  }

  return true;
}

// return true if successful
bool Woz::parseInfoChunk(int8_t fh, uint32_t chunkSize)
{
  if (chunkSize != 60) {
#ifndef TEENSYDUINO
    printf("INFO chunk size is not 60; aborting\n");
#endif
    return false;
  }

  if (!read8(fh, &di.version))
    return false;
  if (di.version > 2) {
#ifndef TEENSYDUINO
    printf("Incorrect version header; aborting\n");
#endif
    return false;
  }

  if (!read8(fh, &di.diskType))
    return false;
  if (di.diskType != 1) {
#ifndef TEENSYDUINO
    printf("Not a 5.25\" disk image; aborting\n");
#endif
    return false;
  }

  if (!read8(fh, &di.writeProtected))
    return false;

  if (!read8(fh, &di.synchronized))
    return false;

  if (!read8(fh, &di.cleaned))
    return false;

  di.creator[32] = 0;
  for (int i=0; i<32; i++) {
    if (!read8(fh, (uint8_t *)&di.creator[i]))
      return false;
  }

  if (di.version >= 2) {
    if (!read8(fh, &di.diskSides))
      return false;
    if (!read8(fh, &di.bootSectorFormat))
      return false;
    if (!read8(fh, &di.optimalBitTiming))
      return false;
    if (!read16(fh, &di.compatHardware))
      return false;
    if (!read16(fh, &di.requiredRam))
      return false;
    if (!read16(fh, &di.largestTrack))
      return false;
  } else {
    di.diskSides = 0;
    di.bootSectorFormat = 0;
    di.compatHardware = 0;
    di.requiredRam = 0;
    di.largestTrack = 13; // 13 * 512 bytes = 6656. All tracks are
			  // padded to 6646 bytes in the v1 image.
    di.optimalBitTiming = 32; // "standard" disk bit timing for a 5.25" disk (4us per bit)
  }

  return true;
}

bool Woz::parseMetaChunk(int8_t fh, uint32_t chunkSize)
{
  metaData = (char *)calloc(chunkSize+1, 1);
  if (!metaData)
    return false;

  for (int i=0; i<chunkSize; i++) {
    metaData[i] = g_filemanager->readByte(fh); // FIXME: no error checking
  }

  metaData[chunkSize] = 0;

  return true;
}

bool Woz::readWozTrackData(int8_t fh, uint8_t wt)
{
  // assume if it's malloc'd, then we've already read it
  if (tracks[wt].trackData)
    return true;

  uint16_t bitsStartBlock = tracks[wt].startingBlock;

  //  if (tracks[targetImageTrack].trackData)
  //    free(tracks[targetImageTrack].trackData);
  
  // Allocate a new buffer for this track
  uint32_t count = tracks[wt].blockCount * 512;
  if (di.version == 1) count = (tracks[wt].bitCount / 8) + ((tracks[wt].bitCount % 8) ? 1 : 0);
  tracks[wt].trackData = (uint8_t *)calloc(count, 1);
  if (!tracks[wt].trackData) {
#ifndef TEENSYDUINO
    perror("Failed to alloc buf to read track magnetic data");
#endif
    return false;
  }

  if (di.version == 1) {
    g_filemanager->setSeekPosition(fh, tracks[wt].startingByte); // FIXME: no error checking
  } else {
    g_filemanager->setSeekPosition(fh, bitsStartBlock*512); // FIXME: no error checking
  }
  for (int i=0; i<count; i++) {
    // FIXME: no error checking
    tracks[wt].trackData[i] = g_filemanager->readByte(fh);
  }

  return true;
}

bool Woz::readQuarterTrackData(int8_t fh, uint8_t quartertrack)
{
  uint8_t targetImageTrack = quarterTrackMap[quartertrack];
  if (targetImageTrack == 0xFF) {
    // It's a tween-track with no reliable data.
    return true;
  }

  return readWozTrackData(fh, targetImageTrack);
}


bool Woz::readSectorData(uint8_t track, uint8_t sector, nibSector *sectorData)
{
  // Find the sector header for this sector...
  uint32_t ptr = 0;

  memset(sectorData->gap1, 0xFF, sizeof(sectorData->gap1));
  memset(sectorData->gap2, 0xFF, sizeof(sectorData->gap1));

  // Allow two loops through the track data looking for the sector prolog
  uint32_t endCount = tracks[track].blockCount*512*2;
  if (di.version == 1) endCount = 2*6646;
  while (ptr < endCount) {
    sectorData->sectorProlog[0] = sectorData->sectorProlog[1];
    sectorData->sectorProlog[1] = sectorData->sectorProlog[2];
    sectorData->sectorProlog[2] = nextDiskByte(track);
    ptr++;
    
    if (sectorData->sectorProlog[0] == 0xd5 &&
	sectorData->sectorProlog[1] == 0xaa &&
	sectorData->sectorProlog[2] == 0x96) {
      // Found *a* sector header. See if it's ours.
      sectorData->volume44[0] = nextDiskByte(track);
      sectorData->volume44[1] = nextDiskByte(track);
      sectorData->track44[0] = nextDiskByte(track);
      sectorData->track44[1] = nextDiskByte(track);
      sectorData->sector44[0] = nextDiskByte(track);
      sectorData->sector44[1] = nextDiskByte(track);
      sectorData->checksum44[0] = nextDiskByte(track);
      sectorData->checksum44[1] = nextDiskByte(track);
      sectorData->sectorEpilog[0] = nextDiskByte(track);
      sectorData->sectorEpilog[1] = nextDiskByte(track);
      sectorData->sectorEpilog[2] = nextDiskByte(track);

      if (sectorData->sectorEpilog[0] == 0xde &&
	  sectorData->sectorEpilog[1] == 0xaa &&
	  sectorData->sectorEpilog[2] == 0xeb) {
	// Header is integral. See if it's our sector:
	uint8_t sectorNum = de44(sectorData->sector44);
	if (sectorNum != sector) {
	  continue;
	}
	// It's our sector - find the data chunk and read it
	while (ptr < tracks[track].blockCount*512*2) {
	  sectorData->dataProlog[0] = sectorData->dataProlog[1];
	  sectorData->dataProlog[1] = sectorData->dataProlog[2];
	  sectorData->dataProlog[2] = nextDiskByte(track);
	  ptr++;

	  if (sectorData->dataProlog[0] == 0xd5 &&
	      sectorData->dataProlog[1] == 0xaa &&
	      sectorData->dataProlog[2] == 0xad) {
	    // Found the data; copy it in
	    for (int i=0; i<342; i++) {
	      sectorData->data62[i] = nextDiskByte(track);
	    }
	    sectorData->checksum = nextDiskByte(track);
	    sectorData->dataEpilog[0] = nextDiskByte(track);
	    sectorData->dataEpilog[1] = nextDiskByte(track);
	    sectorData->dataEpilog[2] = nextDiskByte(track);
	    if (sectorData->dataEpilog[0] != 0xde ||
		sectorData->dataEpilog[1] != 0xaa ||
		sectorData->dataEpilog[2] != 0xeb) {
	      continue;
	    }
	    // Have an integral hunk of data, with epilog - return it
	    return true;
	  }
	}
      }
    }
  }

  return false;
}

bool Woz::writeInfoChunk(uint8_t version, int8_t fh)
{
  if (!write8(fh, version) ||
      !write8(fh, di.diskType) ||
      !write8(fh, di.writeProtected) ||
      !write8(fh, di.synchronized) ||
      !write8(fh, di.cleaned))
    return false;

  for (int i=0; i<32; i++) {
    if (!write8(fh, di.creator[i]))
      return false;
  }
  
  if (version >= 2) {
    // If we read a Wozv1, this will be set to 0. Set it to 1.
    if (di.diskSides == 0)
      di.diskSides = 1;

    if ( !write8(fh, di.diskSides) ||
	 !write8(fh, di.bootSectorFormat) ||
	 !write8(fh, di.optimalBitTiming) ||
	 !write16(fh, di.compatHardware) ||
	 !write16(fh, di.requiredRam) ||
	 !write16(fh, di.largestTrack))
      return false;
  }

  // Padding
  for (int i=0; i<((version==1)?23:14); i++) {
    if (!write8(fh, 0))
      return false;
  }
  return true;
}

bool Woz::writeTMAPChunk(uint8_t version, int8_t fh)
{
  for (int i=0; i<40*4; i++) {
    if (!write8(fh, quarterTrackMap[i]))
      return false;
  }

  return true;
}

bool Woz::writeTRKSChunk(uint8_t version, int8_t fh)
{
  if (version == 1) {
#ifndef TEENSYDUINO
    printf("V1 write is not implemented\n");
#endif
    return false;
  }

  // Reconstruct all of the starting blocks/blockCounts for each
  // track. The bitCount should be correct.
  uint8_t numTracksPacked = 0;
  for (int i=0; i<160; i++) {
    if (tracks[i].trackData) {
      // For any tracks that have data, put it somewhere in the destination file
      tracks[i].startingBlock = STARTBLOCK + 13*(numTracksPacked++);
      // assume tracks[track].bitCount is correct, and recalculate the block size of this track
      uint32_t bytes = (tracks[i].bitCount / 8) + ((tracks[i].bitCount % 8) ? 1 : 0);
      uint32_t blocks = (bytes / 512) + ((bytes % 512) ? 1 : 0);
      tracks[i].blockCount = blocks;
    } else {
      tracks[i].startingBlock = 0;
      tracks[i].blockCount = 0;
      tracks[i].bitCount = 0;
    }

    if (!write16(fh, tracks[i].startingBlock))
      return false;
    if (!write16(fh, tracks[i].blockCount))
      return false;
    if (!write32(fh, tracks[i].bitCount))
      return false;
  }

  // All the track data
  for (int i=0; i<160; i++) {
    if (tracks[i].startingBlock &&
	tracks[i].blockCount) {
      g_filemanager->setSeekPosition(fh, tracks[i].startingBlock*512); // FIXME: no error checking
      uint32_t writeSize = (tracks[i].bitCount / 8) + ((tracks[i].bitCount % 8) ? 1 : 0);
      for (int j=0; j<writeSize; j++) {
	if (!g_filemanager->writeByte(fh, tracks[i].trackData[j])) {
	  return false;
	}
      }
      uint8_t c = 0;
      while (writeSize < tracks[i].blockCount * 512) {
	if (!write8(fh, c))
	  return false;
	writeSize++;
      }
    }
  }
  return true;
}

bool Woz::decodeWozTrackToNib(uint8_t track, nibSector sectorData[16])
{
  for (int sector=0; sector<16; sector++) {
    if (!readSectorData(track, sector, (nibSector *)(&sectorData[sector]))) {
      return false;
    }
  }

  return true;
}

bool Woz::decodeWozTrackToDsk(uint8_t track, uint8_t subtype, uint8_t sectorData[256*16])
{
  // First read it to a NIB; then convert the NIB to a DSK.
  nibSector nibData[16];
  if (!decodeWozTrackToNib(track, nibData))
    return false;

  if (denibblizeTrack((const uint8_t *)nibData, sectorData, subtype, track) != errorNone)
    return false;

  return true;
}

#ifndef TEENSYDUINO
void Woz::dumpInfo()
{
  printf("WOZ image version %d\n", di.version);
  printf("Disk type: %s\n", di.diskType == 1 ? "5.25\"" : "3.5\"");
  printf("Write protected: %s\n", di.writeProtected ? "yes" : "no");
  printf("Synchronized: %s\n", di.synchronized ? "yes" : "no");
  printf("Cleaned: %s\n", di.cleaned ? "yes" : "no");
  printf("Creator: %s\n", di.creator);
  printf("Disk sides: %d\n", di.diskSides);
  printf("Boot sector format: ");
  switch (di.bootSectorFormat) {
  case 0:
  default:
    printf("unknown\n");
    break;
  case 1:
    printf("16 sector\n");
    break;
  case 2: 
    printf("13 sector\n");
    break;
  case 3:
    printf("Both 16 and 13 sector\n");
    break;
  }
  printf("Optimal bit timing: %d ns\n", di.optimalBitTiming * 125);
  printf("Hardware compatability flags: 0x%X\n", di.compatHardware);
  printf("Required RAM: %d K\n", di.requiredRam);
  printf("Largest track: %d bytes\n", di.largestTrack * 512);
  printf("\n");
  
  if (metaData) {
    printf("Metadata:\n");

    char *token, *string, *tofree;
    tofree = string = strdup(metaData);
    char *parts[25];
    memset(parts, 0, sizeof(parts));
    int idx = 0;
    while ((token = strsep(&string, "\n")) != NULL) {
      if (idx >= sizeof(parts)) {
	printf("ERROR: too many metadata keys\n");
	return;
      }
      parts[idx++] = strdup(token);
    }
    free(tofree);

    for (int idx2=0; idx2<idx; idx2++) {
      if (parts[idx2] && strlen(parts[idx2])) {
	char *p = strchr(parts[idx2], '\t');
	if (!p) {
	  printf("ERROR: no delineator on a line of metadata [%s]\n", parts[idx2]);
		 return;
		 }
	    *(p++) = 0;
	  if (strlen(p)) {
	    printf("   %s: %s\n", parts[idx2], p);
	  }
      }
    }

    while (--idx >= 0) {
      free(parts[idx]);
    }
    printf("\n");
  }

  printf("Quarter-track map:\n");
  for (int i=0; i<140; i+=4) {
    printf("%2d     %3d => %3d     %3d => %3d     %3d => %3d     %3d => %3d\n",
	   i/4,
	   i, quarterTrackMap[i],
	   i+1, quarterTrackMap[i+1],
	   i+2, quarterTrackMap[i+2],
	   i+3, quarterTrackMap[i+3]);
  }

  for (int i=0; i<40; i++) {
    printf("Track %d:\n", i);
    if (di.version == 1) {
      printf("  Starts at byte %d\n", tracks[i].startingByte);
    } else {
      printf("  Starts at block %d\n", tracks[i].startingBlock);
    }
    printf("  Number of blocks: %d\n", tracks[i].blockCount);
    printf("  Number of bits: %d\n", tracks[i].bitCount);
    if (tracks[i].bitCount && tracks[i].trackData) {
#if 1
      // Raw track dump
      printf("    Raw track data:\n");
      for (int k=0; k<(tracks[i].bitCount/8)+((tracks[i].bitCount%8)?1:0); k+=16) {
	printf("    0x%.4X :", k);
	for (int j=0; j<16; j++) {
	  if (k+j < (tracks[i].bitCount/8)+((tracks[i].bitCount%8)?1:0)) {
	    printf(" %.2X", tracks[i].trackData[k+j]);
	  }
	}
	printf("\n");
      }

#else
      // Sector parsing & dump
#if 1
      // Look at the sectors in numerical order
      // FIXME: 13-sector support
      nibSector sectorData;
      for (int sector=0; sector<16; sector++) {
	if (readSectorData(i, sector, &sectorData)) {
	  printf("    Volume ID: %d\n", de44(sectorData.volume44));
	  printf("    Track ID: %d\n", de44(sectorData.track44));
	  uint8_t sector = de44(sectorData.sector44);
	  printf("    Sector: %d\n", sector);
	  printf("    Cksum: %d\n", de44(sectorData.checksum44));

	  printf("    Sector Data:\n");
	  for (int k=0; k<342; k+=16) {
	    printf("    0x%.4X :", k);
	    for (int j=0; j<16; j++) {
	      if (k+j < 342) {
		printf(" %.2X", sectorData.data62[k+j]);
	      }
	    }
	    printf("\n");
	  }
	}
      }
#else
      // Look at the sectors found in order on the track
      trackBitIdx = 0x80; trackPointer = 0; trackLoopCounter = 0;
      uint16_t sectorsFound = 0;
      do {
	if (nextDiskByte(i) == 0xD5 &&
	    nextDiskByte(i) == 0xAA &&
	    nextDiskByte(i) == 0x96) {
	  printf("    Volume ID: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));
	  printf("    Track ID: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));
	  uint8_t sector = denib(nextDiskByte(i), nextDiskByte(i));
	  printf("    Sector: %d\n", sector);
	  sectorsFound |= (1 << sector);
	  printf("    Cksum: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));

	  nextDiskByte(i); // skip epilog
	  nextDiskByte(i);
	  nextDiskByte(i);
	  // look for data prolog d5 aa ad
	  while (nextDiskByte(i) != 0xD5 && trackLoopCounter < 2)
	    ;
	  if (trackLoopCounter < 2) {
	    // Hope that's it and skip two bytes
	    nextDiskByte(i);
	    nextDiskByte(i);
	    // Dump the 6-and-2 data
	    printf("    Sector Data:\n");
	    for (int k=0; k<342; k+=16) {
	      printf("    0x%.4X :", k);
	      for (int j=0; j<16; j++) {
		if (k+j < 342) {
		  printf(" %.2X", nextDiskByte(i));
		}
	      }
	      printf("\n");
	    }
	  }
	}

      } while (sectorsFound != 0xFFFF && trackLoopCounter < 2);
#endif
#endif
    }
  }
}
#endif

bool Woz::isSynchronized()
{
  return di.synchronized;
}

uint8_t Woz::trackNumberForQuarterTrack(uint16_t qt)
{
  return quarterTrackMap[qt];
}


