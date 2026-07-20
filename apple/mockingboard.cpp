#include "mockingboard.h"
#include <string.h>

#include "globals.h"
#include "cpu.h"

#ifdef TEENSYDUINO
#include "teensy-println.h"
#include "iocompat.h"
#endif

#ifndef TEENSYDUINO
#include <stdlib.h>   // getenv, for the optional capture hook
// Optional capture of the raw CPU->card write stream, for offline replay and
// regression testing. Set AIIE_MB_CAPTURE=<path> before launching to dump a CSV
// of "cycle,addr,val"; the standalone mb-replay tool turns that back into a WAV.
static FILE *s_mbCapture = NULL;
static bool  s_mbCaptureInit = false;
static void mbCaptureWrite(int64_t cycle, uint8_t addr, uint8_t val)
{
  if (!s_mbCaptureInit) {
    s_mbCaptureInit = true;
    const char *path = getenv("AIIE_MB_CAPTURE");
    if (path && path[0]) {
      s_mbCapture = fopen(path, "w");
      if (s_mbCapture)
        fprintf(s_mbCapture, "# aiie-mockingboard-capture v1 clockHz=1023000\n");
    }
  }
  if (s_mbCapture) {
    fprintf(s_mbCapture, "%lld,%u,%u\n", (long long)cycle, (unsigned)addr, (unsigned)val);
    fflush(s_mbCapture);   // survive an abrupt quit at the end of a capture run
  }
}
#endif

// Matthew Westcott's 2001 measurements, DC-offset removed, 0x1FFF peak
static const int16_t ayAmplitudes[16] = {
  0x0000, 0x0056, 0x007E, 0x00B1,
  0x0101, 0x0179, 0x0208, 0x0365,
  0x0438, 0x06EC, 0x0983, 0x0C81,
  0x1069, 0x1463, 0x1A31, 0x1FFF
};

// AY control lines via 6522 Port B
#define AY_BDIR 0x02
#define AY_BC1  0x01
#define AY_RESET_PIN 0x04

// 6522 IFR/IER bits
#define IFR_TIMER1 0x40
#define IFR_TIMER2 0x20
#define IFR_IRQ    0x80

Mockingboard::Mockingboard()
{
  lastCycleCount = 0;
  Reset();
}

Mockingboard::~Mockingboard() {}

void Mockingboard::Reset()
{
  lastCycleCount = 0;
  for (int i = 0; i < 2; i++) {
    memset(&via[i], 0, sizeof(Via6522));
    via[i].timer1latch = 0xFFFF;
    via[i].timer2latch = 0xFFFF;
    memset(&ay[i], 0, sizeof(AY8910));
    ay[i].noiseShift = 1;
    ay[i].noisePeriod = 1;
    ay[i].envPeriod = 1;
    for (int ch = 0; ch < AY_NUM_CHANNELS; ch++) {
      ay[i].toneHigh[ch] = false;
      ay[i].tonePeriod[ch] = 1;
    }
    ay[i].envDirection = 1;
  }
}

bool Mockingboard::Serialize(int8_t fd) { return true; }
bool Mockingboard::Deserialize(int8_t fd) { return true; }

void Mockingboard::loadROM(uint8_t *toWhere)
{
  memset(toWhere, 0, 256);
}

uint8_t Mockingboard::readSwitches(uint8_t s) { return 0; }
void Mockingboard::writeSwitches(uint8_t s, uint8_t v) {}

uint8_t Mockingboard::readSlotRom(uint8_t addr)
{
  update(g_cpu->cycles);
  int whichVia = (addr & 0x80) ? 1 : 0;
  return viaRead(whichVia, addr & 0x0F);
}

void Mockingboard::writeSlotRom(uint8_t addr, uint8_t val)
{
  update(g_cpu->cycles);
#ifndef TEENSYDUINO
  mbCaptureWrite(g_cpu->cycles, addr, val);
#endif
  int whichVia = (addr & 0x80) ? 1 : 0;
  viaWrite(whichVia, addr & 0x0F, val);
}

// Offline replay of a captured write (see mockingboard.h). Deliberately skips
// update(): that only ticks the VIA timers / CPU IRQ, which do not affect the
// generated audio, and avoids needing a live CPU in the standalone tool.
void Mockingboard::applyWrite(uint8_t addr, uint8_t val)
{
  int whichVia = (addr & 0x80) ? 1 : 0;
  viaWrite(whichVia, addr & 0x0F, val);
}

// --- 6522 VIA ---

uint8_t Mockingboard::viaRead(int v, uint8_t reg)
{
  Via6522 &p = via[v];

  switch (reg) {
  case 0x00:
    return (p.orb & p.ddrb) | (~p.ddrb & 0xFF);
  case 0x01:
  case 0x0F:
    return p.ora;
  case 0x02:
    return p.ddrb;
  case 0x03:
    return p.ddra;
  case 0x04:
    p.ifr &= ~IFR_TIMER1;
    p.timer1fired = false;
    viaUpdateIFR(v);
    return p.timer1counter & 0xFF;
  case 0x05:
    return (p.timer1counter >> 8) & 0xFF;
  case 0x06:
    return p.timer1latch & 0xFF;
  case 0x07:
    return (p.timer1latch >> 8) & 0xFF;
  case 0x08:
    p.ifr &= ~IFR_TIMER2;
    p.timer2fired = false;
    viaUpdateIFR(v);
    return p.timer2counter & 0xFF;
  case 0x09:
    return (p.timer2counter >> 8) & 0xFF;
  case 0x0A:
    return p.sr;
  case 0x0B:
    return p.acr;
  case 0x0C:
    return p.pcr;
  case 0x0D:
    return p.ifr;
  case 0x0E:
    return p.ier | 0x80;
  }
  return 0;
}

void Mockingboard::viaWrite(int v, uint8_t reg, uint8_t val)
{
  Via6522 &p = via[v];

  switch (reg) {
  case 0x00:
    p.orb = (p.orb & ~p.ddrb) | (val & p.ddrb);
    handleOrbChange(v);
    break;
  case 0x01:
  case 0x0F:
    p.ora = val;
    break;
  case 0x02:
    p.ddrb = val;
    break;
  case 0x03:
    p.ddra = val;
    break;
  case 0x04:
    p.timer1latch = (p.timer1latch & 0xFF00) | val;
    break;
  case 0x05:
    p.timer1latch = (p.timer1latch & 0x00FF) | ((uint16_t)val << 8);
    p.timer1counter = p.timer1latch;
    p.timer1running = true;
    p.timer1fired = false;
    p.ifr &= ~IFR_TIMER1;
    viaUpdateIFR(v);
    break;
  case 0x06:
    p.timer1latch = (p.timer1latch & 0xFF00) | val;
    break;
  case 0x07:
    p.timer1latch = (p.timer1latch & 0x00FF) | ((uint16_t)val << 8);
    p.ifr &= ~IFR_TIMER1;
    viaUpdateIFR(v);
    break;
  case 0x08:
    p.timer2latch = val;
    break;
  case 0x09:
    p.timer2counter = ((uint16_t)val << 8) | (p.timer2latch & 0xFF);
    p.timer2running = true;
    p.timer2fired = false;
    p.ifr &= ~IFR_TIMER2;
    viaUpdateIFR(v);
    break;
  case 0x0A:
    p.sr = val;
    break;
  case 0x0B:
    p.acr = val;
    break;
  case 0x0C:
    p.pcr = val;
    break;
  case 0x0D:
    p.ifr &= ~(val & 0x7F);
    viaUpdateIFR(v);
    break;
  case 0x0E:
    if (val & 0x80)
      p.ier |= (val & 0x7F);
    else
      p.ier &= ~(val & 0x7F);
    viaUpdateIFR(v);
    break;
  }
}

void Mockingboard::viaUpdateIFR(int v)
{
  Via6522 &p = via[v];
  if (p.ifr & p.ier & 0x7F)
    p.ifr |= IFR_IRQ;
  else
    p.ifr &= ~IFR_IRQ;

  bool anyIrq = (via[0].ifr & IFR_IRQ) || (via[1].ifr & IFR_IRQ);
  if (anyIrq)
    g_cpu->assertIrq();
  else
    g_cpu->deassertIrq();
}

// --- AY-3-8910 bus protocol ---

void Mockingboard::handleOrbChange(int v)
{
  uint8_t orb = via[v].orb;

  if (!(orb & AY_RESET_PIN)) {
    ayReset(v);
    return;
  }

  uint8_t bdir = (orb & AY_BDIR) ? 1 : 0;
  uint8_t bc1  = (orb & AY_BC1)  ? 1 : 0;

  if (bdir && bc1) {
    ayLatchAddress(v);
  } else if (bdir && !bc1) {
    ayWriteReg(v);
  } else if (!bdir && bc1) {
    ayReadReg(v);
  }
}

void Mockingboard::ayLatchAddress(int a)
{
  ay[a].latchedReg = via[a].ora & 0x0F;
}

void Mockingboard::ayWriteReg(int a)
{
  uint8_t reg = ay[a].latchedReg;
  if (reg >= AY_NUM_REGS) return;
  ay[a].regs[reg] = via[a].ora;
  ayRecalc(a);
}

void Mockingboard::ayReadReg(int a)
{
  uint8_t reg = ay[a].latchedReg;
  if (reg >= AY_NUM_REGS) return;
  via[a].ora = ay[a].regs[reg];
}

void Mockingboard::ayReset(int a)
{
  memset(&ay[a], 0, sizeof(AY8910));
  ay[a].noiseShift = 1;
  ay[a].noisePeriod = 1;
  ay[a].envPeriod = 1;
  for (int ch = 0; ch < AY_NUM_CHANNELS; ch++)
    ay[a].tonePeriod[ch] = 1;
  ay[a].envDirection = 1;
}

void Mockingboard::ayRecalc(int a)
{
  AY8910 &psg = ay[a];

  psg.tonePeriod[0] = ((psg.regs[1] & 0x0F) << 8) | psg.regs[0];
  psg.tonePeriod[1] = ((psg.regs[3] & 0x0F) << 8) | psg.regs[2];
  psg.tonePeriod[2] = ((psg.regs[5] & 0x0F) << 8) | psg.regs[4];

  for (int ch = 0; ch < AY_NUM_CHANNELS; ch++) {
    if (psg.tonePeriod[ch] == 0)
      psg.tonePeriod[ch] = 1;
  }

  psg.noisePeriod = psg.regs[6] & 0x1F;
  if (psg.noisePeriod == 0) psg.noisePeriod = 1;

  psg.envPeriod = (psg.regs[12] << 8) | psg.regs[11];
  if (psg.envPeriod == 0) psg.envPeriod = 1;

  if (psg.regs[13] != 0xFF) {
    psg.envShape = psg.regs[13] & 0x0F;
    psg.envCounter = 0;
    psg.envHolding = false;

    if (psg.envShape & 0x04) {
      psg.envDirection = 1;
      psg.envStep = 0;
    } else {
      psg.envDirection = -1;
      psg.envStep = 15;
    }

    psg.regs[13] = 0xFF;
  }
}

// --- Audio generation ---

// The AY master clock is the CPU clock (1.023 MHz). The AY divides
// that by 8 internally for tone generation, and by 16 for the
// envelope. We generate samples at 44.1 kHz, so each output sample
// spans ~23.2 AY tone clocks (1023000 / 8 / 44100).

#define AY_CLK_PER_SAMPLE_X16 ((uint32_t)((1023000.0 / 8.0 / SAMPLE_RATE) * 65536.0))
#define ENV_CLK_PER_SAMPLE_X16 ((uint32_t)((1023000.0 / 16.0 / SAMPLE_RATE) * 65536.0))
#define NOISE_CLK_PER_SAMPLE_X16 ((uint32_t)((1023000.0 / 16.0 / SAMPLE_RATE) * 65536.0))

int16_t Mockingboard::renderOneSample()
{
  int32_t sum = 0;

  for (int a = 0; a < 2; a++) {
    AY8910 &psg = ay[a];
    uint8_t mixer = psg.regs[7];

    // Advance noise
    psg.noiseCounter += NOISE_CLK_PER_SAMPLE_X16;
    while (psg.noiseCounter >= (psg.noisePeriod << 16)) {
      psg.noiseCounter -= (psg.noisePeriod << 16);
      // 17-bit LFSR: bit 0 XOR bit 3, feedback to bit 16
      uint32_t bit = ((psg.noiseShift) ^ (psg.noiseShift >> 3)) & 1;
      psg.noiseShift = (psg.noiseShift >> 1) | (bit << 16);
    }
    bool noiseOut = psg.noiseShift & 1;
      
    /* 17-bit LFSR note: there's some confusion about which bit is
       "correct" here. AppleWin uses bit 2 as feedback; MAME and KEGS
       use bit 3.  Empirically, I don't hear the difference. US Patent
       4933980 (cited by MAME) does not publish anything about the
       LFSR; the only true primary source that exists is a single
       decapping of an AY-3-8910, which was then reconstructed
       gate-by-gate -
       https://github.com/lvd2/ay-3-8910_reverse_engineered/blob/master/sch/ay-3-8910.jpg
       The generated verilog code -
       https://github.com/lvd2/ay-3-8910_reverse_engineered/blob/master/rtl/ay_model.v
       constructs the noise polynomial as
       
       wire [16:0] noise_reg; // 17-bit shift register
       ...
       .shift_in( (noise_reg[16] ^ noise_reg[13]) | (~|noise_reg) ), // feedback
       ...
       assign noise = ~noise_reg[16]; // output

       That feedback is the mirror of bits 0 and 3, the same LFSR as I'm using
       here but bit-ordered backward; and the silicon inverts the bit (and
       implements a "0 | noise_reg" guard).
    */

    // Advance envelope
    if (!psg.envHolding) {
      psg.envCounter += ENV_CLK_PER_SAMPLE_X16;
      while (psg.envCounter >= (psg.envPeriod << 16)) {
        psg.envCounter -= (psg.envPeriod << 16);
        psg.envStep += psg.envDirection;

        if (psg.envStep > 15 || psg.envStep < 0) {
          uint8_t shape = psg.envShape;
          bool cont    = shape & 0x08;
          bool alt     = shape & 0x02;
          bool hold    = shape & 0x01;

          if (!cont) {
            psg.envStep = 0;
            psg.envHolding = true;
          } else if (hold) {
            psg.envStep = alt ? 15 : 0;
            psg.envHolding = true;
          } else if (alt) {
            psg.envDirection = -psg.envDirection;
            psg.envStep += psg.envDirection;
          } else {
            psg.envStep = (psg.envDirection > 0) ? 0 : 15;
          }
        }
      }
    }

    uint8_t envAmpl = (psg.envStep < 0) ? 0 :
                      (psg.envStep > 15) ? 15 : psg.envStep;

    for (int ch = 0; ch < AY_NUM_CHANNELS; ch++) {
      // Advance tone
      psg.toneCounter[ch] += AY_CLK_PER_SAMPLE_X16;
      while (psg.toneCounter[ch] >= (psg.tonePeriod[ch] << 16)) {
        psg.toneCounter[ch] -= (psg.tonePeriod[ch] << 16);
        psg.toneHigh[ch] = !psg.toneHigh[ch];
      }

      bool toneEnable  = !(mixer & (1 << ch));
      bool noiseEnable = !(mixer & (8 << ch));

      bool toneGate  = psg.toneHigh[ch] || !toneEnable;
      bool noiseGate = noiseOut || !noiseEnable;

      if (toneGate && noiseGate) {
        uint8_t vol = psg.regs[8 + ch];
        int16_t ampl;
        if (vol & 0x10)
          ampl = ayAmplitudes[envAmpl];
        else
          ampl = ayAmplitudes[vol & 0x0F];
        sum += ampl;
      }
    }
  }

  // Clamp to int16 range
  if (sum > 0x7FFF) sum = 0x7FFF;
  if (sum < -0x7FFF) sum = -0x7FFF;
  return (int16_t)sum;
}

// Called from cpuMaintenance with the current cycle count.
// Ticks timers and generates audio samples.
void Mockingboard::update(uint64_t cpuCycles)
{
  if (lastCycleCount == 0) {
    lastCycleCount = cpuCycles;
    return;
  }

  uint64_t elapsed = cpuCycles - lastCycleCount;
  lastCycleCount = cpuCycles;

  // Tick 6522 timers — counters always decrement (even when "stopped"),
  // matching real 6522 behavior. Only IFR flags depend on timer1running.
  for (int v = 0; v < 2; v++) {
    Via6522 &p = via[v];

    if (elapsed >= p.timer1counter) {
      if (p.timer1running) {
        p.ifr |= IFR_TIMER1;
        p.timer1fired = true;
        viaUpdateIFR(v);
      }
      if (p.acr & 0x40) {
        uint32_t overflow = elapsed - p.timer1counter;
        if (p.timer1latch > 0)
          overflow %= (p.timer1latch + 2);
        p.timer1counter = p.timer1latch - overflow;
      } else {
        uint32_t overflow = elapsed - p.timer1counter;
        p.timer1counter = 0xFFFF - (overflow % 0x10000);
        p.timer1running = false;
      }
    } else {
      p.timer1counter -= elapsed;
    }

    if (elapsed >= p.timer2counter) {
      if (p.timer2running) {
        p.ifr |= IFR_TIMER2;
        p.timer2fired = true;
        viaUpdateIFR(v);
      }
      uint32_t overflow = elapsed - p.timer2counter;
      p.timer2counter = 0xFFFF - (overflow % 0x10000);
      p.timer2running = false;
    } else {
      p.timer2counter -= elapsed;
    }
  }

}

// Render 'count' samples directly into the caller's buffer.
// Called from the audio thread.
void Mockingboard::renderToBuffer(int16_t *buf, int count)
{
  for (int i = 0; i < count; i++)
    buf[i] = renderOneSample();
}

int16_t Mockingboard::mixSample()
{
  return renderOneSample();
}
