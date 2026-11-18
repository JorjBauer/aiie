#include "mockingboard.h"
#include <string.h>

#include "globals.h"
#include "cpu.h"
#include "sc01params.h"
#include "sc01.h"

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
#define IFR_CB1    0x10
#define IFR_CB2    0x08
#define IFR_IRQ    0x80

// 6522 PCR bits for port B's control lines
#define PCR_CB1_POSITIVE 0x10   // CB1 flags on a rising edge (else falling)
#define PCR_CB2_MODE     0xE0
#define PCR_CB2_IND_MASK 0xA0   // CB2 input modes 001 and 011 are "independent":
#define PCR_CB2_IND      0x20   //   an ORB access doesn't clear the CB2 flag
#define PCR_CB2_STB_MASK 0xC0   // CB2 output modes 100 (handshake) and 101
#define PCR_CB2_STB      0x80   //   (pulse) drop CB2 when ORB is written

// How long the SC-01 takes from the strobe until A/R rises, in master clocks:
// the chip commits the phoneme 72 clocks after the strobe, then runs 16 ticks
// of DUR*4+1 updates of 36 clocks, plus 2 (aiie-docs/sc01-rom-analysis.md).
// We clock the SC-01 from the Apple's own 1.023MHz, so a master clock is a CPU
// cycle. The chip's nominal clock is 720kHz, so if that's right this card
// talks about 1.42x faster than the datasheet. The real card has a rate pot,
// and nobody has timed one yet.
static int64_t sc01PhoneCycles(uint8_t phone)
{
  uint32_t dur = sc01Phones[phone & 0x3F].dur & 0x7F;
  return 72 + 16 * (dur * 4 + 1) * 36 + 2;
}

// Every card that might have asked the CPU for a callback. The CPU has one
// hook, so it updates all of us, and each asks again for its next event.
#define MAX_MB_CARDS 4
static Mockingboard *s_cards[MAX_MB_CARDS];

void Mockingboard::eventHook()
{
  for (int i = 0; i < MAX_MB_CARDS; i++)
    if (s_cards[i]) s_cards[i]->update(g_cpu->cycles);
}

Mockingboard::Mockingboard(bool soundSpeech)
{
  for (int i = 0; i < MAX_MB_CARDS; i++) {
    if (!s_cards[i]) { s_cards[i] = this; break; }
  }
  hasSpeech = soundSpeech;
  viaHasAY[0] = !soundSpeech;
  viaHasAY[1] = true;
  via1Nmi = soundSpeech;
  // The Sound/Speech I has an AY-3-8910, and the Mockingboard A/C have
  // AY-3-8913s
  ayHasPorts = soundSpeech;
  nmiLow = false;
  irqSource = soundSpeech ? IRQSRC_SOUNDSPEECH : IRQSRC_MOCKINGBOARD;
  // The real chip powers up in a random state. We start it idle on STOP with
  // A/R high.
  sc01.phone = 0x3F;
  sc01.inflection = 0;
  sc01.arHigh = true;
  sc01.arRiseCycle = 0;
  // Clocked from the Apple's 1.023MHz, like the A/R timing above
  voice = soundSpeech ? new SC01Synth(1023000.0) : NULL;
  lastUpdate = 0;
  Reset();
}

Mockingboard::~Mockingboard()
{
  delete voice;
  for (int i = 0; i < MAX_MB_CARDS; i++)
    if (s_cards[i] == this) s_cards[i] = NULL;
}

void Mockingboard::Reset()
{
  for (int i = 0; i < 2; i++) {
    memset(&via[i], 0, sizeof(Via6522));
    via[i].timer1latch = 0xFFFF;
    via[i].timer2latchLow = 0xFF;
    via[i].t1Start = 0xFFFF;
    via[i].t2Start = 0xFFFF;
    via[i].t1NextFlag = INT64_MAX;
    via[i].t2NextFlag = INT64_MAX;
    via[i].paPins = 0xFF;
    memset(&ay[i], 0, sizeof(AY8910));
    ay[i].latchedReg = 0xFF;
    ay[i].noiseShift = 1;
    ay[i].noisePeriod = 1;
    ay[i].envPeriod = 1;
    for (int ch = 0; ch < AY_NUM_CHANNELS; ch++) {
      ay[i].toneHigh[ch] = false;
      ay[i].tonePeriod[ch] = 1;
    }
    ay[i].envDirection = 1;
  }
  // The 6522s' flags are clear now, so we're not holding the IRQ line. (The
  // SC-01 has no reset pin, so it keeps going.)
  nmiLow = false;
  if (g_cpu) g_cpu->deassertIrq(irqSource);
}

// RES clears every 6522 register but the timers' counters and latches and
// the shift register. With ACR at 0, T1 is one-shot and spent, so neither
// timer flags again until it's written. The AYs and the SC-01 aren't on the
// line.
void Mockingboard::busReset()
{
  // The CPU's Reset() just restarted the cycle clock
  update(g_cpu->cycles);
  for (int i = 0; i < 2; i++) {
    Via6522 &p = via[i];
    p.orb = p.ora = 0;
    p.ddrb = p.ddra = 0;
    p.acr = p.pcr = 0;
    p.ifr = p.ier = 0;
    p.t1Armed = false;
    p.t1NextFlag = INT64_MAX;
    p.t2NextFlag = INT64_MAX;
  }
  nmiLow = false;
  if (g_cpu) g_cpu->deassertIrq(irqSource);
  scheduleNext();
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
  update(g_cpu->busCycle());
  int whichVia = (addr & 0x80) ? 1 : 0;
  return viaRead(whichVia, addr & 0x0F);
}

void Mockingboard::writeSlotRom(uint8_t addr, uint8_t val)
{
  update(g_cpu->busCycle());
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
    viaOrbAccess(v);
    return (p.orb & p.ddrb) | (~p.ddrb & 0xFF);
  case 0x01:
  case 0x0F:
    // Output pins read back what we drive; input pins read what's on the
    // wire, which is $FF unless an AY is putting a register there
    return (p.ora & p.ddra) | (p.paPins & ~p.ddra);
  case 0x02:
    return p.ddrb;
  case 0x03:
    return p.ddra;
  case 0x04:
    p.ifr &= ~IFR_TIMER1;
    viaUpdateIFR(v);
    return t1Value(v, g_cpu->busCycle()) & 0xFF;
  case 0x05:
    return t1Value(v, g_cpu->busCycle()) >> 8;
  case 0x06:
    return p.timer1latch & 0xFF;
  case 0x07:
    return (p.timer1latch >> 8) & 0xFF;
  case 0x08:
    p.ifr &= ~IFR_TIMER2;
    viaUpdateIFR(v);
    return t2Value(v, g_cpu->busCycle()) & 0xFF;
  case 0x09:
    return t2Value(v, g_cpu->busCycle()) >> 8;
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
  int64_t now = g_cpu->busCycle();

  switch (reg) {
  case 0x00:
    p.orb = (p.orb & ~p.ddrb) | (val & p.ddrb);
    viaOrbAccess(v);
    // On a Sound/Speech I, the $Cn00 6522's port B feeds the SC-01's
    // phoneme and inflection pins, and CB2 is its strobe. When CB2 is in a
    // mode that drops on an ORB write, the write is a phoneme.
    if (hasSpeech && v == 0) {
      if ((p.pcr & PCR_CB2_STB_MASK) == PCR_CB2_STB) {
        // Pins set as inputs float high
        sc01Write((p.orb & p.ddrb) | (~p.ddrb & 0xFF));
      }
      break;
    }
    if (viaHasAY[v])
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
  case 0x06:
    t1Rebase(v, now, (p.timer1latch & 0xFF00) | val);
    break;
  case 0x05:
    // The counter takes the latch on the cycle after the write, and the
    // flag comes when it rolls from 0 to $FFFF: N+2 cycles after the write
    p.timer1latch = (p.timer1latch & 0x00FF) | ((uint16_t)val << 8);
    p.t1Base = now + 1;
    p.t1Start = p.timer1latch;
    p.t1Armed = true;
    p.t1NextFlag = p.t1Base + p.t1Start + 1;
    p.ifr &= ~IFR_TIMER1;
    viaUpdateIFR(v);
    break;
  case 0x07:
    t1Rebase(v, now, (p.timer1latch & 0x00FF) | ((uint16_t)val << 8));
    p.ifr &= ~IFR_TIMER1;
    viaUpdateIFR(v);
    break;
  case 0x08:
    p.timer2latchLow = val;
    break;
  case 0x09:
    p.t2Base = now + 1;
    p.t2Start = ((uint16_t)val << 8) | p.timer2latchLow;
    p.t2NextFlag = p.t2Base + p.t2Start + 1;
    p.ifr &= ~IFR_TIMER2;
    viaUpdateIFR(v);
    break;
  case 0x0A:
    p.sr = val;
    break;
  case 0x0B:
    p.acr = val;
    t1Reschedule(v, now);
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

void Mockingboard::viaSetFlag(int v, uint8_t mask, int64_t when)
{
  via[v].ifr |= mask;
  viaUpdateIFR(v, when);
}

// 'when' is the cycle a flag that just got set went up, so the CPU can see
// the interrupt at the right instruction boundary. Anything else (a write to
// IER, say) changes the line on the current bus cycle.
void Mockingboard::viaUpdateIFR(int v, int64_t when)
{
  if (when < 0) when = g_cpu->busCycle();
  Via6522 &p = via[v];
  if (p.ifr & p.ier & 0x7F)
    p.ifr |= IFR_IRQ;
  else
    p.ifr &= ~IFR_IRQ;

  bool anyIrq = (via[0].ifr & IFR_IRQ) != 0;
  if (via1Nmi) {
    // The $Cn80 6522 on a Sound/Speech I is wired to NMI, which only
    // interrupts when the line falls
    bool low = (via[1].ifr & IFR_IRQ) != 0;
    if (low && !nmiLow)
      g_cpu->triggerNmi(when);
    nmiLow = low;
  } else if (via[1].ifr & IFR_IRQ) {
    anyIrq = true;
  }
  if (anyIrq)
    g_cpu->assertIrq(irqSource, when);
  else
    g_cpu->deassertIrq(irqSource);
}

// Reading or writing ORB clears the CB1 flag, and the CB2 flag too unless
// CB2 is in one of its "independent" input modes.
void Mockingboard::viaOrbAccess(int v)
{
  Via6522 &p = via[v];
  uint8_t clear = IFR_CB1;
  if ((p.pcr & PCR_CB2_IND_MASK) != PCR_CB2_IND)
    clear |= IFR_CB2;
  if (p.ifr & clear) {
    p.ifr &= ~clear;
    viaUpdateIFR(v);
  }
}

// --- SC-01 (Sound/Speech I only) ---

// The strobe: latch the phoneme, drop A/R, and schedule A/R to rise when the
// phoneme's time is up. The 6522 sees A/R on CB1.
void Mockingboard::sc01Write(uint8_t busValue)
{
  sc01.phone = busValue & 0x3F;
  sc01.inflection = (busValue >> 6) & 0x03;
  if (voice) voice->write(sc01.phone, sc01.inflection);
  int64_t now = g_cpu->busCycle();
  sc01SetAR(false, now);
  sc01.arRiseCycle = now + sc01PhoneCycles(sc01.phone);
  scheduleNext();
}

void Mockingboard::sc01SetAR(bool high, int64_t when)
{
  if (high == sc01.arHigh) return;
  sc01.arHigh = high;
  // CB1 flags on whichever edge PCR bit 4 picks. Speech software sets PCR
  // to $B0, so it's the rising edge: "the last phoneme is done".
  bool wantRising = (via[0].pcr & PCR_CB1_POSITIVE) != 0;
  if (high == wantRising)
    viaSetFlag(0, IFR_CB1, when);
}

// --- AY-3-8910 bus protocol ---

void Mockingboard::handleOrbChange(int v)
{
  uint8_t orb = via[v].orb;

  // The AY only drives the bus while it's being read
  via[v].paPins = 0xFF;

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

// What the AY sees on its data bus: port A's outputs, and $FF on any pin
// that's an input (nothing else drives it)
static uint8_t portABus(const Via6522 &p)
{
  return (p.ora & p.ddra) | (uint8_t)~p.ddra;
}

// The AY latches all 8 bits. It only answers to addresses whose top 4 bits
// are 0, so anything else (and the state after a reset) selects nothing.
void Mockingboard::ayLatchAddress(int a)
{
  ay[a].latchedReg = portABus(via[a]);
}

// The AY-3-8913 only keeps the bits each register uses, and reads back 0 in
// the rest. (A YM2149 keeps all 8, which is how software tells them apart.)
static const uint8_t ayRegMask[AY_NUM_REGS] = {
  0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
  0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};

void Mockingboard::ayWriteReg(int a)
{
  uint8_t reg = ay[a].latchedReg;
  if (reg >= AY_NUM_REGS) return;
  uint8_t mask = ayRegMask[reg];
  // The 8913 has no I/O ports, so it has no use for R7's port direction bits
  if (reg == 7 && !ayHasPorts) mask = 0x3F;
  ay[a].regs[reg] = portABus(via[a]) & mask;
  // Writing the shape restarts the envelope, even with the same value
  ayRecalc(a, reg == 13);
}

void Mockingboard::ayReadReg(int a)
{
  uint8_t reg = ay[a].latchedReg;
  if (reg >= AY_NUM_REGS) return;
  uint8_t val = ay[a].regs[reg];
  if (reg >= 14) {
    if (!ayHasPorts) {
      // The 8913 has no I/O ports and leaves the bus alone, so port A still
      // holds the address we latched ($0E or $0F)
      val = reg;
    } else if (!(ay[a].regs[7] & (reg == 14 ? 0x40 : 0x80))) {
      // An 8910 port set as an input reads its pins, which nothing on the
      // card drives, so they're pulled up
      val = 0xFF;
    }
  }
  via[a].paPins = val;
}

void Mockingboard::ayReset(int a)
{
  memset(&ay[a], 0, sizeof(AY8910));
  ay[a].latchedReg = 0xFF;
  ay[a].noiseShift = 1;
  ay[a].noisePeriod = 1;
  ay[a].envPeriod = 1;
  for (int ch = 0; ch < AY_NUM_CHANNELS; ch++)
    ay[a].tonePeriod[ch] = 1;
  ay[a].envDirection = 1;
}

void Mockingboard::ayRecalc(int a, bool restartEnvelope)
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

  if (restartEnvelope) {
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
    if (!viaHasAY[a]) continue;
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

  if (voice)
    sum += voice->render(SAMPLE_RATE);

  // Clamp to int16 range
  if (sum > 0x7FFF) sum = 0x7FFF;
  if (sum < -0x7FFF) sum = -0x7FFF;
  return (int16_t)sum;
}

// Called from cpuMaintenance with the current cycle count.
// --- 6522 timers ---
//
// Timer 1 counts down from t1Start, which it held on cycle t1Base. When it
// gets past 0 it shows $FFFF for a cycle, then reloads from the latch, in
// one-shot mode too, so a period is latch+2 cycles. The flag goes up on the $FFFF cycle: every time in free-running
// mode, and in one-shot mode only until the first one-shot timeout after a
// T1C-H write.

uint16_t Mockingboard::t1Value(int v, int64_t r)
{
  Via6522 &p = via[v];
  int64_t k = r - p.t1Base;
  if (k < 0) return p.t1Start;
  if (k <= p.t1Start) return p.t1Start - k;
  int64_t j = k - p.t1Start - 1;          // 0 is the first $FFFF cycle
  if (j == 0) return 0xFFFF;
  int64_t period = (int64_t)p.timer1latch + 2;
  int64_t m = (j - 1) % period;
  return (m <= p.timer1latch) ? p.timer1latch - m : 0xFFFF;
}

// The first cycle after r that T1 rolls to $FFFF
int64_t Mockingboard::t1UnderflowAfter(int v, int64_t r)
{
  Via6522 &p = via[v];
  int64_t a = p.t1Base + p.t1Start + 1;
  if (a > r) return a;
  int64_t period = (int64_t)p.timer1latch + 2;
  return a + ((r - a) / period + 1) * period;
}

// The latch is changing at cycle r. The count in progress carries on, but
// the reloads after it use the new latch, so start the arithmetic over from
// here.
void Mockingboard::t1Rebase(int v, int64_t r, uint16_t newLatch)
{
  Via6522 &p = via[v];
  int64_t k = r - p.t1Base;
  if (k > p.t1Start) {
    int64_t j = k - p.t1Start - 1;
    int64_t period = (int64_t)p.timer1latch + 2;
    bool reloadNext = (j == 0) || ((j - 1) % period == period - 1);
    if (reloadNext) {
      // On the $FFFF cycle: the next one loads the new latch
      p.timer1latch = newLatch;
      p.t1Base = r + 1;
      p.t1Start = newLatch;
      t1Reschedule(v, r);
      return;
    }
  }
  uint16_t now = t1Value(v, r);
  p.timer1latch = newLatch;
  p.t1Base = r;
  p.t1Start = now;
  t1Reschedule(v, r);
}

void Mockingboard::t1Reschedule(int v, int64_t r)
{
  Via6522 &p = via[v];
  if ((p.acr & 0x40) || p.t1Armed)
    p.t1NextFlag = t1UnderflowAfter(v, r);
  else
    p.t1NextFlag = INT64_MAX;
}

// Timer 2 has no high latch: after 0 it carries on from $FFFF. It flags once
// per T2C-H write.
uint16_t Mockingboard::t2Value(int v, int64_t r)
{
  Via6522 &p = via[v];
  int64_t k = r - p.t2Base;
  if (k < 0) return p.t2Start;
  return (uint16_t)((p.t2Start - k) & 0xFFFF);
}

// The CPU's cycle clock restarts on a reset (and jumps on a restore), and
// everything here is stamped with it. Move our stamps by the same amount so
// the timers and the SC-01 carry on where they were.
void Mockingboard::rebaseClock(int64_t delta)
{
  for (int v = 0; v < 2; v++) {
    Via6522 &p = via[v];
    p.t1Base += delta;
    p.t2Base += delta;
    if (p.t1NextFlag != INT64_MAX) p.t1NextFlag += delta;
    if (p.t2NextFlag != INT64_MAX) p.t2NextFlag += delta;
  }
  sc01.arRiseCycle += delta;
  lastUpdate += delta;
}

void Mockingboard::update(int64_t now)
{
  // A read or write is stamped a few cycles past the instruction boundary,
  // so small steps backward are normal. A big one means the clock restarted.
  if (now + 1000 < lastUpdate)
    rebaseClock(now - lastUpdate);
  if (now > lastUpdate) lastUpdate = now;

  for (int v = 0; v < 2; v++) {
    Via6522 &p = via[v];
    if (p.t1NextFlag <= now) {
      int64_t when = p.t1NextFlag;
      // Only a one-shot timeout uses up the one-shot: switch a free-running
      // timer to one-shot and it still flags once more
      if (p.acr & 0x40) {
        p.t1NextFlag = t1UnderflowAfter(v, now);
      } else {
        p.t1Armed = false;
        p.t1NextFlag = INT64_MAX;
      }
      viaSetFlag(v, IFR_TIMER1, when);
    }
    if (p.t2NextFlag <= now) {
      int64_t when = p.t2NextFlag;
      p.t2NextFlag = INT64_MAX;
      viaSetFlag(v, IFR_TIMER2, when);
    }
  }

  if (hasSpeech && !sc01.arHigh && sc01.arRiseCycle <= now)
    sc01SetAR(true, sc01.arRiseCycle);

  scheduleNext();
}

void Mockingboard::scheduleNext()
{
  if (!g_cpu) return;
  g_cpu->eventHook = &Mockingboard::eventHook;
  int64_t next = INT64_MAX;
  for (int v = 0; v < 2; v++) {
    if (via[v].t1NextFlag < next) next = via[v].t1NextFlag;
    if (via[v].t2NextFlag < next) next = via[v].t2NextFlag;
  }
  if (hasSpeech && !sc01.arHigh && sc01.arRiseCycle < next)
    next = sc01.arRiseCycle;
  if (next != INT64_MAX)
    g_cpu->scheduleEvent(next);
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
