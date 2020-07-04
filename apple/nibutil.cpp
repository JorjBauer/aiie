#include "nibutil.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "disktypes.h"

// Default disk volume identifier
#define DISK_VOLUME 254

// 4-and-4 encoding handlers
#define         nib1(a) (((a & 0xAA) >> 1) | 0xAA)
#define         nib2(b) (((b & 0x55)     ) | 0xAA)
#define denib(a, b) ((((a) & ~0xAA) << 1) | ((b) & ~0xAA))

// In 6-and-2 encoding, there are 86 (0x56) 6-bit values
#define SIXBIT_SPAN 0x56

typedef struct _bitPtr {
  uint16_t idx;
  uint8_t bitIdx;
} bitPtr;

#define INCIDX(p) { p->bitIdx >>= 1; if (!p->bitIdx) {p->bitIdx = 0x80; p->idx++;} }

const static uint8_t _trans[64] = {0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
                                   0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
                                   0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
                                   0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
                                   0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
                                   0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
                                   0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
                                   0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};

// This is the inverted DOS 3.3 RWTS Write Table (high bit                                                                                                   
// stripped). Any "bad" value is stored as 0xFF.                                                                                                             
const static uint8_t _detrans[0x80] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04,
                                       0xFF, 0xFF, 0x08, 0x0C, 0xFF, 0x10, 0x14, 0x18,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1C, 0x20,
                                       0xFF, 0xFF, 0xFF, 0x24, 0x28, 0x2C, 0x30, 0x34,
                                       0xFF, 0xFF, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C,
                                       0xFF, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0x6C, 0xFF, 0x70, 0x74, 0x78,
                                       0xFF, 0xFF, 0xFF, 0x7C, 0xFF, 0xFF, 0x80, 0x84,
                                       0xFF, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C, 0xA0,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xA4, 0xA8, 0xAC,
                                       0xFF, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8,
                                       0xFF, 0xFF, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0,
                                       0xFF, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC };

// dos 3.3 to physical sector conversion
const static uint8_t dephys[16] = {
  0x00, 0x07, 0x0e, 0x06, 0x0d, 0x05, 0x0c, 0x04,
  0x0b, 0x03, 0x0a, 0x02, 0x09, 0x01, 0x08, 0x0f };

// Prodos to physical sector conversion
const uint8_t deProdosPhys[] = {
  0x00, 0x08, 0x01, 0x09, 0x02, 0x0a, 0x03, 0x0b,
  0x04, 0x0c, 0x05, 0x0d, 0x06, 0x0e, 0x07, 0x0f };

uint8_t de44(uint8_t nibs[2])
{
  return denib(nibs[0], nibs[1]);
}

static void _packBit(uint8_t *output, bitPtr *ptr, uint8_t isOn)
{
  if (isOn)
    output[ptr->idx] |= ptr->bitIdx;
  INCIDX(ptr);
}

static void _packGap(uint8_t *output, bitPtr *ptr)
{
  for (int i=0; i<8; i++) 
    _packBit(output, ptr, 1);
  _packBit(output, ptr, 0);
  _packBit(output, ptr, 0);
}

static void _packByte(uint8_t *output, bitPtr *ptr, uint8_t v)
{
  for (int i=0; i<8; i++) {
    _packBit(output, ptr, v & (1 << (7-i)));
  }
}

// Take 256 bytes of input and turn it in to 343 bytes of nibblized output
static void _encodeData(uint8_t *outputBuffer, bitPtr *ptr, const uint8_t input[256])
{
  int ptr2 = 0;
  int ptr6 = 0x56;
  static int nibbles[0x156];

  memset(nibbles, 0, sizeof(nibbles));

  int idx2 = 0x55;
  for (int idx6 = 0x101; idx6 >= 0; idx6--) {
    int val6 = input[idx6 & 0xFF];
    int val2 = nibbles[ptr2 + idx2];

    val2 = (val2 << 1) | (val6 & 1);
    val6 >>= 1;
    val2 = (val2 << 1) | (val6 & 1);
    val6 >>= 1;

    // There are 2 "extra" bytes of 2-bit data that we add in here.
    if (ptr6 + idx6 < 0x156) {
      nibbles[ptr6 + idx6] = val6;
    }
    if (ptr2 + idx2 < 0x156) {
      nibbles[ptr2 + idx2] = val2;
    }

    if (--idx2 < 0) {
      idx2 = 0x55;
    }
  }
  // mask out the "extra" 2-bit data above. Note that the Apple decoders
  // don't care about the extra bits, so taking these back out isn't 
  // operationally important.
  nibbles[0x54] &= 0x0F;
  nibbles[0x55] &= 0x0F;

  int lastv = 0;
  for (int idx = 0; idx < 0x156; idx++) {
    int val = nibbles[idx];
    _packByte(outputBuffer, ptr, _trans[lastv ^ val]);
    lastv = val;
  }
  _packByte(outputBuffer, ptr, _trans[lastv]);
}

static uint8_t _whichBit(uint8_t bitIdx)
{
  switch (bitIdx) {
  case 0x80:
    return 0;
  case 0x40:
    return 1;
  case 0x20:
    return 2;
  case 0x10:
    return 3;
  case 0x08:
    return 4;
  case 0x04:
    return 5;
  case 0x02:
    return 6;
  case 0x01: 
    return 7;
  default:
    return 0; // not used
  }
  /* NOTREACHED */
}

// rawTrackBuffer is input (dsk/po format); outputBuffer is encoded
// nibbles (416*16 bytes). Returns the number of bits actually
// encoded.
uint32_t nibblizeTrack(uint8_t outputBuffer[NIBTRACKSIZE], const uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track)
{
  int checksum;
  bitPtr ptr = { 0, 0x80 };

  for (uint8_t sector=0; sector<16; sector++) {

    for (uint8_t i=0; i<16; i++) {
      _packGap(outputBuffer, &ptr);
    }

    _packByte(outputBuffer, &ptr, 0xD5); // prolog
    _packByte(outputBuffer, &ptr, 0xAA);
    _packByte(outputBuffer, &ptr, 0x96);
    
    _packByte(outputBuffer, &ptr, nib1(DISK_VOLUME));
    _packByte(outputBuffer, &ptr, nib2(DISK_VOLUME));

    _packByte(outputBuffer, &ptr, nib1(track));
    _packByte(outputBuffer, &ptr, nib2(track));
    
    _packByte(outputBuffer, &ptr, nib1(sector));
    _packByte(outputBuffer, &ptr, nib2(sector));
    
    checksum = DISK_VOLUME ^ track ^ sector;
    _packByte(outputBuffer, &ptr, nib1(checksum));
    _packByte(outputBuffer, &ptr, nib2(checksum));

    _packByte(outputBuffer, &ptr, 0xDE); // epilog
    _packByte(outputBuffer, &ptr, 0xAA);
    _packByte(outputBuffer, &ptr, 0xEB);
    
    for (uint8_t i=0; i<5; i++) {
      _packGap(outputBuffer, &ptr);
    }
    
    _packByte(outputBuffer, &ptr, 0xD5); // data prolog
    _packByte(outputBuffer, &ptr, 0xAA);
    _packByte(outputBuffer, &ptr, 0xAD);
    
    uint8_t physicalSector = (diskType == T_PO ? deProdosPhys[sector] : dephys[sector]);
    _encodeData(outputBuffer, &ptr, &rawTrackBuffer[physicalSector * 256]);

    _packByte(outputBuffer, &ptr, 0xDE); // data epilog
    _packByte(outputBuffer, &ptr, 0xAA);
    _packByte(outputBuffer, &ptr, 0xEB);

    for (uint8_t i=0; i<16; i++) {
      _packGap(outputBuffer, &ptr);
    }
  }

  return (ptr.idx*8 + _whichBit(ptr.bitIdx));
}

// Pop the next 343 bytes off of trackBuffer, which should be 342
// 6:2-bit GCR encoded values, which we decode back in to 256 8-byte
// output values; and one checksum byte.
//
// Return true if we've successfully consumed 343 bytes from
// trackBuf.
static bool _decodeData(const uint8_t trackBuffer[343], uint8_t output[256])
{
  static uint8_t workbuf[342];

  for (int i=0; i<342; i++) {
    uint8_t in = *(trackBuffer++) & 0x7F; // strip high bit
    workbuf[i] = _detrans[in];
    if (workbuf[i] == 0xFF) // bad data is untranslatable
      return false;
  }

  // fixme: collapse this in to the previous loop
  uint8_t prev = 0;
  for (int i=0; i<342; i++) {
    workbuf[i] = prev ^ workbuf[i];
    prev = workbuf[i];
  }

#if 0
  if (prev != trackBuffer[342]) {
    printf("ERROR: checksum of sector is incorrect [0x%X v 0x%X]\n", prev, trackBuffer[342]);
    return false;
  }
#endif

  // Start with all of the bytes with 6 bits of data
  for (uint16_t i=0; i<256; i++) {
    output[i] = workbuf[SIXBIT_SPAN + i] & 0xFC; // 6 bits
  }

  // Then pull in all of the 2-bit values, which are stuffed 3 to a byte. That gives us
  // 4 bits more than we need - the last two skip two of the bits.
  for (uint8_t i=0; i<SIXBIT_SPAN; i++) {
    // This byte (workbuf[i]) has 2 bits for each of 3 output bytes:
    //     i, SIXBIT_SPAN+i, and 2*SIXBIT_SPAN+i
    uint8_t thisbyte = workbuf[i];
    output[                i] |= ((thisbyte & 0x08) >> 3) | ((thisbyte & 0x04) >> 1);
    output[  SIXBIT_SPAN + i] |= ((thisbyte & 0x20) >> 5) | ((thisbyte & 0x10) >> 3);
    if (i < SIXBIT_SPAN-2) {
      output[2*SIXBIT_SPAN + i] |= ((thisbyte & 0x80) >> 7) | ((thisbyte & 0x40) >> 5);
    }
  }

  return true;
}

// trackBuffer is input NIB data; rawTrackBuffer is output DSK/PO data
nibErr denibblizeTrack(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType, int8_t track)
{
  // bitmask of the sectors that we've found while decoding. We should
  // find all 16.
  uint16_t sectorsUpdated = 0;

  // loop through the data twice, so we make sure we read anything 
  // that crosses the end/start boundary
  //  uint16_t startOfSector;
  for (uint16_t i=0; i<2*416*16; i++) {
    // Find the prolog
    if (input[i % NIBTRACKSIZE] != 0xD5)
      continue;
    //    startOfSector = i;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA)
      continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0x96)
      continue;
    i++;

    // And now we should be in the header section
    uint8_t volumeID = denib(input[i     % NIBTRACKSIZE],
			     input[(i+1) % NIBTRACKSIZE]);
    i += 2;
    uint8_t trackID = denib(input[i     % NIBTRACKSIZE],
			    input[(i+1) % NIBTRACKSIZE]);
    i += 2;
    uint8_t sectorNum = denib(input[i     % NIBTRACKSIZE],
			      input[(i+1) % NIBTRACKSIZE]);
    i += 2;
    uint8_t headerChecksum = denib(input[i     % NIBTRACKSIZE],
				   input[(i+1) % NIBTRACKSIZE]);
    i += 2;

    if (headerChecksum != (volumeID ^ trackID ^ sectorNum)) {
      continue;
    }

    // check for the epilog
    if (input[i % NIBTRACKSIZE] != 0xDE) {
      continue;
    }
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA) {
      continue;
    }
    i++;

    // Skip to the data prolog
    while (input[i % NIBTRACKSIZE] != 0xD5) {
      i++;
    }
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA)
      continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAD)
      continue;
    i++;


    // Decode the data in to a temporary buffer: we don't want to overwrite 
    // something valid with partial data
    uint8_t output[256];
    // create a new nibData (in case it wraps around our track data)
    uint8_t nibData[343];
    for (int j=0; j<343; j++) {
      nibData[j] = input[(i+j)%NIBTRACKSIZE];
    }
    if (!_decodeData(nibData, output)) {
      return errorBadData;
    }
    i += 343;

    // Check the data epilog
    if (input[i % NIBTRACKSIZE] != 0xDE)
      continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA)
      continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xEB)
      continue;
    i++;

    // We've got a whole block! Put it in the rawTrackBuffer and mark
    // the bit for it in sectorsUpdated.

    // FIXME: if trackID != curTrack, that's an error?

    uint8_t targetSector;
    if (diskType == T_PO) {
      targetSector = deProdosPhys[sectorNum];
    } else { 
      targetSector = dephys[sectorNum];
    } 

    if (targetSector > 16)
      return errorBadData;

    memcpy(&rawTrackBuffer[targetSector * 256],
	   output,
	   256);
    sectorsUpdated |= (1 << sectorNum);
  }

  // Check that we found all of the sectors for this track
  if (sectorsUpdated != 0xFFFF) {
    return errorMissingSectors;
  }
  
  return errorNone;
}

