#include "woz.h"
#include <string.h>
#include "crc32.h"
#include "nibutil.h"
#include "version.h"
#include "fscompat.h"

// Block number we start packing data bits after (Woz 2.0 images)
#define STARTBLOCK 3

#ifdef TEENSYDUINO
#define SKIPCHECKSUM
#define malloc extmem_malloc
#define free extmem_free
#define calloc extmem_calloc
#define realloc extmem_realloc
#endif

#define PREP_SECTION(fd, t) {      \
  uint32_t type = t;               \
  if (!write32(fd, type))           \
    return false;                  \
  if (!write32(fd, 0))              \
    return false;                  \
  curpos = lseek(fd, 0, SEEK_CUR); \
 }

#define END_SECTION(fd) {                   \
  long endpos = lseek(fd, 0, SEEK_CUR);	    \
  lseek(fd, curpos-4, SEEK_SET);            \
  uint32_t chunksize = endpos - curpos;     \
  if (!write32(fd, chunksize))               \
    return false;                           \
  lseek(fd, 0, SEEK_END);                   \
  }

Woz::Woz(bool verbose, uint8_t dumpflags)
{
  fd = -1;
  trackPointer = 0;
  trackBitIdx = 0x80;
  trackBitCounter = 0;
  trackByteFromDataTrack = 255;
  trackLoopCounter = 0;
  imageType = T_AUTO;
  metaData = NULL;
  this->verbose = verbose;
  this->dumpflags = dumpflags;

  memset(&quarterTrackMap, 255, sizeof(quarterTrackMap));
  memset(&di, 0, sizeof(diskInfo));
  memset(&tracks, 0, sizeof(tracks));
  randPtr = 0;
}

Woz::~Woz()
{
  if (fd != -1) {
    close(fd);
    fd = -1;
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

// external interface for a disk subsystem to write a bit
bool Woz::writeNextWozBit(uint8_t datatrack, uint8_t bit)
{
  if (datatrack == 0xFF) {
    printf("ERROR: tried to write bit on half-track; not implemented\n");
    return true;
  }

  if (!tracks[datatrack].trackData) {
    fprintf(stderr, "ERROR: tried to writeNextWozBit to a data track that's not loaded, and we can't possibly tell which QT that should be\n");
    return false;
  }
  
  //  if (trackByteFromDataTrack != datatrack) {
    // FIXME what if trackPointer is out of bounds for this track
    trackByte = tracks[datatrack].trackData[trackPointer];
    trackByteFromDataTrack = datatrack;
    //  }

  if (bit)
    trackByte |= trackBitIdx;
  else
    trackByte &= ~trackBitIdx;

  tracks[datatrack].trackData[trackPointer] = trackByte;

  advanceBitStream(datatrack);
  trackDirty = true;
  
  return true;
}

// external interface for a disk interface to write a byte
bool Woz::writeNextWozByte(uint8_t datatrack, uint8_t b)
{
  if (datatrack == 0xFF) {
    // Not on a track, so pretend to write but throw it away. FIXME:
    // probably want to create a new Woz track entry here.
    fprintf(stderr, "ERROR: tried to write to a half track; not implemented\n");
    return true;
  }

  if (!tracks[datatrack].trackData) {
    fprintf(stderr, "ERROR: tried to write to a track that's not loaded, and it's not possible to tell what QT was meant\n");
    return false;
  }

  // We could be byte-aligned, but it's not guaranteed, so this
  // handles it bitwise.
  for (uint8_t i=0; i<8; i++) {
    writeNextWozBit(datatrack, b & (1 << (7-i)) ? 1 : 0);
  }
  return true;
}

void Woz::advanceBitStream(uint8_t datatrack)
{
  trackBitCounter++;
  
  trackBitIdx >>= 1;
  if (!trackBitIdx) {
    trackBitIdx = 0x80;
    trackPointer++;

    if ((di.version == 2 && trackPointer < tracks[datatrack].blockCount*512)||
	(di.version == 1 && trackPointer < NIBTRACKSIZE)) {
      trackByte = tracks[datatrack].trackData[trackPointer];
      trackByteFromDataTrack = datatrack;
    }
  }

  // This could have " || trackPointer >= tracks[datatrack].bitCount/8" but
  // it should be totally redundant
  if (trackBitCounter >= tracks[datatrack].bitCount) {
    trackPointer = 0;
    trackBitIdx = 0x80;
    trackBitCounter = 0;
    trackLoopCounter++;
    trackByte = tracks[datatrack].trackData[trackPointer];
    trackByteFromDataTrack = datatrack;
  }
}

uint8_t Woz::getNextWozBit(uint8_t datatrack)
{
  if (datatrack >= 160) {
    return 0;
  }

  if (!tracks[datatrack].trackData) {
    fprintf(stderr, "ERROR: getNextWozBit was called without the track being cached, and it can't possibly know which QT to load it from\n");
    return 0;
  }

  if (trackByteFromDataTrack != datatrack) {
    // FIXME what if trackPointer is out of bounds for this track
    trackByte = tracks[datatrack].trackData[trackPointer];
    trackByteFromDataTrack = datatrack;
  }
  
  uint8_t ret = (trackByte & trackBitIdx) ? 1 : 0;
  advanceBitStream(datatrack);
  return ret;
}

uint8_t Woz::fakeBit()
{
  // 30% should be 1s, but I'm not biasing the data here, so this is
  // more like 50% 1s.

  if (randPtr == 0) {
    randPtr = 0x80;
    randData = (uint8_t) ((float)256 * rand() / (RAND_MAX + 1.0));
  }

  uint8_t ret = (randData & randPtr) ? 1 : 0;
  randPtr >>= 1;
  
  return ret;
}

uint8_t Woz::nextDiskBit(uint8_t datatrack)
{
  if (!tracks[datatrack].trackData) {
    fprintf(stderr, "ERROR: nextDiskBit was called without the track being cached, and it can't possibly know which QT to load it from\n");
    return 0;
  }

  static uint8_t head_window = 0;
  head_window <<= 1;
  head_window |= getNextWozBit(datatrack);
  if ((head_window & 0x0f) != 0x00) {
    return (head_window & 0x02) >> 1;
  } else {
    return fakeBit();
  }
}

uint8_t Woz::nextDiskByte(uint8_t datatrack)
{
  if (!tracks[datatrack].trackData) {
    fprintf(stderr, "ERROR: nextDiskByte was called without the track being cached, and it can't possibly know which QT to load it from\n");
    return 0;
  }

  uint8_t d = 0;
  while ((d & 0x80) == 0) {
    d <<= 1;
    d |= nextDiskBit(datatrack);
  }
  return d;
}

static bool write8(int fd, uint8_t v)
{
  if (write(fd, &v, 1) != 1)
    return false;
  return true;
}

static bool write16(int fd, uint16_t v)
{
  if (!write8(fd, v & 0xFF))
    return false;
  v >>= 8;
  if (!write8(fd, v & 0xFF))
    return false;
  return true;
}

static bool write32(int fd, uint32_t v)
{
  for (int i=0; i<4; i++) {
    if (!write8(fd, v&0xFF))
      return false;
    v >>= 8;
  }
  return true;
}

static bool read8(int fd, uint8_t *toWhere)
{
  uint8_t r;
  if (read(fd, &r, 1) != 1) {
    return false;
  }
  *toWhere = r;

  return true;
}

static bool read16(int fd, uint16_t *toWhere)
{
  uint16_t ret = 0;
  for (int i=0; i<2; i++) {
    uint8_t r;
    if (!read8(fd, &r)) {
      return false;
    }
    ret >>= 8;
    ret |= (r<<8);
  }

  *toWhere = ret;

  return true;
}

static bool read32(int fd, uint32_t *toWhere)
{
  uint32_t ret = 0;
  for (int i=0; i<4; i++) {
    uint8_t r;
    if (!read8(fd, &r)) {
      return false;
    }
    ret >>= 8;
    ret |= (r<<24);
  }

  *toWhere = ret;
  return true;
}

bool Woz::writeFile(const char *filename, uint8_t forceType)
{
  if (forceType == T_AUTO) {
    // Try to determine type from the file extension
    const char *p = strrchr(filename, '.');
    if (!p) {
      fprintf(stderr, "Unable to determine file type of '%s'\n", filename);
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
      fprintf(stderr, "Unable to determine file type of '%s'\n", filename);
      return false;
    }
  }

  switch (forceType) {
  case T_WOZ:
    return writeWozFile(filename, forceType);
  case T_DSK:
  case T_PO:
    return writeDskFile(filename, forceType);
  case T_NIB:
    return writeNibFile(filename);
  default:
    fprintf(stderr, "Unknown disk type; unable to write\n");
    return false;
  }
}


bool Woz::writeWozFile(const char *filename, uint8_t subtype)
{
  int fdout = -1;

  fdout = open(filename, O_TRUNC|O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
  if (fdout == -1) {
    perror("ERROR: Unable to open output file");
    return false;
  }

  bool retval = writeWozFile(fdout, subtype);
  close(fdout);
  return retval;
}

bool Woz::writeWozFile(int fdout, uint8_t subtype)
{
  bool retval = false;
  uint32_t tmp32; // scratch 32-bit value
  off_t crcPos, endPos;
  off_t curpos; // used in macros to dynamically tell what size the chunks are
  uint32_t crcDataSize;
  uint8_t *crcData = NULL;

  int version = 2; // FIXME: determine from subtype
  
  if (version > 2 || !version) {
    fprintf(stderr, "ERROR: version must be 1 or 2\n");
    return false;
  }
  lseek(fdout, 0, SEEK_SET);
  
  // header
  if (version == 1) {
    tmp32 = 0x315A4F57;
  } else {
    tmp32 = 0x325A4F57;
  }
  if (!write32(fdout, tmp32)) {
    fprintf(stderr, "ERROR: failed to write\n");
    goto done;
  }
  tmp32 = 0x0A0D0AFF;
  if (!write32(fdout, tmp32)) {
    fprintf(stderr, "ERROR: failed to write\n");
    goto done;
  }

  // We'll come back and write the checksum later
  crcPos = lseek(fdout, 0, SEEK_CUR);
  tmp32 = 0;
  if (!write32(fdout, tmp32)) {
    fprintf(stderr, "ERROR: failed to write\n");
    goto done;
  }

  PREP_SECTION(fdout, 0x4F464E49); // 'INFO'
  if (!writeInfoChunk(version, fdout)) {
    fprintf(stderr, "ERROR: failed to write INFO chunk\n");
    goto done;
  }
  END_SECTION(fdout);

  PREP_SECTION(fdout, 0x50414D54); // 'TMAP'
  if (!writeTMAPChunk(version, fdout)) {
    fprintf(stderr, "ERROR: failed to write TMAP chunk\n");
    goto done;
  }
  END_SECTION(fdout);

  PREP_SECTION(fdout, 0x534B5254); // 'TRKS'
  if (!writeTRKSChunk(version, fdout)) {
    fprintf(stderr, "ERROR: failed to write TRKS chunk\n");
    goto done;
  }
  END_SECTION(fdout);

  // Write the metadata if we have any
  if (metaData) {
    PREP_SECTION(fdout, 0x4154454D); // 'META'
    if (write(fdout, metaData, strlen(metaData)) != strlen(metaData)) {
      fprintf(stderr, "ERROR: failed to write META chunk\n");
      goto done;
    }
    END_SECTION(fdout);
  }

  // FIXME: missing the WRIT chunk, if it exists

  // Fix up the checksum. Optional; the spec says it can be 0 meaning
  // "don't verify"
#ifndef SKIPCHECKSUM
  endPos = lseek(fdout, 0, SEEK_CUR);
  crcDataSize = endPos-crcPos-4;
  crcData = (uint8_t *)malloc(crcDataSize);
  if (!crcData) {
    fprintf(stderr, "ERROR: failed to malloc crc data chunk\n");
    goto done;
  }
    
  // Read the data in for checksumming
  if (lseek(fdout, crcPos+4, SEEK_SET) == -1) {
    fprintf(stderr, "ERROR: failed to fseek to crcPos+4 (0x%llX)\n", crcPos+4);
    goto done;
  }

  tmp32 = read(fdout, crcData, crcDataSize);
  if (tmp32 != crcDataSize) {
    fprintf(stderr, "ERROR: failed to read in data for checksum [read %d, wanted %d]\n", tmp32, crcDataSize);
    goto done;
  }
    
  tmp32 = compute_crc_32(crcData, crcDataSize);
  // Write it back out

  lseek(fdout, crcPos, SEEK_SET);
  if (!write32(fdout, tmp32)) {
    fprintf(stderr, "ERROR: failed to write CRC\n");
    goto done;
  }
#endif
  
  retval = true;

 done:
  if (crcData)
    free(crcData);
  return retval;
}

bool Woz::writeDskFile(const char *filename, uint8_t subtype)
{
  int fdout = open(filename, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
  if (fdout == -1) {
    perror("Failed to open output file");
    exit(1);
  }

  bool retval = writeDskFile(fdout, subtype);
  close(fdout);
  return retval;
}

bool Woz::writeDskFile(int fdout, uint8_t subtype)
{
  if (isSynchronized()) {
    fprintf(stderr, "WARNING: disk image has synchronized tracks; it may not work as a DSK or NIB file.\n");
  }
  lseek(fdout, 0, SEEK_SET);

  uint8_t sectorData[256*16];
  for (int phystrack=0; phystrack<35; phystrack++) {
    if (!decodeWozTrackToDsk(quarterTrackMap[phystrack*4], subtype, sectorData)) {
      fprintf(stderr, "Failed to decode track %d; aborting\n", phystrack);
      exit(1);
    }
    ssize_t numWritten = write(fdout, sectorData, 256*16);
    if (numWritten != 256*16) {
      perror("Failed[2] to write to track; aborting");
      exit(1);
    }
  }
  return true;
}

bool Woz::writeNibFile(const char *filename)
{
  int fdout = open(filename, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
  if (fdout == -1) {
    perror("Failed to open output file");
    exit(1);
  }

  bool retval = writeNibFile(fdout);
  close(fdout);
  return retval;
}

bool Woz::writeNibFile(int fdout)
{
  if (isSynchronized()) {
    fprintf(stderr, "WARNING: disk image has synchronized tracks; it may not work as a DSK or NIB file.\n");
  }
  lseek(fdout, 0, SEEK_SET);

  nibSector nibData[16];
  for (int phystrack=0; phystrack<35; phystrack++) {
    if (!decodeWozTrackToNibFromDataTrack(quarterTrackMap[phystrack*4], nibData)) {
      fprintf(stderr, "Failed to decode track %d; aborting\n", phystrack);
      exit(1);
    }
    if (write(fdout, nibData, NIBTRACKSIZE) != NIBTRACKSIZE) {
      fprintf(stderr, "Failed[1] to write track %d; aborting\n", phystrack);
      exit(1);
    }
  }
  return true;
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

// Only used if we didn't preload a data track; the load we perform
// differs based on the image type we originally read from
bool Woz::loadMissingTrackFromImage(uint8_t datatrack)
{
  // If we're going to malloc a new one, then find all the other ones
  // that might be malloc'd and purge them if we're
  // autoFlushTrackData==true (trying to limit memory use)
  if (autoFlushTrackData == true) {
    flush();
    for (int i=0; i<160; i++) {
      if (tracks[i].trackData) {
        free(tracks[i].trackData);
        tracks[i].trackData = NULL;
      }
    }
  }
  // Based on the source image type, load the data track we're looking for
  if (imageType == T_WOZ) {
    // If the source was WOZ, just load the datatrack directly
    return readWozDataTrack(datatrack);
  } else if (imageType == T_PO ||
	     imageType == T_DSK) {
    // If the source was a DSK file, then the datatrack was mapped directly
    // from the physical track
    if (datatrack >= 35) {
      // There are only 35 tracks; the remainder are blank.
      tracks[datatrack].trackData = NULL;
      return true;
    }
    
    uint8_t phystrack = datatrack; // used for clarity of which kind of track we mean, below
    
    static uint8_t sectorData[256*16];
    
    lseek(fd, 256*16*phystrack, SEEK_SET);
    
    if (read(fd, sectorData, 256*16) != 256*16) {
      fprintf(stderr, "Failed to read track\n");
      return false;
    }
    
    tracks[datatrack].trackData = (uint8_t *)calloc(NIBTRACKSIZE, 1);
    if (!tracks[datatrack].trackData) {
      fprintf(stderr, "Failed to malloc track data\n");
      return false;
    }

    tracks[datatrack].startingBlock = STARTBLOCK + 13*phystrack; // make it look like it came from a WOZ2 image
    tracks[datatrack].blockCount = 13;
    uint32_t sizeInBits = nibblizeTrack(tracks[datatrack].trackData, sectorData, imageType, phystrack);
    tracks[datatrack].bitCount = sizeInBits; // ... reality.
    
    return true;
  }
  else if (imageType == T_NIB) {
    if (datatrack >= 35) {
      // There are only 35 tracks; the remainder are blank.
      tracks[datatrack].trackData = NULL;
      return true;
    }
    // If the source was a NIB file, then the datatrack is directly
    // mapped 1:1 to the physical track
    uint8_t phystrack = datatrack; // used for clarity of which kind of track we mean, below
    tracks[datatrack].trackData = (uint8_t *)calloc(NIBTRACKSIZE, 1);
    if (!tracks[datatrack].trackData) {
      fprintf(stderr, "Failed to malloc track data\n");
      return false;
    }

    lseek(fd, NIBTRACKSIZE * phystrack, SEEK_SET);
    read(fd, tracks[datatrack].trackData, NIBTRACKSIZE);
    // FIXME: no error checking
    
    tracks[datatrack].startingBlock = STARTBLOCK + 13*phystrack; // make it look like it came from a WOZ2 image
    tracks[datatrack].blockCount = 13;
    tracks[datatrack].bitCount = NIBTRACKSIZE*8;
    
    return true;
  }
  
  printf("ERROR: don't know how we reached this point\n");
  return false;
}

bool Woz::readDskFile(const char *filename, bool preloadTracks, uint8_t subtype)
{
  bool retval = false;
  autoFlushTrackData = !preloadTracks;
  imageType = subtype;

  if (fd != -1) close(fd);
  fd = open(filename, O_RDWR, S_IRUSR|S_IWUSR);
  if (fd == -1) {
     perror("Unable to open input file");
    goto done;
  }

  _initInfo();

  // Now read in the 35 tracks of data from the DSK file and convert them to NIB
  if (preloadTracks) {
    uint8_t sectorData[256*16];
    for (int phystrack=0; phystrack<35; phystrack++) {
      uint32_t bytesRead = read(fd, sectorData, 256*16);
      if (bytesRead != 256*16) {
	fprintf(stderr, "Failed to read DSK data; got %d bytes, wanted %d\n", bytesRead, 256);
	goto done;
      }
      uint8_t datatrack = quarterTrackMap[phystrack*4];
      tracks[datatrack].trackData = (uint8_t *)calloc(NIBTRACKSIZE, 1);
      if (!tracks[datatrack].trackData) {
	fprintf(stderr, "Failed to malloc track data\n");
	goto done;
      }

      tracks[datatrack].startingBlock = STARTBLOCK + 13*datatrack; // make it look like it came from a WOZ2 image
      tracks[datatrack].blockCount = 13;
      uint32_t sizeInBits = nibblizeTrack(tracks[datatrack].trackData, sectorData, subtype, phystrack);
      tracks[datatrack].bitCount = sizeInBits; // ... reality.
    }
  }
  
  retval = true;

 done:
  //  if (preloadTracks && fd != -1) {
    //    close(fd);
    //    fd = -1;
    //  }
  return retval;
}

bool Woz::readNibFile(const char *filename, bool preloadTracks)
{
  autoFlushTrackData = !preloadTracks;
  imageType = T_NIB;

  if (fd != -1) close(fd);
  fd = open(filename, O_RDWR, S_IRUSR|S_IWUSR);
  if (fd == -1) {
    perror("Unable to open input file");
    return false;
  }
  
  _initInfo();

  // Now read in the 35 tracks of data from the nib file
  if (preloadTracks) {
    nibSector nibData[16];
    for (int phystrack=0; phystrack<35; phystrack++) {
      uint32_t bytesRead = read(fd, nibData, NIBTRACKSIZE);
      if (bytesRead != NIBTRACKSIZE) {
	fprintf(stderr, "Failed to read NIB data; got %d bytes, wanted %d\n", bytesRead, NIBTRACKSIZE);
	return false;
      }
      uint8_t datatrack = quarterTrackMap[phystrack * 4];
      tracks[datatrack].trackData = (uint8_t *)calloc(NIBTRACKSIZE, 1);
      if (!tracks[datatrack].trackData) {
	fprintf(stderr, "Failed to malloc track data\n");
	return false;
      }

      memcpy(tracks[datatrack].trackData, nibData, NIBTRACKSIZE);
      tracks[datatrack].startingBlock = STARTBLOCK + 13*phystrack; // make it look like it came from a WOZ2 image
      tracks[datatrack].blockCount = 13;
      tracks[datatrack].bitCount = NIBTRACKSIZE*8;
    }
  }
  //  if (preloadTracks && fd != -1) {
    //    close(fd);
    //    fd = -1;
    //  }

  return true;
}

bool Woz::readWozFile(const char *filename, bool preloadTracks)
{
  imageType = T_WOZ;
  autoFlushTrackData = !preloadTracks;

  if (fd != -1) close(fd);
  fd = open(filename, O_RDWR, S_IRUSR|S_IWUSR);
  if (fd == -1) {
    perror("Unable to open input file");
    return false;
  }
  
  // Header
  uint32_t h;
  read32(fd, &h);
  if (h == 0x325A4F57 || h == 0x315A4F57) {
    if (verbose) {
      printf("WOZ%c disk image\n", (h & 0xFF000000)>>24);
    }
  } else {
    fprintf(stderr, "Unknown disk image type; can't continue\n");
    if (preloadTracks && fd != -1)
      close(fd);
    return false;
  }

  uint32_t tmp;
  if (!read32(fd, &tmp)) {
    fprintf(stderr, "Read failure\n");
    if (preloadTracks && fd != -1)
      close(fd);
    return false;
  }
  if (tmp != 0x0A0D0AFF) {
    fprintf(stderr, "WOZ header failure; exiting\n");
    if (preloadTracks && fd != -1)
      close(fd);
    return false;
  }
  uint32_t crc32;
  read32(fd, &crc32);
  // If CRC is set, then check it
  if (crc32) {
    // FIXME: check CRC
    if (verbose) {
      printf("Disk crc32 should be 0x%X\n", crc32);
    }
  }
  
  uint32_t fpos = 12;
  uint8_t haveData = 0;

#define cINFO 1
#define cTMAP 2
#define cTRKS 4

  while (1) {
    if (lseek(fd, fpos, SEEK_SET) == -1) {
      break;
    }

    uint32_t chunkType;
    if (!read32(fd, &chunkType)) {
      break;
    }
    uint32_t chunkDataSize;
    read32(fd, &chunkDataSize);
    if ((int32_t)chunkDataSize < 0) {
      printf("ERROR: data size < 0?\n");
      exit(1);
    }

    bool isOk;

    switch (chunkType) {
    case 0x4F464E49: // 'INFO'
      if (verbose) {
	printf("Reading INFO chunk starting at byte 0x%llX\n",
	       (unsigned long long)lseek(fd, 0, SEEK_CUR));
      }
      isOk = parseInfoChunk(chunkDataSize);
      haveData |= cINFO;
      break;
    case 0x50414D54: // 'TMAP'
      if (verbose) {
	printf("Reading TMAP chunk starting at byte 0x%llX\n",
	       (unsigned long long)lseek(fd, 0, SEEK_CUR));
      }
      isOk = parseTMAPChunk(chunkDataSize);
      haveData |= cTMAP;
      break;
    case 0x534B5254: // 'TRKS'
      if (verbose) {
	printf("Reading TRKS chunk starting at byte 0x%llX\n",
	       (unsigned long long)lseek(fd, 0, SEEK_CUR));
      }
      isOk = parseTRKSChunk(chunkDataSize);
      haveData |= cTRKS;
      break;
    case 0x4154454D: // 'META'
      if (verbose) {
	printf("Reading META chunk starting at byte 0x%llX\n",
	       (unsigned long long)lseek(fd, 0, SEEK_CUR));
      }	  
      isOk = parseMetaChunk(chunkDataSize);
      break;
    default:
      printf("Unknown chunk type 0x%X\n", chunkType);
      if (preloadTracks && fd != -1)
	close(fd);
      return false;
      break;
    }

    if (!isOk) {
      printf("Chunk parsing [0x%X] failed; exiting\n", chunkType);
      if (preloadTracks && fd != -1)
	close(fd);
      return false;
    }
    fpos += chunkDataSize + 8; // 8 bytes for the ChunkID and the ChunkSize
  }

  if (haveData != 0x07) {
    printf("ERROR: missing one or more critical sections\n");
    return false;
  }

  // For a Woz file, we need to read *every* quarter-track; and if we've
  // already got the target track's data, we don't need to re-read it.
  // And if we're not preloading the tracks, then we'll wind up loading
  // them on demand later.
  if (preloadTracks) {
    for (int i=0; i<160; i++) {
      if (!readWozDataTrack(i)) {
	printf("Failed to read Woz datatrack %d\n", i);
	if (fd != -1) {
	  close(fd);
	  fd = -1;
	}
	return false;
      }
    }
  }

  //  if (preloadTracks && fd != -1) {
  //    fd = -1;
  //    close(fd);
  //  }
  return true;
}

bool Woz::readFile(const char *filename, bool preloadTracks, uint8_t forceType)
{
  if (forceType == T_AUTO) {
    // Try to determine type from the file extension
    const char *p = strrchr(filename, '.');
    if (!p) {
      printf("Unable to determine file type of '%s'\n", filename);
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
      printf("Unable to determine file type of '%s'\n", filename);
      return false;
    }
  }

  switch (forceType) {
  case T_WOZ:
    return readWozFile(filename, preloadTracks);
  case T_DSK:
  case T_PO:
    return readDskFile(filename, preloadTracks, forceType);
  case T_NIB:
    return readNibFile(filename, preloadTracks);
  default:
    printf("Unknown disk type; unable to read\n");
    return false;
  }
}

bool Woz::parseTRKSChunk(uint32_t chunkSize)
{
  if (di.version == 2) {
    for (int i=0; i<160; i++) {
      if (!read16(fd, &tracks[i].startingBlock))
	return false;
      if (!read16(fd, &tracks[i].blockCount))
	return false;
      if (!read32(fd, &tracks[i].bitCount))
	return false;
      tracks[i].startingByte = 0; // v1-specific
    }
    return true;
  }

  // V1 parsing
  uint32_t ptr = 0;
  uint8_t trackNumber = 0;
  while (ptr < chunkSize) {
    tracks[trackNumber].startingByte = trackNumber * 6656 + 256;
    tracks[trackNumber].startingBlock = 0; // v2-specific
    tracks[trackNumber].blockCount = 13;
    lseek(fd, (trackNumber * 6656 + 256) + 6648, SEEK_SET);
    uint16_t numBits;
    if (!read16(fd, &numBits)) {
      return false;
    }
    if (verbose) {
      printf("Track %d: read %d bits\n", trackNumber, numBits);
    }
    if (numBits > 6656 * 8) {
      fprintf(stderr, "WARNING: track %d looks like it's too long (%d bits > 6656 bytes)?\n", trackNumber, numBits);
    }
    tracks[trackNumber].bitCount = numBits;
    ptr += 6656;
    trackNumber++;
  }

  return true;
}

bool Woz::parseTMAPChunk(uint32_t chunkSize)
{
  if (chunkSize != 0xa0) {
    printf("TMAP chunk is the wrong size; aborting\n");
    return false;
  }

  for (int i=0; i<40*4; i++) {
    if (!read8(fd, (uint8_t *)&quarterTrackMap[i]))
      return false;
    chunkSize--;
  }
  if (verbose && 0){
    printf("Read quarter-track map:\n");
    for (int i=0; i<140; i+=4) {
      printf("%2d     %3d => %3d     %3d => %3d     %3d => %3d     %3d => %3d\n",
	     i/4,
	     i, quarterTrackMap[i],
	     i+1, quarterTrackMap[i+1],
	     i+2, quarterTrackMap[i+2],
	     i+3, quarterTrackMap[i+3]);
    }
  }

  return true;
}

// return true if successful
bool Woz::parseInfoChunk(uint32_t chunkSize)
{
  if (chunkSize != 60) {
    fprintf(stderr, "INFO chunk size is not 60; aborting\n");
    return false;
  }

  if (!read8(fd, &di.version))
    return false;
  if (di.version > 2) {
    fprintf(stderr, "Incorrect version header; aborting\n");
    return false;
  }

  if (!read8(fd, &di.diskType))
    return false;
  if (di.diskType != 1) {
    fprintf(stderr, "Not a 5.25\" disk image; aborting\n");
    return false;
  }

  if (!read8(fd, &di.writeProtected))
    return false;

  if (!read8(fd, &di.synchronized))
    return false;

  if (!read8(fd, &di.cleaned))
    return false;

  di.creator[32] = 0;
  for (int i=0; i<32; i++) {
    if (!read8(fd, (uint8_t *)&di.creator[i]))
      return false;
  }

  if (di.version >= 2) {
    if (!read8(fd, &di.diskSides))
      return false;
    if (!read8(fd, &di.bootSectorFormat))
      return false;
    if (!read8(fd, &di.optimalBitTiming))
      return false;
    if (!read16(fd, &di.compatHardware))
      return false;
    if (!read16(fd, &di.requiredRam))
      return false;
    if (!read16(fd, &di.largestTrack))
      return false;
  } else {
    di.diskSides = 0;
    di.bootSectorFormat = 0;
    di.compatHardware = 0;
    di.requiredRam = 0;
    di.largestTrack = 13; // 13 * 512 bytes = 6656. All tracks are
			  // padded to 6646 (yes, 6646, not 6656)bytes
			  // in the v1 image.
    di.optimalBitTiming = 32; // "standard" disk bit timing for a 5.25" disk (4us per bit)
  }

  return true;
}

bool Woz::parseMetaChunk(uint32_t chunkSize)
{
  metaData = (char *)calloc(chunkSize+1, 1);
  if (!metaData) {
      fprintf(stderr, "Failed to malloc metadata\n");
    return false;
  }

  if (read(fd, metaData, chunkSize) != chunkSize)
    return false;

  metaData[chunkSize] = 0;

  return true;
}

bool Woz::readWozDataTrack(uint8_t datatrack)
{
  // If it's already loaded then there's nothing to do here
  if (tracks[datatrack].trackData)
    return true;

  // If we have no open FD, then assume anything missing is supposed
  // to be missing
  if (fd == -1) {
    return true;
  }

  uint16_t bitsStartBlock = tracks[datatrack].startingBlock;

  // Allocate a new buffer for this track
  uint32_t count = tracks[datatrack].blockCount * 512;
  if (di.version == 1) count = (tracks[datatrack].bitCount / 8) + ((tracks[datatrack].bitCount % 8) ? 1 : 0);
  if (tracks[datatrack].trackData) {
    return true; // We've already read this track's data; don't re-read it
  }
  tracks[datatrack].trackData = (uint8_t *)calloc(count, 1);
  if (!tracks[datatrack].trackData) {
    perror("Failed to alloc buf to read track magnetic data");

    return false;
  }

  if (di.version == 1) {
    if (verbose) {
      printf("Reading datatrack[1] %d starting at byte 0x%X\n",
	     datatrack,
	     tracks[datatrack].startingByte);
    }
    if (lseek(fd, tracks[datatrack].startingByte, SEEK_SET) == -1) {
      perror("Failed to seek to start of block");
      return false;
    }
  } else {
    if (verbose) {
      printf("Reading datatrack[2] %d starting at byte 0x%X\n",
	     datatrack,
	     bitsStartBlock*512);
    }
    if (lseek(fd, bitsStartBlock*512, SEEK_SET) == -1) {
      perror("Failed to seek to start of block");
      return false;
    }
  }
  uint32_t didRead = read(fd, tracks[datatrack].trackData, count);

  if (didRead != count) {
    printf("Failed to read all track data for track [read %d, wanted %d]\n", didRead, count);
    return false;
  }

  return true;
}


bool Woz::readNibSectorDataFromDataTrack(uint8_t dataTrack, uint8_t sector, nibSector *sectorData)
{
  // Find the sector header for this sector...
  uint32_t ptr = 0;

  if (!tracks[dataTrack].trackData) {
    // Load the cached track for this phys Nib track.
    if (!loadMissingTrackFromImage(dataTrack)) {
      fprintf(stderr, "Failed to load track data for track %d\n", dataTrack);
      return false;
    }
  }

  memset(sectorData->gap1, 0xFF, sizeof(sectorData->gap1));
  memset(sectorData->gap2, 0xFF, sizeof(sectorData->gap2));

  // Allow two loops through the track data looking for the sector prolog
  uint32_t endCount = tracks[dataTrack].blockCount*512*2;
  if (di.version == 1) endCount = 2*6646;
  while (ptr < endCount) {
    sectorData->sectorProlog[0] = sectorData->sectorProlog[1];
    sectorData->sectorProlog[1] = sectorData->sectorProlog[2];
    sectorData->sectorProlog[2] = nextDiskByte(dataTrack);
    ptr++;
    
    if (sectorData->sectorProlog[0] == 0xd5 &&
	sectorData->sectorProlog[1] == 0xaa &&
	sectorData->sectorProlog[2] == 0x96) {
      // Found *a* sector header. See if it's ours.
      sectorData->volume44[0] = nextDiskByte(dataTrack);
      sectorData->volume44[1] = nextDiskByte(dataTrack);
      sectorData->track44[0] = nextDiskByte(dataTrack);
      sectorData->track44[1] = nextDiskByte(dataTrack);
      sectorData->sector44[0] = nextDiskByte(dataTrack);
      sectorData->sector44[1] = nextDiskByte(dataTrack);
      sectorData->checksum44[0] = nextDiskByte(dataTrack);
      sectorData->checksum44[1] = nextDiskByte(dataTrack);
      sectorData->sectorEpilog[0] = nextDiskByte(dataTrack);
      sectorData->sectorEpilog[1] = nextDiskByte(dataTrack);
      sectorData->sectorEpilog[2] = nextDiskByte(dataTrack);

      if (sectorData->sectorEpilog[0] == 0xde &&
	  sectorData->sectorEpilog[1] == 0xaa &&
	  sectorData->sectorEpilog[2] == 0xeb) {
	// Header is integral. See if it's our sector:
	uint8_t sectorNum = de44(sectorData->sector44);
	if (sectorNum != sector) {
	  continue;
	}
	// It's our sector - find the data chunk and read it
	while (ptr < tracks[dataTrack].blockCount*512*2) {
	  sectorData->dataProlog[0] = sectorData->dataProlog[1];
	  sectorData->dataProlog[1] = sectorData->dataProlog[2];
	  sectorData->dataProlog[2] = nextDiskByte(dataTrack);
	  ptr++;

	  if (sectorData->dataProlog[0] == 0xd5 &&
	      sectorData->dataProlog[1] == 0xaa &&
	      sectorData->dataProlog[2] == 0xad) {
	    // Found the data; copy it in
	    for (int i=0; i<342; i++) {
	      sectorData->data62[i] = nextDiskByte(dataTrack);
	    }
	    sectorData->checksum = nextDiskByte(dataTrack);
	    sectorData->dataEpilog[0] = nextDiskByte(dataTrack);
	    sectorData->dataEpilog[1] = nextDiskByte(dataTrack);
	    sectorData->dataEpilog[2] = nextDiskByte(dataTrack);
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

bool Woz::writeInfoChunk(uint8_t version, int fdout)
{
  if (!write8(fdout, version) ||
      !write8(fdout, di.diskType) ||
      !write8(fdout, di.writeProtected) ||
      !write8(fdout, di.synchronized) ||
      !write8(fdout, di.cleaned))
    return false;

  for (int i=0; i<32; i++) {
    if (!write8(fdout, di.creator[i]))
      return false;
  }
  
  if (version >= 2) {
    // If we read a Wozv1, this will be set to 0. Set it to 1.
    if (di.diskSides == 0)
      di.diskSides = 1;

    if ( !write8(fdout, di.diskSides) ||
	 !write8(fdout, di.bootSectorFormat) ||
	 !write8(fdout, di.optimalBitTiming) ||
	 !write16(fdout, di.compatHardware) ||
	 !write16(fdout, di.requiredRam) ||
	 !write16(fdout, di.largestTrack))
      return false;
  }

  // Padding
  for (int i=0; i<((version==1)?23:14); i++) {
    if (!write8(fdout, 0))
      return false;
  }
  return true;
}

bool Woz::writeTMAPChunk(uint8_t version, int fdout)
{
  for (int i=0; i<40*4; i++) {
    if (!write8(fdout, quarterTrackMap[i]))
      return false;
  }

  return true;
}

bool Woz::writeTRKSChunk(uint8_t version, int fdout)
{
  if (version == 1) {
    printf("V1 write is not implemented\n");
    return false;
  }

  // Reconstruct all of the starting blocks/blockCounts for each
  // track. The bitCount should be correct.
  uint8_t numTracksPacked = 0;
  for (int i=0; i<160; i++) {
    // If we didn't preload, and the track isn't loaded, then load it now
    if (autoFlushTrackData && !tracks[i].trackData) {
      loadMissingTrackFromImage(i);
    }
    
    if (tracks[i].trackData && tracks[i].bitCount) {
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
    if (!write16(fdout, tracks[i].startingBlock))
      return false;
    if (!write16(fdout, tracks[i].blockCount))
      return false;
    if (!write32(fdout, tracks[i].bitCount))
      return false;
  }

  // All the track data
  for (int i=0; i<160; i++) {
    // If we didn't preload, and the track isn't loaded, then load it now
    if (autoFlushTrackData && !tracks[i].trackData) {
      loadMissingTrackFromImage(i);
    }
    
    if (tracks[i].startingBlock &&
	tracks[i].blockCount) {
      if (lseek(fdout, tracks[i].startingBlock * 512, SEEK_SET) == -1) {
	fprintf(stderr, "Failed to seek before writing track\n");
	return false;
      }
      // Technically, we only have this many bytes to write:
      // uint32_t writeSize = (tracks[i].bitCount / 8) + ((tracks[i].bitCount % 8) ? 1 : 0);
      // ... but in practice, the tracks are all padded to NIBTRACKSIZE bytes;
      // and we alloc'd a buffer of that size, too; so write the whole thing,
      // since it would have been calloc'd initially.
      ssize_t numWritten = write(fdout, tracks[i].trackData, NIBTRACKSIZE);
      if (numWritten != NIBTRACKSIZE) {
	fprintf(stderr, "Failed to write track [%ld]\n", numWritten);
	perror("error writing");
	return false;
      }
#if 0      
      uint8_t c = 0;
      while (writeSize < tracks[i].blockCount * 512) {
	if (write(fdout, &c, 1) != 1)
	  return false;
	writeSize++;
      }
#endif
    }
  }
  return true;
}

bool Woz::decodeWozTrackToNibFromDataTrack(uint8_t dataTrack, nibSector sectorData[16])
{
  for (int sector=0; sector<16; sector++) {
    if (!readNibSectorDataFromDataTrack(dataTrack, sector, (nibSector *)(&sectorData[sector]))) {
      printf("Failed to read nib sector data for datatrack %d sector %d\n",
	     dataTrack, sector);
      return false;
    }
  }

  return true;
}

bool Woz::decodeWozTrackToDsk(uint8_t phystrack, uint8_t subtype, uint8_t sectorData[256*16])
{
  uint8_t dataTrack = quarterTrackMap[phystrack*4];
  // First read it to a NIB; then convert the NIB to a DSK.
  static nibSector nibData[16];
  if (!decodeWozTrackToNibFromDataTrack(dataTrack, nibData)) {
    printf("failed to decode to Nib\n");
    return false;
  }    

  nibErr ret = denibblizeTrack((const uint8_t *)nibData, sectorData, subtype, phystrack);
  if (ret != errorNone) {
    printf("Failed to denibblize track: %d\n", ret);
    return false;
  }

  return true;
}

bool Woz::checksumWozDataTrack(uint8_t datatrack, uint32_t *retCRC)
{
  if (!retCRC)
    return false;

  if (!tracks[datatrack].trackData) {
    *retCRC = 0;
    return false;
  }
  
  *retCRC = compute_crc_32(tracks[datatrack].trackData, tracks[datatrack].bitCount/8);
  return true;
}



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

  if (dumpflags & DUMP_QTMAP) {
    printf("Quarter-track map:\n");
    for (int i=0; i<140; i+=4) {
      printf("%2d     %3d => %3d     %3d => %3d     %3d => %3d     %3d => %3d\n",
	     i/4,
	     i, quarterTrackMap[i],
	     i+1, quarterTrackMap[i+1],
	     i+2, quarterTrackMap[i+2],
	     i+3, quarterTrackMap[i+3]);
    }
  }
  
  if (dumpflags & DUMP_QTCRC) {
    printf("Woz internal quarter-track CRCs:\n");
    // Dump the CRC32 for each Woz quarter-track
    for (int i=0 ;i<160; i++) {
      uint32_t crc=0;
      checksumWozDataTrack(i, &crc);
      printf("Woz track %d CRC32: 0x%X\n", i, crc);
    }
  }

  if (dumpflags & DUMP_TRACK) {
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
	if (dumpflags & DUMP_RAWTRACK) {
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
	}
	
	if (dumpflags & DUMP_SECTOR)  {
	  printf("    Sector dump:\n");
	  // Look at the sectors in numerical order
	  // FIXME: 13-sector support
	  nibSector sectorData;
	  for (int sector=0; sector<16; sector++) {
	    if (readNibSectorDataFromDataTrack(quarterTrackMap[i*4], sector, &sectorData)) {
	      printf("      Volume ID: %d\n", de44(sectorData.volume44));
	      printf("      Track ID: %d\n", de44(sectorData.track44));
	      uint8_t sector = de44(sectorData.sector44);
	      printf("      Sector: %d\n", sector);
	      printf("      Cksum: %d\n", de44(sectorData.checksum44));
	      
	      printf("      Sector Data:\n");
	      for (int k=0; k<342; k+=16) {
		printf("      0x%.4X :", k);
		for (int j=0; j<16; j++) {
		  if (k+j < 342) {
		    printf(" %.2X", sectorData.data62[k+j]);
		  }
		}
		printf("\n");
	      }
	    }
	  }
	}
      }
      
      if (dumpflags & DUMP_TOFILE) {
	// Dump each sector to a file for analysis
	uint8_t sectorData[256*16];
	decodeWozTrackToDsk(quarterTrackMap[i*4],
			    T_DSK,
			    sectorData);
	for (int j=0; j<16; j++) {
	  char buf[25];
	  sprintf(buf, "t%ds%d", i, j);
	  int fdout = open(buf, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
	  write(fdout, &sectorData[256*j], 256);
	  close(fdout);
	}
      }

      if (dumpflags & DUMP_ORDEREDSECTOR) {
#define denib(a, b) ((((a) & ~0xAA) << 1) | ((b) & ~0xAA))
	printf("    Track-ordered sector dump:\n");
	// Look at the sectors found in order on the track
	trackBitIdx = 0x80; trackPointer = 0; trackLoopCounter = 0;
	uint16_t sectorsFound = 0;
	do {
	  if (nextDiskByte(i) == 0xD5 &&
	      nextDiskByte(i) == 0xAA &&
	      nextDiskByte(i) == 0x96) {
	    printf("      Volume ID: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));
	    printf("      Track ID: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));
	    uint8_t sector = denib(nextDiskByte(i), nextDiskByte(i));
	    printf("      Sector: %d\n", sector);
	    sectorsFound |= (1 << sector);
	    printf("      Cksum: %d\n", denib(nextDiskByte(i), nextDiskByte(i)));
	    
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
	      printf("      Sector Data:\n");
	      for (int k=0; k<342; k+=16) {
		printf("      0x%.4X :", k);
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
      }
    }
  }
}

bool Woz::isSynchronized()
{
  return di.synchronized;
}

uint8_t Woz::dataTrackNumberForQuarterTrack(uint16_t qt)
{
  return quarterTrackMap[qt];
}

bool Woz::flush()
{
  return false;
}
