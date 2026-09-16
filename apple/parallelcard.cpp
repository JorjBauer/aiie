#include "parallelcard.h"
#include <string.h>

// The slot ROM, assembled from apple/parallelrom.s into parallel.rom and
// packaged here by util/genrom.pl. The image is the same for every slot;
// loadROM fills in the two bytes that depend on which slot the card is in.
#include "parallel-rom.h"
#include "fx80.h"
#include "globals.h"

// The two immediates in the ROM's output hook that hold the slot: "ldx #$Cn"
// at $Cn06 and "ldy #$n0" at $Cn08. The source pins them with .assert, and
// loadROM checks the opcodes before touching anything.
#define PARROM_LDX_SLOT 0x06
#define PARROM_LDY_SLOT 0x08

#ifdef TEENSYDUINO
#include "teensy-println.h"
#endif

ParallelCard::ParallelCard()
{
  fx80 = new Fx80();
}

ParallelCard::~ParallelCard()
{
}

bool ParallelCard::Serialize(int8_t fd)
{
  return true;
}

bool ParallelCard::Deserialize(int8_t fd)
{
  return true;
}

void ParallelCard::Reset()
{
}

uint8_t ParallelCard::readSwitches(uint8_t s)
{
  return 0x7F; // bit 7 clear = printer ready
}

void ParallelCard::writeSwitches(uint8_t s, uint8_t v)
{
  if (s == 0x00) {
    fx80->input(v);
  } else {
    //    printf("unknown switch 0x%X\n", s);
  }
}

void ParallelCard::loadROM(uint8_t *toWhere)
{
#ifdef TEENSYDUINO
  println("loading parallel slot rom");
  for (uint16_t i=0; i<=0xFF; i++) {
    toWhere[i] = pgm_read_byte(&romData[i]);
  }
#else
  printf("loading parallel slot rom\n");
  memcpy(toWhere, romData, 256);
#endif

  // The output hook is entered without X or Y set, and the ROM has no other
  // way to learn its slot: fill it in.
  if (toWhere[PARROM_LDX_SLOT] != 0xA2 || toWhere[PARROM_LDY_SLOT] != 0xA0) {
#ifdef TEENSYDUINO
    println("Parallel ROM layout mismatch; not patching the slot");
#else
    fprintf(stderr, "Parallel ROM layout mismatch; not patching the slot\n");
#endif
    return;
  }
  toWhere[PARROM_LDX_SLOT + 1] = 0xC0 + g_slotParallel;
  toWhere[PARROM_LDY_SLOT + 1] = g_slotParallel << 4;
}
