#ifndef __MOCKINGBOARD_H
#define __MOCKINGBOARD_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "slot.h"

class SC01Synth;

#define AY_NUM_REGS    16
#define AY_NUM_CHANNELS 3
#define SAMPLE_RATE    44100

#define MB_BUF_SAMPLES 4096
#define MB_BUF_MASK    (MB_BUF_SAMPLES - 1)

// The timers are kept as "this count, on this cycle" and worked out from
// there when someone looks, so they're right to the cycle no matter how
// often we're updated. All cycles are bus cycles (Cpu::busCycle()).
struct Via6522 {
  uint8_t orb, ora;
  uint8_t ddrb, ddra;
  uint8_t paPins;            // what the outside drives onto port A ($FF: nothing)
  uint16_t timer1latch;
  uint8_t  timer2latchLow;   // T2 has no high latch
  int64_t  t1Base;           // T1 held t1Start on this cycle, then counted down
  uint16_t t1Start;
  bool     t1Armed;          // one-shot: flag on the next underflow
  int64_t  t1NextFlag;       // cycle of the next T1 flag (INT64_MAX: none)
  int64_t  t2Base;
  uint16_t t2Start;
  int64_t  t2NextFlag;
  uint8_t sr;
  uint8_t acr;
  uint8_t pcr;
  uint8_t ifr;
  uint8_t ier;
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

// The SC-01 speech chip on a Sound/Speech I, as the CPU sees it: the
// phoneme latch and the A/R handshake, timed in CPU cycles. The voice itself
// is SC01Synth (sc01.h), which runs on the audio thread.
struct SC01State {
  uint8_t phone;        // 6-bit phoneme code
  uint8_t inflection;   // 2-bit pitch input
  bool    arHigh;       // A/R: high = ready for the next phoneme
  int64_t arRiseCycle;  // CPU cycle at which A/R goes high again
};

class Mockingboard : public Slot {
 public:
  // soundSpeech = true makes this a Sound/Speech I: the $Cn00 6522 drives
  // only the SC-01 (on port B), and the $Cn80 6522 drives the one AY and is
  // wired to NMI instead of IRQ, as on the real card.
  Mockingboard(bool soundSpeech = false);
  virtual ~Mockingboard();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset();
  // Ctrl-Reset: the //e's RESET line goes to each 6522's RES pin
  virtual void busReset();
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);

  virtual bool interceptsSlotRom() { return true; };
  virtual uint8_t readSlotRom(uint8_t addr);
  virtual void writeSlotRom(uint8_t addr, uint8_t val);

  // Bring the timers and the SC-01 up to the given cycle and raise any flags
  // that came due, then ask the CPU to call us back for the next one.
  void update(int64_t cycle);

  void renderToBuffer(int16_t *buf, int count);
  int16_t mixSample();

  // Offline replay: apply one captured slot write straight to the VIA/AY, with
  // no CPU-timing side effects. addr is the low byte the CPU wrote (VIA select in
  // bit 7, register in bits 3..0). Used by the standalone mb-replay test tool to
  // regenerate audio from a captured dataset.
  void applyWrite(uint8_t addr, uint8_t val);

 private:
  uint8_t viaRead(int whichVia, uint8_t reg);
  void viaWrite(int whichVia, uint8_t reg, uint8_t val);
  void viaUpdateIFR(int whichVia, int64_t when = -1);
  void viaSetFlag(int whichVia, uint8_t mask, int64_t when);
  uint16_t t1Value(int whichVia, int64_t cycle);
  uint16_t t2Value(int whichVia, int64_t cycle);
  int64_t t1UnderflowAfter(int whichVia, int64_t cycle);
  void t1Rebase(int whichVia, int64_t cycle, uint16_t newLatch);
  void t1Reschedule(int whichVia, int64_t cycle);
  void scheduleNext();
  void rebaseClock(int64_t delta);
  static void eventHook();

  void ayWriteReg(int whichAY);
  void ayReadReg(int whichAY);
  void ayLatchAddress(int whichAY);
  void ayReset(int whichAY);
  void ayRecalc(int whichAY, bool restartEnvelope);

  void handleOrbChange(int whichVia);
  void viaOrbAccess(int whichVia);

  void sc01Write(uint8_t busValue);
  void sc01SetAR(bool high, int64_t when);

  int16_t renderOneSample();

  Via6522 via[2];
  AY8910  ay[2];

  bool    hasSpeech;
  bool    viaHasAY[2];  // does this 6522's port B drive an AY?
  bool    ayHasPorts;   // AY-3-8910 (Sound/Speech I) rather than the portless 8913
  bool    via1Nmi;      // the $Cn80 6522's interrupt goes to NMI, not IRQ
  bool    nmiLow;       // last level we drove on NMI (it's edge-triggered)
  uint8_t irqSource;    // our bit on the CPU's wired-OR IRQ line
  SC01State sc01;
  SC01Synth *voice;     // NULL unless this is a Sound/Speech I

  int64_t lastUpdate;
};

#endif
