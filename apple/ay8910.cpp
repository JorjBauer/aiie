#include "ay8910.h"
#include <stdio.h>

#include "globals.h"

AY8910::AY8910()
{
  Reset();
}

void AY8910::Reset()
{
  printf("AY8910 reset\n");
  curRegister = 0;
  for (uint8_t i=0; i<16; i++)
    r[i] = 0xFF;
  waveformFlipTimer[0] = waveformFlipTimer[1] = waveformFlipTimer[2] = 0;
  outputState[0] = outputState[1] = outputState[2] = 0;
}

uint8_t AY8910::read(uint8_t reg)
{
  // FIXME: does anything ever need to read from this?
  return 0xFF;
}

// reg represents BC1, BDIR, /RST in bits 0, 1, 2.
// val is the state of those three bits.
// PortA is the state of whatever's currently on PortA when we do it.
void AY8910::write(uint8_t reg, uint8_t PortA)
{
  // Bit 2 (1 << 2 == 0x04) is wired to the Reset pin. If it goes low,
  // we reset the virtual chip.
  if ((reg & 0x04) == 0) {
    Reset();
    return;
  }

  // Bit 0 (1 << 0 == 0x01) is the BC1 pin. BC2 is hard-wired to +5v.
  // We can ignore bit 3, b/c that was just checked above & triggered
  // a reset.
  reg &= ~0x04;

  switch (reg) {
  case 0: // bDir==0 && BC1 == 0 (IAB)
    // Puts the DA bus in high-impedance state. Nothing for us to do?
    return;
  case 1: // bDir==0 && BC1 == 1 (DTB)
    // Contents of the currently addressed register are put in DA. FIXME?
    return;
  case 2: // bDir==1 && BC1 == 0 (DWS)
    // Write current PortA to PSG
    printf("Set register %d to %X\n", reg, PortA);
    r[curRegister] = PortA;
    if (curRegister <= 1) {
      cycleTime[0] = cycleTimeForPSG(0);
    } else if (curRegister <= 3) {
      cycleTime[1] = cycleTimeForPSG(1);
    } else if (curRegister <= 5) {
      cycleTime[2] = cycleTimeForPSG(2);
    } else if (curRegister == 7) {
      cycleTime[0] = cycleTimeForPSG(0);
      cycleTime[1] = cycleTimeForPSG(1);
      cycleTime[2] = cycleTimeForPSG(2);
    }

    return;
  case 3: // bDir==1 && BC1 == 1 (INTAK)
    // Select current register
    curRegister = PortA & 0xF;
    return;
  }
}

// The lowest frequency the AY8910 makes is 30.6 Hz, which is ~33431
// clock cycles.
//
// The highest frequency produced is 125kHz, which is ~8 cycles.
//
// The highest practicable, given our 24-cycle-main-loop, is
// 41kHz. Which should be plenty fine.
//
// Conversely: we should be able to call update() as slowly as once
// every 60-ish clock cycles before we start noticing it in the output
// audio.
uint16_t AY8910::cycleTimeForPSG(uint8_t psg)
{
  // Convert the current registers in to a cycle count for how long
  // between flips of 0-to-1 from the square wave generator.

  uint16_t regVal = (r[1+(psg*2)] << 8) | (r[0 + (psg*2)]);
  if (regVal == 0) regVal++;

  // Ft = 4MHz / (32 * regVal); our clock is 1MHz
  // so we should return (32 * regVal) / 4 ?

  return (32 * regVal) / 4;
}

void AY8910::update(uint32_t cpuCycleCount)
{
  // For any waveformFlipTimer that is > 0: if cpuCycleCount is larger
  // than the timer, we'll flip state. (It's a square wave!)

  for (uint8_t i=0; i<3; i++) {
    uint32_t cc = cycleTime[i];

    if (cc == 0) {
      waveformFlipTimer[i] = 0;
    } else {
      if (!waveformFlipTimer[i]) {
	// start a cycle, if necessary
	waveformFlipTimer[i] = cpuCycleCount + cc;
      }

      if (waveformFlipTimer[i] && waveformFlipTimer[i] <= cpuCycleCount) {
	// flip when it's time to flip
	waveformFlipTimer[i] += cc;
	outputState[i] = !outputState[i];
      }
    }
    // If any of the square waves is on, then we want to be on.
    
    // r[i+8] is the amplitude control.
    // FIXME: if r[i+8] & 0x10, then it's an envelope-specific amplitude
    g_speaker->mixOutput(outputState[i] ? (r[i+8] & 0x0F) : 0x00);
  }
}

