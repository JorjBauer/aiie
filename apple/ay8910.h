#ifndef __AY8910_H
#define __AY8910_H

#include <stdint.h>
#include "lcg.h"

// Operations...
enum {
  IAB   = 0,
  DTB   = 1,
  DWS   = 2,
  INTAK = 3,
  NRSET = 4
};

// Registers...
enum {
  CHAN_A_FINE       = 0,
  CHAN_A_COARSE     = 1,
  CHAN_B_FINE       = 2,
  CHAN_B_COARSE     = 3,
  CHAN_C_FINE       = 4,
  CHAN_C_COARSE     = 5,
  NOISE_PERIOD      = 6,
  ENAB              = 7,
  CHAN_A_AMP        = 8,
  CHAN_B_AMP        = 9,
  CHAN_C_AMP        = 10,
  ENV_PERIOD_FINE   = 11,
  ENV_PERIOD_COARSE = 12,
  ENV_SHAPE         = 13
};

// Enable flags (all negative; enabled-low)
enum {
  ENAB_N_TONEA  = 1,
  ENAB_N_TONEB  = 2,
  ENAB_N_TONEC  = 4,
  ENAB_N_NOISEA = 8,
  ENAB_N_NOISEB = 16,
  ENAB_N_NOISEC = 32
};

class AY8910 {
 public:
  AY8910();

  void Reset();

  uint8_t read(uint8_t reg);
  void write(uint8_t reg, uint8_t PortA);

  void update(uint32_t cpuCycleCount);

 protected:
  uint16_t cycleTimeForPSG(uint8_t psg);
  uint16_t cycleTimeForNoise();
  uint32_t calculateEnvelopeTime();

 private:
  uint8_t curRegister;
  uint8_t r[16];
  uint16_t cycleTime[3];         // how long each cycle will last, in clock cycles
  uint32_t waveformFlipTimer[3]; // when we're going to flip next
  uint8_t outputState[3];
  int8_t envCounter; // which bit of the waveform the envelope is on
  int8_t envDirection;
  uint32_t envelopeTime;
  uint32_t envelopeTimer;
  uint32_t noiseFlipTimer;
  bool noiseFlag;

  LCG lcg;
  uint8_t lcgLastByte;
  uint8_t lcgBitsRemaining;
};

#endif
