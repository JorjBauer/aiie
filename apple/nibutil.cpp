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

#define INCIDX(p) { p->bitIdx >>= 1; if (!p->bitIdx) {p->bitIdx = 0x80; p->idx++;} }

// This is the DOS 3.3 RWTS Write Table (UTA2E, p. 9-26).
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

// DOS 3.2 physical → logical sector. DOS 3.2 writes logical sector N to
// physical sector (N*5) mod 13, so the inverse mapping (physical → logical)
// is (phys*8) mod 13. Verified by catalog layout on DOS 3.2 System Master.
const static uint8_t dephys13[13] = {
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12 };

// 5-and-3 disk alphabet: the 32 permissible 8-bit patterns on a
// 13-sector Apple II floppy. Index i is the 5-bit logical value.
static const uint8_t _diskBytes53[32] = {
  0xAB, 0xAD, 0xAE, 0xAF, 0xB5, 0xB6, 0xB7, 0xBA,
  0xBB, 0xBD, 0xBE, 0xBF, 0xD6, 0xD7, 0xDA, 0xDB,
  0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xED, 0xEE, 0xEF,
  0xF5, 0xF6, 0xF7, 0xFA, 0xFB, 0xFD, 0xFE, 0xFF };

// Inverse table: disk byte → 5-bit value, or 0xFF if invalid. Built at
// startup from _diskBytes53 so we don't carry a second hand-maintained
// copy that could drift.
static uint8_t _invDiskBytes53[256];
static bool _inv53Ready = false;
static void _build53Inverse() {
  if (_inv53Ready) return;
  for (int i = 0; i < 256; i++) _invDiskBytes53[i] = 0xFF;
  for (int i = 0; i < 32; i++) _invDiskBytes53[_diskBytes53[i]] = (uint8_t)i;
  _inv53Ready = true;
}

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
  // A gap byte has two sync bits after it.
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
void _encode62Data(uint8_t outputBuffer[343], const uint8_t input[256])
{
  memset(outputBuffer, 0, 342);

  int idx2 = 0x55;
  for (int idx6 = 0x101; idx6 >= 0; idx6--) {
    int val6 = input[idx6 & 0xFF];
    int val2 = outputBuffer[idx2];

    val2 = (val2 << 1) | (val6 & 1);
    val6 >>= 1;
    val2 = (val2 << 1) | (val6 & 1);
    val6 >>= 1;

    outputBuffer[idx2] = val2;
    if (idx6 < 0x100) {
      outputBuffer[0x56 + idx6] = val6;
    }

    if (--idx2 < 0) {
      idx2 = 0x55;
    }
  }

  // mask out the "extra" 2-nyb data from above. Note that the Apple
  // decoders don't care about the extra bits, so taking these back
  // out isn't operationally important. Just don't overflow
  // _trans[]...
  outputBuffer[0x54] &= 0x0F;
  outputBuffer[0x55] &= 0x0F;

  int lastv = 0;
  for (int idx = 0; idx < 0x156; idx++) {
    int val = outputBuffer[idx];
    outputBuffer[idx] = _trans[lastv^val];
    lastv = val;
  }
  outputBuffer[342] = _trans[lastv];
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

    uint8_t nibData[343];
    _encode62Data(nibData, &rawTrackBuffer[physicalSector * 256]);
    for (int i=0; i<343; i++) {
      _packByte(outputBuffer, &ptr, nibData[i]);
    }

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
bool _decode62Data(const uint8_t trackBuffer[343], uint8_t output[256])
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

// ----- 5-and-3 codec ------------------------------------------------
//
// Layout of an encoded sector body (411 bytes):
//   [0..153]   aux bytes  (low 3 bits of each input byte, packed backwards)
//   [154..409] primary    (high 5 bits of each input byte)
//   [410]      checksum
//
// Both aux and primary are written through a running-XOR: each disk value
// equals (previous-decoded ^ this-decoded). The first aux byte XORs with
// the seed (always 0 on stock DOS). The receiver recovers the original
// 5-bit value by XORing back against the running checksum.
//
// The aux section packs 51 "groups of 5 input bytes" into 51 triplets.
// For a group (byteA, byteB, byteC, byteD, byteE), their low 3 bits are
// split across three aux entries (at indices i, 51+i, 102+i):
//   aux[i]      = (A_low3 << 2) | ((D_low3 & 0x4) >> 1) | ((E_low3 & 0x4) >> 2)
//   aux[51+i]   = (B_low3 << 2) | ( D_low3 & 0x2)       | ((E_low3 & 0x2) >> 1)
//   aux[102+i]  = (C_low3 << 2) | ((D_low3 & 0x1) << 1) | ( E_low3 & 0x1)
// The 154th aux byte carries the low 3 bits of the 256th input byte
// directly (no packing — it's a leftover).

void _encode53Data(uint8_t outputBuffer[411], const uint8_t input[256])
{
  _build53Inverse(); // harmless here; keeps both paths self-initializing

  uint8_t tops[256];      // high-5-bit values
  uint8_t threes[154];    // packed aux values

  // Fill tops[] in the layout the decoder will read (reversed per group).
  const int CHUNK = 51;
  for (int g = 0; g < CHUNK; g++) {
    int chunk = (CHUNK - 1) - g;
    uint8_t A = input[g*5 + 0], B = input[g*5 + 1], C = input[g*5 + 2];
    uint8_t D = input[g*5 + 3], E = input[g*5 + 4];

    tops[chunk + CHUNK*0] = A >> 3;
    tops[chunk + CHUNK*1] = B >> 3;
    tops[chunk + CHUNK*2] = C >> 3;
    tops[chunk + CHUNK*3] = D >> 3;
    tops[chunk + CHUNK*4] = E >> 3;

    uint8_t a = A & 0x07, b = B & 0x07, c = C & 0x07;
    uint8_t d = D & 0x07, e = E & 0x07;
    threes[chunk + CHUNK*0] = (uint8_t)((a << 2) | ((d & 0x4) >> 1) | ((e & 0x4) >> 2));
    threes[chunk + CHUNK*1] = (uint8_t)((b << 2) | ( d & 0x2)       | ((e & 0x2) >> 1));
    threes[chunk + CHUNK*2] = (uint8_t)((c << 2) | ((d & 0x1) << 1) | ( e & 0x1));
  }

  // Straggler: the 256th input byte has no group companions.
  tops[255]   = input[255] >> 3;
  threes[153] = input[255] & 0x07;

  // Emit through the running-XOR: first aux (reverse order), then primary,
  // then final checksum nibble so the receiver can verify.
  int chk = 0;
  for (int i = 153; i >= 0; i--) {
    outputBuffer[153 - i] = _diskBytes53[threes[i] ^ chk];
    chk = threes[i];
  }
  for (int i = 0; i < 256; i++) {
    outputBuffer[154 + i] = _diskBytes53[tops[i] ^ chk];
    chk = tops[i];
  }
  outputBuffer[410] = _diskBytes53[chk];
}

bool _decode53Data(const uint8_t trackBuffer[411], uint8_t output[256])
{
  _build53Inverse();

  uint8_t threes[154];    // aux 5-bit values, post-XOR
  uint8_t tops[256];      // primary 5-bit values, post-XOR
  uint8_t chk = 0;

  // Aux section first — the encoder wrote it reversed, so index 153 down.
  for (int i = 153; i >= 0; i--) {
    uint8_t v = _invDiskBytes53[trackBuffer[153 - i]];
    if (v == 0xFF) return false;
    chk ^= v;
    threes[i] = chk;
  }
  for (int i = 0; i < 256; i++) {
    uint8_t v = _invDiskBytes53[trackBuffer[154 + i]];
    if (v == 0xFF) return false;
    chk ^= v;
    tops[i] = chk;
  }
  uint8_t trailer = _invDiskBytes53[trackBuffer[410]];
  if (trailer == 0xFF || trailer != chk) return false;

  // Rebuild the 255 five-byte groups in reverse order (matching how they
  // were laid out during encode). See the header comment for bit layout.
  const int CHUNK = 51;
  int outIdx = 0;
  for (int g = CHUNK - 1; g >= 0; g--) {
    uint8_t t1 = threes[g];
    uint8_t t2 = threes[CHUNK + g];
    uint8_t t3 = threes[CHUNK*2 + g];

    uint8_t d = (uint8_t)(((t1 & 0x02) << 1) | (t2 & 0x02) | ((t3 & 0x02) >> 1));
    uint8_t e = (uint8_t)(((t1 & 0x01) << 2) | ((t2 & 0x01) << 1) | (t3 & 0x01));

    output[outIdx++] = (uint8_t)((tops[g]            << 3) | ((t1 >> 2) & 0x07));
    output[outIdx++] = (uint8_t)((tops[CHUNK + g]    << 3) | ((t2 >> 2) & 0x07));
    output[outIdx++] = (uint8_t)((tops[CHUNK*2 + g]  << 3) | ((t3 >> 2) & 0x07));
    output[outIdx++] = (uint8_t)((tops[CHUNK*3 + g]  << 3) | (d & 0x07));
    output[outIdx++] = (uint8_t)((tops[CHUNK*4 + g]  << 3) | (e & 0x07));
  }
  // Straggler byte.
  output[outIdx++] = (uint8_t)((tops[255] << 3) | (threes[153] & 0x07));
  return true;
}

// 13-sector track denibblizer. Largely parallel to denibblizeTrack but
// keyed off the DOS 3.2 address prologue (D5 AA B5) and using the 5-and-3
// sector-data codec. Data prologue is still D5 AA AD.
nibErr denibblizeTrack13(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*13])
{
  uint16_t sectorsUpdated = 0;

  // Scan the track twice so a sector that wraps the end/start still
  // matches. The upper bound generously covers 13 sectors * ~510 disk
  // bytes/sector at max.
  for (uint32_t i = 0; i < 2 * NIBTRACKSIZE; i++) {
    if (input[i % NIBTRACKSIZE] != 0xD5) continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA) continue;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xB5) continue;
    i++;

    // 4-and-4 header: volume, track, sector, checksum.
    uint8_t volumeID = denib(input[i % NIBTRACKSIZE], input[(i+1) % NIBTRACKSIZE]); i += 2;
    uint8_t trackID  = denib(input[i % NIBTRACKSIZE], input[(i+1) % NIBTRACKSIZE]); i += 2;
    uint8_t sectorNum= denib(input[i % NIBTRACKSIZE], input[(i+1) % NIBTRACKSIZE]); i += 2;
    uint8_t hdrSum   = denib(input[i % NIBTRACKSIZE], input[(i+1) % NIBTRACKSIZE]); i += 2;
    if (hdrSum != (volumeID ^ trackID ^ sectorNum)) continue;

    // Address epilogue: DE AA EB (last byte only partially checked to
    // match 6-and-2 tolerance above).
    if (input[i % NIBTRACKSIZE] != 0xDE) continue; i++;
    if (input[i % NIBTRACKSIZE] != 0xAA) continue; i++;

    // Skip gap to data prologue.
    while (input[i % NIBTRACKSIZE] != 0xD5) i++;
    i++;
    if (input[i % NIBTRACKSIZE] != 0xAA) continue; i++;
    if (input[i % NIBTRACKSIZE] != 0xAD) continue; i++;

    // Pull the 411 data bytes into a contiguous buffer (track may wrap).
    uint8_t nibData[411];
    for (int j = 0; j < 411; j++) {
      nibData[j] = input[(i + j) % NIBTRACKSIZE];
    }
    uint8_t sectorOut[256];
    if (!_decode53Data(nibData, sectorOut)) return errorBadData;
    i += 411;

    if (input[i % NIBTRACKSIZE] != 0xDE) continue; i++;
    if (input[i % NIBTRACKSIZE] != 0xAA) continue; i++;
    if (input[i % NIBTRACKSIZE] != 0xEB) continue; i++;

    if (sectorNum >= 13) return errorBadData;
    uint8_t logicalSector = dephys13[sectorNum];
    memcpy(&rawTrackBuffer[logicalSector * 256], sectorOut, 256);
    sectorsUpdated |= (uint16_t)(1 << sectorNum);
    // Once every sector is accounted for, stop scanning. Otherwise a
    // stray D5 AA B5 pattern that appears in gap data (or a misaligned
    // re-match on the wraparound pass) can send us down a false data
    // prologue and produce spurious decode errors.
    if (sectorsUpdated == 0x1FFF) break;
  }

  return (sectorsUpdated == 0x1FFF) ? errorNone : errorMissingSectors;
}

// trackBuffer is input NIB data; rawTrackBuffer is output DSK/PO data
nibErr denibblizeTrack(const uint8_t input[NIBTRACKSIZE], uint8_t rawTrackBuffer[256*16],
		       uint8_t diskType)
{
  // bitmask of the sectors that we've found while decoding. We should
  // find all 16.
  uint16_t sectorsUpdated = 0;

  // loop through the data twice, so we make sure we read anything 
  // that crosses the end/start boundary
  for (uint16_t i=0; i<2*416*16; i++) {
    // Find the prolog
    if (input[i % NIBTRACKSIZE] != 0xD5)
      continue;
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
    if (!_decode62Data(nibData, output)) {
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

nibErr nibblizeSector(uint8_t dataIn[256], uint8_t dataOut[343])
{
  _encode62Data(dataOut, dataIn);

  return errorNone;
}

nibErr denibblizeSector(nibSector input, uint8_t dataOut[256])
{
  if (_decode62Data((uint8_t *)(input.data62), dataOut)) {
    return errorNone;
  }

  return errorBadData;
}
