#include "parallelsram.h"

// Assumes any Output Enable pin is hardwired-enabled;
//         any Chip Enable pin is hardwared-enabled.
//
// Uses the low 8 bits of Port D as I/O lines (2, 14, 7, 8, 6, 20, 21, 5).
// 
// R/W (aka WriteEnable) is on pin 31.

#define RAM_RW 34

// The Address pins (19 of them). It would be nice to have these
// easily bitwise-manipulable, instead of having to set each bit
// individually.
//
// We can use 12 bits of Port C: 15, 22, 23, 9, 10, 13, 11, 12, 35, 36, 37, 38
// And then 6 bits of Port B: 16 17 19 18 49 50
//
// And hard wire one bit low (we don't need all 19 lines). That gets us 
// 256 Kb of RAM which should be sufficient.

static uint8_t addrPins[] = { 15, 22, 23, 9, 10, 13, 11, 12, 35, 36, 37, 38,
			      16, 17, 19, 18, 49, 50
};

#if 0
#define DELAY { delayMicroseconds(1); /* overkill, but useful for debugging */ }
#else
#define DELAY { __asm__ volatile ("nop"); __asm__ volatile ("nop"); \
__asm__ volatile ("nop"); __asm__ volatile ("nop"); \
__asm__ volatile ("nop"); __asm__ volatile ("nop"); \
__asm__ volatile ("nop"); __asm__ volatile ("nop"); \
 }
#endif

#define OE_ON { /*if (noe != 255) {digitalWrite(noe, LOW);}*/ }
#define OE_OFF { /*if (noe != 255) {digitalWrite(noe, HIGH);}*/ }

#define CE_ON { /*if (n_ce != 255) {digitalWrite(n_ce, LOW);} if (p_ce != 255) { digitalWrite(p_ce, HIGH); }*/ }
#define CE_OFF { /*if (n_ce != 255) {digitalWrite(n_ce, HIGH);} if (p_ce != 255) { digitalWrite(p_ce, LOW); }*/ }

#define WE_ON { digitalWriteFast(RAM_RW, LOW); }
#define WE_OFF { digitalWriteFast(RAM_RW, HIGH); }

ParallelSRAM::ParallelSRAM()
{
  pinMode(RAM_RW, OUTPUT);

  // Port D is our I/O port. Use the AVR emulation layer to set up the
  // pins once, and then we'll just fiddle with the DDR, input, and
  // output directly.

  // Enable it as a digital port...
  //  SIM_SCGC5 |= SIM_SCGC5_PORTD;
  //... what else? How do we set PORTD_PCR[0-7]?

  pinMode(2, INPUT);
  pinMode(14, INPUT);
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  pinMode(6, INPUT);
  pinMode(20, INPUT);
  pinMode(21, INPUT);
  pinMode(5, INPUT);
  isInput = true;

  // Set up the address pins
  for (int i=0; i<sizeof(addrPins); i++) {
    pinMode(addrPins[i], INPUT); // disable pull-ups
    pinMode(addrPins[i], OUTPUT);
    digitalWrite(addrPins[i], LOW);
  }

}

void ParallelSRAM::SetPins()
{
  pinMode(RAM_RW, OUTPUT);

  // Port D is our I/O port. Use the AVR emulation layer to set up the
  // pins once, and then we'll just fiddle with the DDR, input, and
  // output directly.

  // Enable it as a digital port...
  //  SIM_SCGC5 |= SIM_SCGC5_PORTD;
  //... what else? How do we set PORTD_PCR[0-7]?

  pinMode(2, INPUT);
  pinMode(14, INPUT);
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  pinMode(6, INPUT);
  pinMode(20, INPUT);
  pinMode(21, INPUT);
  pinMode(5, INPUT);
  isInput = true;

  // Set up the address pins
  for (int i=0; i<sizeof(addrPins); i++) {
    pinMode(addrPins[i], INPUT); // disable pull-ups
    pinMode(addrPins[i], OUTPUT);
    digitalWrite(addrPins[i], LOW);
  }
}

ParallelSRAM::~ParallelSRAM()
{
}

uint8_t ParallelSRAM::read(uint32_t addr)
{
  cli();
  // Read cycle 2
  setAddress(addr);

  // make sure address is valid before CE is asserted
  DELAY;

  CE_ON;
  OE_ON;

  DELAY;

  uint8_t ret = getInput();

  // Optional; can leave these lines asserted...
  OE_OFF;
  CE_OFF;

  sei();
  return ret;
}

void ParallelSRAM::write(uint32_t addr, uint8_t v)
{
  cli();
  setAddress(addr);

  DELAY;

  WE_ON;
  CE_ON;

  setOutput(v);

  DELAY;

  CE_OFF;
  WE_OFF;
  sei();
}

uint8_t ParallelSRAM::getInput()
{
  if (!isInput) {
#if 1
    // Directly set the direction bits. The rest of the port setup
    // should be fine from the initial config.
    *(volatile uint8_t *)(&GPIOD_PDDR) = 0x00; // inputs
#else
    pinMode(2, INPUT);
    pinMode(14, INPUT);
    pinMode(7, INPUT);
    pinMode(8, INPUT);
    pinMode(6, INPUT);
    pinMode(20, INPUT);
    pinMode(21, INPUT);
    pinMode(5, INPUT);
#endif
    isInput = true;
  }

  return GPIOD_PDIR & 0xFF;
}

void ParallelSRAM::setOutput(uint8_t v)
{
  // FIXME: is there a faster way to do this?
  if (isInput) {
#if 1
    // FIMXE: would this be correct?
    *(volatile uint8_t *)(&GPIOD_PDDR) |= 0xFF; // outputs
#else
    pinMode(2, OUTPUT);
    pinMode(14, OUTPUT);
    pinMode(7, OUTPUT);
    pinMode(8, OUTPUT);
    pinMode(6, OUTPUT);
    pinMode(20, OUTPUT);
    pinMode(21, OUTPUT);
    pinMode(5, OUTPUT);
#endif
    isInput = false;
  }

  // Directly set the low 8 bits of D.
  *(volatile uint8_t *)(&GPIOD_PDOR) = v;
}

void ParallelSRAM::setAddress(uint32_t addr)
{
  // The low 12 bits of the address go right in to Port C. Set these
  // by doing a clear of the bitmask, and then set the bits...
  GPIOC_PCOR = 0x00000FFF;
  GPIOC_PSOR = (addr & 0xFFF);

  // The high 6 bits of the address go in to Port B, bits 0..5.
  // We do that the same way...
  GPIOB_PCOR = 0x0000003F;
  GPIOB_PSOR = (addr >> 12);

#if 0
  for (uint8_t i=0; i<sizeof(addrPins); i++) {
    digitalWrite(addrPins[i], 
		 addr & (1 << i) ? HIGH : LOW);
  }
#endif
}
