#ifndef __AY8910_H
#define __AY8910_H

#include <stdint.h>

class AY8910 {
 public:
  AY8910();

  void Reset();

  uint8_t read(uint8_t reg);
  void write(uint8_t reg, uint8_t PortA);

  void update(uint32_t cpuCycleCount);

 protected:
  uint16_t cycleTimeForPSG(uint8_t psg);

 private:
  uint8_t curRegister;
  uint8_t r[16];
  uint32_t waveformFlipTimer[3];
  uint8_t outputState[3];
  uint16_t cycleTime[3];
};

#endif
