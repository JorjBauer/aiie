// The Votrax SC-01's voice. This is a port of Olivier Galibert's
// circuit-level model of the chip, built from die photographs, and it's
// under his license:
//
// Copyright (c) Olivier Galibert
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// The phoneme parameters are NOT the chip's: they're the clean-room table in
// sc01params.h. How the filters are derived from the capacitor values is
// written up in aiie-docs/sc01-rom-analysis.md.

#include "sc01.h"
#include "sc01params.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Scales the model's output to about the level of one AY at full volume
#define OUTPUT_GAIN 16000.0

// The pulse from the transistor ladder, one step per 8 pitch counts
static const double glottalWave[9] = {
  0, -4/7.0, 7/7.0, 6/7.0, 5/7.0, 4/7.0, 3/7.0, 2/7.0, 1/7.0
};

template<int N> static void shiftHist(double val, double (&hist)[N])
{
  for (int i = N-1; i > 0; i--)
    hist[i] = hist[i-1];
  hist[0] = val;
}

// 'a' applies to the inputs and 'b' to the outputs
template<int Nx, int Ny, int Na, int Nb>
static double applyFilter(const double (&x)[Nx], const double (&y)[Ny],
                          const double (&a)[Na], const double (&b)[Nb])
{
  double total = 0;
  for (int i = 0; i < Na; i++)
    total += x[i] * a[i];
  for (int i = 1; i < Nb; i++)
    total -= y[i-1] * b[i];
  return total / b[0];
}

// The switched capacitors that are in, one per bit of 'value'
static double bitsToCaps(uint32_t value, const double *caps, int n)
{
  double total = 0;
  for (int i = 0; i < n; i++) {
    if (value & 1)
      total += caps[i];
    value >>= 1;
  }
  return total;
}

static const double F1_CAPS[4]  = { 2546, 4973, 9861, 19724 };
static const double F2Q_CAPS[4] = { 1390, 2965, 5875, 11297 };
static const double F2_CAPS[5]  = { 833, 1663, 3164, 6327, 12654 };
static const double F3_CAPS[4]  = { 2226, 4485, 9056, 18111 };

SC01Synth::SC01Synth(double masterClock)
{
  mainClock = masterClock;
  sClock = mainClock / 18.0;
  cClock = mainClock / 36.0;
  writeSeq = 0;
  seenSeq = 0;
  pendingPhone = 0x3F;
  pendingInflection = 0;
  reset();
}

// There's no reset pin, and the chip powers up in a random state. We start it
// quiet, on STOP.
void SC01Synth::reset()
{
  phone = 0x3F;
  inflection = 0;
  sampleCount = 0;

  phoneCommit();

  curFa = curFc = curVa = 0;
  curF1 = curF2 = curF2q = curF3 = 0;
  filtFa = filtFc = filtVa = 0;
  filtF1 = filtF2 = filtF2q = filtF3 = 0;
  filtersCommit(true);

  pitch = 0;
  closure = 0;
  updateCounter = 0;
  curClosure = true;
  noise = 0;
  curNoise = false;

  memset(voice1, 0, sizeof(voice1));
  memset(voice2, 0, sizeof(voice2));
  memset(voice3, 0, sizeof(voice3));
  memset(noise1, 0, sizeof(noise1));
  memset(noise2, 0, sizeof(noise2));
  memset(noise3, 0, sizeof(noise3));
  memset(noise4, 0, sizeof(noise4));
  memset(vn1, 0, sizeof(vn1));
  memset(vn2, 0, sizeof(vn2));
  memset(vn3, 0, sizeof(vn3));
  memset(vn4, 0, sizeof(vn4));
  memset(vn5, 0, sizeof(vn5));
  memset(vn6, 0, sizeof(vn6));

  phase = 0;
  lastOut = 0;
}

void SC01Synth::write(uint8_t p, uint8_t infl)
{
  pendingPhone = p & 0x3F;
  pendingInflection = infl & 0x03;
  // The audio thread checks the sequence number, so it goes last
  __atomic_store_n(&writeSeq, writeSeq + 1, __ATOMIC_RELEASE);
}

// The strobe is 72 master clocks ahead of the commit, which is less than one
// of our samples, so we commit as soon as we see it.
void SC01Synth::takePending()
{
  uint32_t seq = __atomic_load_n(&writeSeq, __ATOMIC_ACQUIRE);
  if (seq == seenSeq) return;
  seenSeq = seq;
  phone = pendingPhone;
  inflection = pendingInflection;
  phoneCommit();
}

void SC01Synth::phoneCommit()
{
  // Only these two counters restart on a new phoneme; the rest run freely
  phoneTick = 0;
  ticks = 0;

  const SC01Phone &p = sc01Phones[phone & 0x3F];
  romF1 = p.f1;
  romVa = p.va;
  romF2 = p.f2;
  romFc = p.fc;
  romF2q = p.f2q;
  romF3 = p.f3;
  romFa = p.fa;
  romCld = p.cld;
  romVd = p.vd;
  romClosure = p.cl != 0;
  romDuration = p.dur & 0x7F;

  // The pauses are wired on the die, not in the table
  romPause = (phone == 0x03) || (phone == 0x3E);

  if (romCld == 0)
    curClosure = romClosure;
}

// One step: add an eighth of the distance to the target
static inline void interpolate(uint8_t &reg, uint8_t target)
{
  reg = reg - (reg >> 3) + (target << 1);
}

// Runs at master clock / 36
void SC01Synth::chipUpdate()
{
  // 16 ticks per phoneme, each DUR*4+1 updates long (the comparator path has
  // a one-update delay). The counter stops comparing at 16.
  if (ticks != 0x10) {
    phoneTick++;
    if (phoneTick == ((romDuration << 2) | 1)) {
      phoneTick = 0;
      ticks++;
      if (ticks == romCld)
        curClosure = romClosure;
    }
  }

  // Two update clocks, divide by 16 (625Hz at 720kHz) and by 48 (208Hz),
  // phased so the slow one lands between two fast ones
  updateCounter++;
  if (updateCounter == 0x30)
    updateCounter = 0;
  bool tick625 = !(updateCounter & 0xF);
  bool tick208 = updateCounter == 0x28;

  // The formants move at 208Hz, and freeze on a pause until both
  // amplitudes are down to 0. On the die, FC is in this group and VA in the
  // fast one (see aiie-docs/sc01-rom-analysis.md).
  if (tick208 && (!romPause || !(filtFa || filtVa))) {
    interpolate(curFc, romFc);
    interpolate(curF1, romF1);
    interpolate(curF2, romF2);
    interpolate(curF2q, romF2q);
    interpolate(curF3, romF3);
  }

  if (tick625) {
    if (ticks >= romVd)
      interpolate(curFa, romFa);
    if (ticks >= romCld)
      interpolate(curVa, romVa);
  }

  // The closure counter ramps the output gain down over a few updates
  if (!curClosure && (filtFa || filtVa))
    closure = 0;
  else if (closure != (7 << 2))
    closure++;

  // The pitch counter resets on an equality compare, two counts late. F1
  // and the inflection pins both move the pitch.
  pitch = (pitch + 1) & 0xFF;
  if (pitch == (0xE0 ^ (inflection << 5) ^ (filtF1 << 1)) + 2)
    pitch = 0;

  // The filters only take new values at one point of the pitch wave
  if ((pitch & 0xF9) == 0x08)
    filtersCommit(false);

  // 15-bit noise shift register, XNOR feedback from its top two bits
  bool inp = curNoise && (noise != 0x7FFF);
  noise = ((noise << 1) & 0x7FFE) | (inp ? 1 : 0);
  curNoise = !(((noise >> 14) ^ (noise >> 13)) & 1);
}

void SC01Synth::filtersCommit(bool force)
{
  filtFa = curFa >> 4;
  filtFc = curFc >> 4;
  filtVa = curVa >> 4;

  if (force || filtF1 != (curF1 >> 4)) {
    filtF1 = curF1 >> 4;
    buildStandardFilter(f1a, f1b, 11247, 11797, 949, 52067,
                        2280 + bitsToCaps(filtF1, F1_CAPS, 4), 166272);
  }

  if (force || filtF2 != (curF2 >> 3) || filtF2q != (curF2q >> 4)) {
    filtF2 = curF2 >> 3;
    filtF2q = curF2q >> 4;
    double q = 829 + bitsToCaps(filtF2q, F2Q_CAPS, 4);
    double f = 2352 + bitsToCaps(filtF2, F2_CAPS, 5);
    buildStandardFilter(f2va, f2vb, 24840, 29154, q, 38180, f, 34270);
    buildInjectionFilter(f2na, f2nb, 29154, q, 38180, f, 34270);
  }

  if (force || filtF3 != (curF3 >> 4)) {
    filtF3 = curF3 >> 4;
    buildStandardFilter(f3a, f3b, 0, 17594, 868, 18828,
                        8480 + bitsToCaps(filtF3, F3_CAPS, 4), 50019);
  }

  if (force) {
    buildStandardFilter(f4a, f4b, 0, 28810, 1165, 21457, 8558, 7289);
    buildLowpassFilter(fxa, fxb, 1122, 23131);
    buildNoiseShaperFilter(fna, fnb, 15500, 14854, 8450, 9523, 14083);
  }
}

// Runs at master clock / 18
double SC01Synth::analogCalc()
{
  // Voice: the pulse, scaled by VA, through F1 and F2
  double v = pitch >= (9 << 3) ? 0 : glottalWave[pitch >> 3];
  v = v * filtVa / 15.0;
  shiftHist(v, voice1);
  v = applyFilter(voice1, voice2, f1a, f1b);
  shiftHist(v, voice2);
  v = applyFilter(voice2, voice3, f2va, f2vb);
  shiftHist(v, voice3);

  // Noise: gated by the pitch counter, scaled by FA, shaped, then fed into
  // F2 scaled by FC
  double n = 1e4 * (((pitch & 0x40) ? curNoise : false) ? 1 : -1);
  n = n * filtFa / 15.0;
  shiftHist(n, noise1);
  n = applyFilter(noise1, noise2, fna, fnb);
  shiftHist(n, noise2);
  double n2 = n * filtFc / 15.0;
  shiftHist(n2, noise3);
  n2 = applyFilter(noise3, noise4, f2na, f2nb);
  shiftHist(n2, noise4);

  // Both through F3, then more noise, F4, the closure gain and a final
  // low-pass
  double vn = v + n2;
  shiftHist(vn, vn1);
  vn = applyFilter(vn1, vn2, f3a, f3b);
  shiftHist(vn, vn2);
  vn += n * (5 + (15 ^ filtFc)) / 20.0;
  shiftHist(vn, vn3);
  vn = applyFilter(vn3, vn4, f4a, f4b);
  shiftHist(vn, vn4);
  vn = vn * (7 ^ (closure >> 2)) / 7.0;
  shiftHist(vn, vn5);
  vn = applyFilter(vn5, vn6, fxa, fxb);
  shiftHist(vn, vn6);

  return vn * 0.35;
}

int16_t SC01Synth::render(double outRate)
{
  // Average the chip's samples that fall in this output sample
  phase += sClock / outRate;
  double acc = 0;
  int n = 0;
  while (phase >= 1.0) {
    phase -= 1.0;
    takePending();
    sampleCount++;
    if (sampleCount & 1)
      chipUpdate();
    acc += analogCalc();
    n++;
  }
  if (n) lastOut = acc / n;

  double out = lastOut * OUTPUT_GAIN;
  if (out > 32767) out = 32767;
  if (out < -32768) out = -32768;
  return (int16_t)out;
}

// The filters come from the circuits' transfer functions with a bilinear
// transform, pre-warped at the filter's peak. The capacitor values are die
// surfaces; only their ratios matter. A switched capacitor of C switched at
// f acts as a resistor of 1/(C*f), with f = master clock / 36.

// H(s) = (1 + k0*s) / (1 + k1*s + k2*s^2)
void SC01Synth::buildStandardFilter(double *a, double *b, double c1t, double c1b,
                                    double c2t, double c2b, double c3, double c4)
{
  double k0 = c1t / (cClock * c1b);
  double k1 = c4 * c2t / (cClock * c1b * c3);
  double k2 = c4 * c2b / (cClock * cClock * c1b * c3);

  double fpeak = sqrt(fabs(k0*k1 - k2)) / (2*M_PI*k2);
  double zc = 2*M_PI*fpeak / tan(M_PI*fpeak / sClock);

  double m0 = zc*k0;
  double m1 = zc*k1;
  double m2 = zc*zc*k2;

  a[0] = 1+m0;
  a[1] = 3+m0;
  a[2] = 3-m0;
  a[3] = 1-m0;
  b[0] = 1+m1+m2;
  b[1] = 3+m1-m2;
  b[2] = 3-m1-m2;
  b[3] = 1-m1+m2;
}

// The fixed low-pass at the end. The capacitors put it near 150Hz, but
// recordings of the chip sound like 4kHz, so it's moved there.
void SC01Synth::buildLowpassFilter(double *a, double *b, double c1t, double c1b)
{
  double k = c1b / (cClock * c1t) * (150.0/4000.0);
  double fpeak = 1/(2*M_PI*k);
  double zc = 2*M_PI*fpeak / tan(M_PI*fpeak / sClock);
  double m = zc*k;

  a[0] = 1;
  b[0] = 1+m;
  b[1] = 1-m;
}

// The noise shaper: H(s) = k0*s / (1 + k1*s + k2*s^2)
void SC01Synth::buildNoiseShaperFilter(double *a, double *b, double c1, double c2t,
                                       double c2b, double c3, double c4)
{
  double k0 = c2t*c3*c2b/c4;
  double k1 = c2t*(cClock * c2b);
  double k2 = c1*c2t*c3/(cClock * c4);

  double fpeak = sqrt(1/k2)/(2*M_PI);
  double zc = 2*M_PI*fpeak / tan(M_PI*fpeak / sClock);

  double m0 = zc*k0;
  double m1 = zc*k1;
  double m2 = zc*zc*k2;

  a[0] = m0;
  a[1] = 0;
  a[2] = -m0;
  b[0] = 1+m1+m2;
  b[1] = 2-2*m2;
  b[2] = 1-m1+m2;
}

// Noise injection into F2: H(s) = (k0 + k2*s) / (k1 + k2*s), scaled so that
// H(infinity) = 1, and not pre-warped
void SC01Synth::buildInjectionFilter(double *a, double *b, double c1b, double c2t,
                                     double c2b, double c3, double c4)
{
  (void)c4;
  double k0 = cClock * c2t;
  double k1 = cClock * (c1b * c3 / c2t - c2t);
  double k2 = c2b;

  double zc = 2*sClock;
  double m = zc*k2;

  a[0] = k0 + m;
  a[1] = k0 - m;
  b[0] = k1 + m;
  b[1] = k1 - m;
}
