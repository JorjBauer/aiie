#ifndef __NIBUTIL_H
#define __NIBUTIL_H

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIBTRACKSIZE 0x1A00
#define NIBSECTORSIZE 416

typedef struct _nibSector {
  uint8_t gap1[48];

  uint8_t sectorProlog[3];
  uint8_t volume44[2];
  uint8_t track44[2];
  uint8_t sector44[2];
  uint8_t checksum44[2];
  uint8_t sectorEpilog[3];

  uint8_t gap2[5];

  uint8_t dataProlog[3];
  uint8_t data62[342];
  uint8_t checksum;
  uint8_t dataEpilog[3];
} nibSector;

enum nibErr {
  errorNone           = 0,
  errorMissingSectors = 1,
  errorBadData        = 2
};

uint32_t nibblizeTrack(uint8_t outputBuffer[NIBTRACKSIZE], const uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track);

nibErr denibblizeTrack(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track);

uint8_t de44(uint8_t nibs[2]);

#ifdef __cplusplus
};
#endif

#endif
