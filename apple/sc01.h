#ifndef __SC01_H
#define __SC01_H

#include <stdint.h>

// The Votrax SC-01's voice: a glottal pulse and a noise source run through
// switched-capacitor formant filters, driven by the phoneme table in
// sc01params.h. This is a port of Olivier Galibert's circuit-level model of
// the chip (BSD-3-Clause; the license is in sc01.cpp).
//
// The chip runs at its own rate (master clock / 18) and we resample to the
// output rate. write() comes from the CPU thread; render() runs on the audio
// thread, and picks up the newest write at its next sample.

class SC01Synth {
 public:
  SC01Synth(double masterClock);

  void reset();

  // A strobe: the 6-bit phoneme and the 2-bit inflection
  void write(uint8_t phone, uint8_t inflection);

  // One output sample at outRate, roughly +/-32767 at full volume
  int16_t render(double outRate);

 private:
  void takePending();
  void phoneCommit();
  void chipUpdate();
  void filtersCommit(bool force);
  double analogCalc();

  void buildStandardFilter(double *a, double *b, double c1t, double c1b,
                           double c2t, double c2b, double c3, double c4);
  void buildLowpassFilter(double *a, double *b, double c1t, double c1b);
  void buildNoiseShaperFilter(double *a, double *b, double c1, double c2t,
                              double c2b, double c3, double c4);
  void buildInjectionFilter(double *a, double *b, double c1b, double c2t,
                            double c2b, double c3, double c4);

  double mainClock, sClock, cClock;

  // From the CPU thread
  volatile uint8_t pendingPhone, pendingInflection;
  volatile uint32_t writeSeq;
  uint32_t seenSeq;

  uint8_t phone, inflection;

  // The current phoneme's parameters
  uint8_t romDuration, romVd, romCld;
  uint8_t romFa, romFc, romVa;
  uint8_t romF1, romF2, romF2q, romF3;
  bool romClosure, romPause;

  // Interpolated values (8 bits; the top 4 or 5 are used)
  uint8_t curFa, curFc, curVa;
  uint8_t curF1, curF2, curF2q, curF3;

  // What the filters were last built with
  uint8_t filtFa, filtFc, filtVa;
  uint8_t filtF1, filtF2, filtF2q, filtF3;

  uint16_t phoneTick;
  uint8_t ticks;
  uint8_t pitch;
  uint8_t closure;
  uint8_t updateCounter;
  bool curClosure;
  uint16_t noise;
  bool curNoise;
  uint32_t sampleCount;

  // Filter histories
  double voice1[4], voice2[4], voice3[4];
  double noise1[3], noise2[3], noise3[2], noise4[2];
  double vn1[4], vn2[4], vn3[4], vn4[4], vn5[2], vn6[2];

  // Filter coefficients
  double f1a[4], f1b[4];
  double f2va[4], f2vb[4];
  double f2na[2], f2nb[2];
  double f3a[4], f3b[4];
  double f4a[4], f4b[4];
  double fxa[1], fxb[2];
  double fna[3], fnb[3];

  // Resampling
  double phase;
  double lastOut;
};

#endif
