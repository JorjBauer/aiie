#include "ay8910.h"
#include <stdio.h>

#include "globals.h"

// Map our linear 4-bit amplitude to 8-bit output level
static const uint8_t volumeLevels[16] = { 0x00, 0x04, 0x05, 0x07, 
					  0x0B, 0x10, 0x16, 0x23,
					  0x2B, 0x44, 0x5A, 0x73,
					  0x92, 0xB0, 0xD9, 0xFF };

// Envelope constants
enum {
  AY_ENV_HOLD   = 1,
  AY_ENV_ALT    = 2,
  AY_ENV_ATTACK = 4,
  AY_ENV_CONT   = 8
};

/* Envelope handling
 * (Per General Instruments AY-3-8910 documentation.)
 *
 * Envelope period is set in the 16-bit value r[0x0C]:r[0x0B] (where 0 = 1).
 * The resulting frequency is from 0.12Hz to 7812.5 Hz.
 *
 * The shape of the envelope is selected by r[0x0D] and uses the
 * constants above.
 *
 * If AY_ENV_HOLD is set, then when the envelope reaches terminal (0
 * or 15) it stays there.
 *
 * If AY_ENV_ALT is set, the direction reverses each time it reaches
 * terminal. (If both AY_ENV_HOLD and AY_ENV_ALT are set, then the
 * envelope counter returns to its initial count before holding.)
 *
 * If AY_ENV_ATTACK is set, the counter is ascending (0-to-15); otherwise 
 * it is descending (15-to-0).
 *
 * If AY_ENV_CONT is *clear* (0), then the counter resets to 0 after
 * one cycle and holds there. If it is 1, it does whatever HOLD
 * says. (So AY_ENV_CONT==0 takes priority over AY_ENV_HOLD).
 *
 *
 */ 

AY8910::AY8910() : lcg(0)
{
  Reset();
}

void AY8910::Reset()
{
  curRegister = 0;

  // FIXME: what are the right default values?
  for (uint8_t i=0; i<16; i++)
    r[i] = 0x00;
  waveformFlipTimer[0] = waveformFlipTimer[1] = waveformFlipTimer[2] = 0;
  outputState[0] = outputState[1] = outputState[2] = 0;
  envCounter = 0;
  envelopeTimer = envelopeTime = 0;
  envDirection = 1;
  noiseFlipTimer = 0;
  noiseFlag = true;

  lcgBitsRemaining = 0;

#if 0
  // Debugging
  r[ENV_PERIOD_COARSE] = 0xFF;
  r[ENV_PERIOD_FINE] = 0xFF;
  envelopeTimer = 1;
  envelopeTime = calculateEnvelopeTime();
  r[ENV_SHAPE] = 0x08; // sawtooth, descending
  if (r[ENV_SHAPE] & AY_ENV_ATTACK) {
    // rising
    envDirection = 1;
    envCounter = 0;
  } else {
    // falling
    envDirection = -1;
    envCounter = 15;
  }
#endif  
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
  if ((reg & NRSET) == 0) {
    Reset();
    return;
  }

  // Bit 0 (1 << 0 == 0x01) is the BC1 pin. BC2 is hard-wired to +5v.
  // We can ignore bit 3, b/c that was just checked above & triggered
  // a reset.
  reg &= ~0x04;

  switch (reg) {
  case IAB: // bDir==0 && BC1 == 0 (IAB)
    // Puts the DA bus in high-impedance state. Nothing for us to do?
    return;
  case DTB: // bDir==0 && BC1 == 1 (DTB)
    // Contents of the currently addressed register are put in DA. FIXME?
    return;
  case DWS: // bDir==1 && BC1 == 0 (DWS)
    // Write current PortA to PSG
    r[curRegister] = PortA;

    if (curRegister <= CHAN_A_COARSE) {
      // FIXME: for all of A/B/C changes, figure out how much time had
      // elapsed on the previous timer and apply it to the new one
      cycleTime[0] = cycleTimeForPSG(0);
      waveformFlipTimer[0] = g_cpu->cycles + cycleTime[0];
    } else if (curRegister <= CHAN_B_COARSE) {
      cycleTime[1] = cycleTimeForPSG(1);
      waveformFlipTimer[1] = g_cpu->cycles + cycleTime[1];
    } else if (curRegister <= CHAN_C_COARSE) {
      cycleTime[2] = cycleTimeForPSG(2);
      waveformFlipTimer[2] = g_cpu->cycles + cycleTime[2];
    } else if (curRegister == ENAB) {
      if (r[ENAB] & ENAB_N_TONEA) {
	cycleTime[0] = waveformFlipTimer[0] = 0;
      } else {
	cycleTime[0] = cycleTimeForPSG(0);
	waveformFlipTimer[0] = g_cpu->cycles + cycleTime[0];
      }
      if (r[ENAB] & ENAB_N_TONEB) {
	cycleTime[1] = waveformFlipTimer[1] = 0;
      } else {
	cycleTime[1] = cycleTimeForPSG(1);
	waveformFlipTimer[1] = g_cpu->cycles + cycleTime[1];
      }
      if (r[ENAB] & ENAB_N_TONEC) {
	cycleTime[2] = waveformFlipTimer[2] = 0;
      } else {
	cycleTime[2] = cycleTimeForPSG(2);
	waveformFlipTimer[2] = g_cpu->cycles + cycleTime[2];
      }
    } else if (curRegister >= ENV_PERIOD_FINE && curRegister <= ENV_PERIOD_COARSE) {
      // Envelope control -- period or shape
      // FIXME: should envCounter be initialized to the start position?
      envelopeTime = calculateEnvelopeTime();
      envelopeTimer = 0; // reset so it will pick up @ next tick
    } else if (curRegister == ENV_SHAPE) {
      if (r[ENV_SHAPE] & AY_ENV_ATTACK) {
	// rising
	envDirection = 1;
	envCounter = 0;
      } else {
	// falling
	envDirection = -1;
	envCounter = 15;
      }
    } else if (curRegister == NOISE_PERIOD) {
      noiseFlipTimer = g_cpu->cycles + cycleTimeForNoise();
    }
    return;
  case INTAK: // bDir==1 && BC1 == 1 (INTAK)
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

uint16_t AY8910::cycleTimeForNoise()
{
  uint8_t regval = r[NOISE_PERIOD];
  if (regval == 0) regval++;

  return (512 * regval) / 4;
}

// Similar calculation: this one, for the envelope timer.
// FIXME: I *think* this is right. Not sure. Needs validation.
uint32_t AY8910::calculateEnvelopeTime()
{
  uint32_t regVal = (r[ENV_PERIOD_COARSE] << 8) | (r[ENV_PERIOD_FINE]);
  if (regVal == 0) regVal++;

  // This constant is wrong by about 2%. But it should be fast b/c
  // powers of 2.
  return (32 * regVal) / 4;
}

void AY8910::update(uint32_t cpuCycleCount)
{
#if 0
  // Debugging: print state of the 16 registers
  printf("AY8910: ");
  for (int i=0; i<16; i++) {
    printf("%02X ", r[i]);
  }
  printf("%04X %04X %04X\n", cycleTime[0], cycleTime[1], cycleTime[2]);
#endif
  
  // update the envelope timer if it's time
  if (envelopeTime != 0) {
    if (!envelopeTimer) {
      // timer wasn't set, so start it running
      envelopeTimer = cpuCycleCount + envelopeTime;
    }
    if (envelopeTimer <= cpuCycleCount) {
      // time to update the envelopeCounter.

      envCounter += envDirection;

      switch (r[ENV_SHAPE]) {
	// Continue / Attack / Alternate / Hold bits
      case 0x00: // 0 / 0 / x / x -- descend once, stay @ bottom
      case 0x01: // 0 / 0 / x / x
      case 0x02: // 0 / 0 / x / x
      case 0x03: // 0 / 0 / x / x
      case 0x04: // 0 / 1 / x / x -- ascend once, jump to bottom
      case 0x05: // 0 / 1 / x / x
      case 0x06: // 0 / 1 / x / x
      case 0x07: // 0 / 1 / x / x
      case 0x09: // 1 / 0 / 0 / 1 -- descend once, stay @ bottom
      case 0x0b: // 1 / 0 / 1 / 1 -- descend once, jump to top
      case 0x0d: // 1 / 1 / 0 / 1 -- ascend once, stay @ top
      case 0x0f: // 1 / 1 / 1 / 1 -- ascend once, jump to bottom

	// In all these cases, we go from start to finish once. In all
	// cases except 0x0b and 0x0d, when we're done, we go low.

	if (envDirection > 0) {
	  // We were ascending: did we hit 15? If so, stop & go terminal
	  if (envCounter == 15) {
	    envDirection = 0;
	    // One ascending case (0x0b) goes high after; all others are low
	    envCounter = (r[ENV_SHAPE] == 0x0b ? 0x0F : 0x00);
	  }
	} else if (envDirection < 0) {
	  // We were descending: did we hit 0? If so, stop & go terminal
	  if (envCounter == 0) {
	    envDirection = 0;
	    // One descending case (0x0d) goes high after; all others are low
	    envCounter = (r[ENV_SHAPE] == 0x0d ? 0x0F : 0x00);
	  }
	}
	break;

      case 0x08:
      case 0x0C:
	// These two jump back to the start when they get to the end.

	if (envCounter > 15) {
	  envCounter = 0;
	} else if (envCounter < 0) {
	  envCounter = 15;
	}
	break;
	
      case 0x0A:
      case 0x0E:
	break;
	// These two reverse direction.
	if (envCounter == 15 || envCounter == 0) {
	  envDirection = -envDirection;
	  }
	break;
      }
      
      // Set up the envelope timer for the next transition
      // FIXME: can set this to 0 if envDirection is 0, but have to be careful about setup of timer again when envDirection is re-set
      envelopeTimer += envelopeTime;
    }
  }

  // For the noise timer: if it expires, we get another random bit
  if (noiseFlipTimer && noiseFlipTimer <= cpuCycleCount) {
    // FIXME: srnd() this somewhere when we initialized? Does it matter?

    if (!lcgBitsRemaining) {
      lcgBitsRemaining = 8;
      lcgLastByte = lcg.rnd();
    }
    noiseFlag = lcgLastByte & 1;
    lcgLastByte >>= 1;
    lcgBitsRemaining--;
  }

#if 0
  // DEBUGGING ENVELOPES: just output the envelope
  g_speaker->mixOutput(volumeLevels[envCounter]);
  return;
#endif

  // For any waveformFlipTimer that is > 0: if cpuCycleCount is larger
  // than the timer, we'll flip state. (It's a square wave!)

  for (uint8_t i=0; i<3; i++) {
    uint32_t cc = cycleTime[i];

    if (cc) {
      if (waveformFlipTimer[i] <= cpuCycleCount) {
	// flip when it's time to flip
	waveformFlipTimer[i] += cc;
	outputState[i] = !outputState[i];
      }
    } else {
      outputState[i] = 0;
    }

    // Figure out what output comes from this channel and send it to
    // the speaker. The output is controlled by outputState[i] (from
    // the square wave, above); the amplitude control line for this
    // output (r[i+8], below) and the tone/noise selection.

    uint8_t amplitude = 0;
    // If we're trying to output "high" this square-wave cycle, and if
    // the ToneEnable bit is set for this register, then generate
    // output.
    if (!(r[ENAB] & (1 << i)) && outputState[i]) {
      amplitude = r[i+8] & 0xF;
      // ... and if bit 0x10 is on, it's modified by the envelope counter.
      if (r[i+8] & 0x10) 
	amplitude = envCounter;
    }

    // test the NoiseEnable bit for this register
    if (!(r[ENAB] & (1 << (3+i)))) {
      // FIXME: if the noiseFlag is off, do we keep the tone value
      // above? Or set to 0?
      if (noiseFlag) {
	amplitude = 7; // median "0-value" level (fixme?)
	// ... and if bit 0x10 is on, it's modified by the envelope counter.
	if (r[i+8] & 0x10) 
	  amplitude = envCounter >> 1;
      } else {
	amplitude = 0;
      }
    }

    g_speaker->mixOutput(volumeLevels[amplitude]);
  }
}

