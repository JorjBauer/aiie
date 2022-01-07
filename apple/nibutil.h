#ifndef __NIBUTIL_H
#define __NIBUTIL_H

#include <unistd.h>
#ifndef TEENSYDUINO
#include <fcntl.h>
#endif
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

typedef struct _bitPtr {
  uint16_t idx;
  uint8_t bitIdx;
} bitPtr;

  bool _decode62Data(const uint8_t trackBuffer[343], uint8_t output[256]);
  void _encode62Data(uint8_t *outputBuffer, const uint8_t input[256]);

uint32_t nibblizeTrack(uint8_t outputBuffer[NIBTRACKSIZE], const uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track);

nibErr denibblizeTrack(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType);

uint8_t de44(uint8_t nibs[2]);

nibErr nibblizeSector(uint8_t dataIn[256], uint8_t dataOut[343]);
nibErr denibblizeSector(nibSector input, uint8_t dataOut[256]);

#ifdef __cplusplus
};
#endif

#endif
