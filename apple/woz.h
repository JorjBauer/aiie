#ifndef __WOZ_H
#define __WOZ_H
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "nibutil.h"
#include "disktypes.h"

#define DUMP_TRACK         0x01
#define DUMP_QTMAP         0x02
#define DUMP_QTCRC         0x04
// these all require DUMP_TRACK:
#define DUMP_TOFILE        0x10
#define DUMP_SECTOR        0x20
#define DUMP_RAWTRACK      0x40
#define DUMP_ORDEREDSECTOR 0x80


typedef struct _diskInfo {
  uint8_t version;          // Woz format version #
  uint8_t diskType;         // 1 = 5.25"; 2 = 3.5"
  uint8_t writeProtected;   // 1 = true
  uint8_t synchronized;     // were tracks written with cross-track sync?
  uint8_t cleaned;          // were MC3470 "fake bits" removed?
  char creator[33];         // 32 chars of creator string and one terminator
  uint8_t diskSides;        // always 1 for a 5.25" disk
  uint8_t bootSectorFormat; // 0=Unknown; 1=16-sector; 2=13-sector; 3=both
  uint8_t optimalBitTiming; // 125-nS increments; standard == 32 (4uS)
  uint16_t compatHardware;  // 0=unknown compatability
  uint16_t requiredRam;     // value in K. 0 = unknown
  uint16_t largestTrack;    // # of 512-byte blocks used for largest track
} diskInfo;

typedef struct _trackInfo {
  uint16_t startingBlock; // v2
  uint32_t startingByte;  // v1
  uint16_t blockCount;
  uint32_t bitCount;
  uint8_t *trackData;
} trackInfo;

class Woz {
 public:
  Woz(bool verbose, uint8_t dumpflags);
  ~Woz();

  bool readFile(const char *filename, bool preloadTracks, uint8_t forceType = T_AUTO);
  bool writeFile(const char *filename, uint8_t forceType = T_AUTO);

  uint8_t getNextWozBit(uint8_t datatrack);

  void dumpInfo();

  bool isSynchronized();

  uint8_t dataTrackNumberForQuarterTrack(uint16_t qt);
  
  bool flush();

  //protected:
  // Interface for AiiE
  bool writeNextWozBit(uint8_t datatrack, uint8_t bit);
  bool writeNextWozByte(uint8_t datatrack, uint8_t b);
  uint8_t nextDiskBit(uint8_t datatrack);
  uint8_t nextDiskByte(uint8_t datatrack);
  bool skipByte(uint8_t datatrack);

 private:
  bool readWozFile(const char *filename, bool preloadTracks);
  bool readDskFile(const char *filename, bool preloadTracks, uint8_t subtype);
  bool readNibFile(const char *filename, bool preloadTracks);

  bool decodeWozTrackToNib(uint8_t phystrack, nibSector sectorData[16]);
  bool decodeWozTrackToDsk(uint8_t phystrack, uint8_t subtype, uint8_t sectorData[256*16]);

  bool writeWozFile(const char *filename, uint8_t subtype);
  bool writeDskFile(const char *filename, uint8_t subtype);
  bool writeNibFile(const char *filename);

  uint8_t fakeBit();

  bool parseTRKSChunk(uint32_t chunkSize);
  bool parseTMAPChunk(uint32_t chunkSize);
  bool parseInfoChunk(uint32_t chunkSize);
  bool parseMetaChunk(uint32_t chunkSize);

  bool writeInfoChunk(uint8_t version, int fdout);
  bool writeTMAPChunk(uint8_t version, int fdout);
  bool writeTRKSChunk(uint8_t version, int fdout);

  bool readWozDataTrack(uint8_t datatrack);
  bool readNibSectorData(uint8_t phystrack, uint8_t sector, nibSector *sectorData);

  bool loadMissingTrackFromImage(uint8_t datatrack);
  
  bool checksumWozDataTrack(uint8_t datatrack, uint32_t *retCRC);

  void _initInfo();

 private:
  int fd;
  uint8_t imageType;
  
  bool verbose;
  uint8_t dumpflags;

  bool autoFlushTrackData;
  bool trackDirty;
  
  uint8_t quarterTrackMap[40*4];
  diskInfo di;
  trackInfo tracks[160];

  // cursor for track enumeration
  uint32_t trackPointer;
  uint32_t trackBitCounter;
  uint8_t trackByte;
  uint8_t trackBitIdx;
  uint8_t trackLoopCounter;
  char *metaData;
  uint8_t randData, randPtr;
};

#endif
