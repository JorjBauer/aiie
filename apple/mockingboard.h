#ifndef __MOCKINGBOARD_H
#define __MOCKINGBOARD_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "slot.h"

#define AY_NUM_REGS    16
#define AY_NUM_CHANNELS 3
#define SAMPLE_RATE    44100

#define MB_BUF_SAMPLES 4096
#define MB_BUF_MASK    (MB_BUF_SAMPLES - 1)

struct Via6522 {
  uint8_t orb, ora;
  uint8_t ddrb, ddra;
  uint16_t timer1latch;
  uint16_t timer1counter;
  uint16_t timer2latch;
  uint16_t timer2counter;
  uint8_t sr;
  uint8_t acr;
  uint8_t pcr;
  uint8_t ifr;
  uint8_t ier;

  bool timer1running;
  bool timer2running;
  bool timer1fired;
  bool timer2fired;
};

struct AY8910 {
  uint8_t regs[AY_NUM_REGS];
  uint8_t latchedReg;

  uint32_t tonePeriod[AY_NUM_CHANNELS];
  uint32_t toneCounter[AY_NUM_CHANNELS];
  bool     toneHigh[AY_NUM_CHANNELS];

  uint32_t noisePeriod;
  uint32_t noiseCounter;
  uint32_t noiseShift;

  uint32_t envPeriod;
  uint32_t envCounter;
  uint8_t  envStep;
  bool     envHolding;
  int8_t   envDirection;
  uint8_t  envShape;
};

class Mockingboard : public Slot {
 public:
  Mockingboard();
  virtual ~Mockingboard();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset();
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  virtual bool interceptsSlotRom() { return true; };
  virtual uint8_t readSlotRom(uint8_t addr);
  virtual void writeSlotRom(uint8_t addr, uint8_t val);

  void update(uint64_t cpuCycles);

  void renderToBuffer(int16_t *buf, int count);
  int16_t mixSample();

 private:
  uint8_t viaRead(int whichVia, uint8_t reg);
  void viaWrite(int whichVia, uint8_t reg, uint8_t val);
  void viaUpdateIFR(int whichVia);

  void ayWriteReg(int whichAY);
  void ayReadReg(int whichAY);
  void ayLatchAddress(int whichAY);
  void ayReset(int whichAY);
  void ayRecalc(int whichAY);

  void handleOrbChange(int whichVia);

  int16_t renderOneSample();

  Via6522 via[2];
  AY8910  ay[2];

  uint64_t lastCycleCount;
};

#endif
