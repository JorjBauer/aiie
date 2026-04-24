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

  // 5-and-3 (DOS 3.2 / 13-sector) sector codec. 256 bytes of data encode
  // to 411 disk bytes: 154 "aux" bytes holding the low 3 bits of every
  // input byte, 256 "primary" bytes holding the high 5 bits, and one
  // running-XOR checksum byte. The 32-value disk alphabet is fixed by
  // the Apple disk controller's "no two adjacent zero bits, high bit
  // set" constraints.
  bool _decode53Data(const uint8_t trackBuffer[411], uint8_t output[256]);
  void _encode53Data(uint8_t outputBuffer[411], const uint8_t input[256]);

uint32_t nibblizeTrack(uint8_t outputBuffer[NIBTRACKSIZE], const uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track);

nibErr denibblizeTrack(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType);

// 13-sector variant: decodes a 13-sector DOS 3.2 track. rawTrackBuffer
// should be 256*13 bytes. Uses D5 AA B5 address prologue and the 5-and-3
// codec for sector data.
nibErr denibblizeTrack13(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*13]);

uint8_t de44(uint8_t nibs[2]);

nibErr nibblizeSector(uint8_t dataIn[256], uint8_t dataOut[343]);
nibErr denibblizeSector(nibSector input, uint8_t dataOut[256]);

#ifdef __cplusplus
};
#endif

#endif
