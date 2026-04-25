#include "wsola-speaker.h"
#include <string.h>
#include <stdint.h>

#ifdef TEENSYDUINO
#include <Arduino.h>
#endif

// --------------------------------------------------------------------
// Apple II speaker time-scale resampler.
//
//   wsola_toggle(c, level)   writes emu-rate samples to `emuBuf` at
//                            44.1 kHz *emulated* time — bit-exactly
//                            what the pre-WSOLA code wrote into its
//                            soundBuf. Flat blocks of the post-flip
//                            level, exactly as Apple II speaker code
//                            expects (the speaker / amp / ear acts
//                            as the low-pass filter that decodes the
//                            PWM pattern).
//
//   wsola_produce(out, N)    drains N wall-clock samples. At 1×
//                            emulation (ratio ≈ 1) this is a bit-
//                            exact memcpy from emuBuf to out, which
//                            preserves every sample the game toggled
//                            — critical for fake-polyphonic music.
//                            Only when ratio exceeds RATIO_WSOLA_ON
//                            (i.e., emulator is sustainedly faster
//                            than real-time) does WSOLA kick in.
// --------------------------------------------------------------------

#define AUDIO_SAMPLE_RATE 44100

#define EMU_BUF_SAMPLES 32768
#define EMU_BUF_MASK    (EMU_BUF_SAMPLES - 1)

#define WSOLA_FRAME     256
#define WSOLA_OVERLAP   128
#define WSOLA_SEARCH    64
#define WSOLA_SYNHOP    (WSOLA_FRAME - WSOLA_OVERLAP)   // 128

#define PENDING_BUF_SAMPLES 512

// Target lag: how much emu audio we aim to keep buffered after each
// callback.
#define TARGET_LAG          2048

// Only engage WSOLA when the buffer is sustainedly beyond this ratio.
// Below it we do bit-exact passthrough — essential for PWM-based
// polyphonic music whose audible content lives in the sequence of
// constant-block polarities. WSOLA's crossfade smears those.
#define RATIO_WSOLA_ON      1.50
#define WSOLA_ENGAGE_CHUNKS 50

#define MAX_RATIO           5

#ifdef TEENSYDUINO
EXTMEM static int16_t emuBuf[EMU_BUF_SAMPLES];
#else
static int16_t emuBuf[EMU_BUF_SAMPLES];
#endif

static volatile uint64_t emuWriteIdx = 0;
static volatile uint64_t emuReadIdx  = 0;
static int64_t           lastFilledTime = 0;

static int16_t prevTail[WSOLA_OVERLAP];
static bool    prevTailValid = false;
static int     silenceChunks = 0;
static int     wsolaEngageCount = 0;

static int16_t pendingBuf[PENDING_BUF_SAMPLES];
static int     pendingFill = 0;

static bool    internalToggleState = false;
static int16_t lastWrittenLevel = 0;
static int16_t lastOutputSample = 0;

// Duty-cycle integration: track the exact cycle of each state change
// to correctly render PWM/DAC audio that toggles faster than 44.1 kHz.
static int64_t lastToggleCycle = 0;
static int64_t highCyclesAccum = 0;
static int64_t sampleStartCycle = 0;
static int16_t cachedHigh = 0;
static int16_t cachedLow  = 0;

// --- Public API ---

void wsola_reset()
{
  memset(emuBuf, 0, sizeof(emuBuf));
  memset(prevTail, 0, sizeof(prevTail));
  memset(pendingBuf, 0, sizeof(pendingBuf));
  emuWriteIdx = 0;
  emuReadIdx  = 0;
  lastFilledTime = 0;
  prevTailValid = false;
  silenceChunks = 0;
  wsolaEngageCount = 0;
  pendingFill = 0;
  internalToggleState = false;
  lastWrittenLevel = 0;
  lastOutputSample = 0;
  lastToggleCycle = 0;
  highCyclesAccum = 0;
  sampleStartCycle = 0;
  cachedHigh = 0;
  cachedLow  = 0;
}

// Emit integrated samples into emuBuf up to (but not including) the
// sample containing `upToCycle`. Each sample's value is the duty-cycle
// weighted average of HIGH and LOW based on how many cycles the speaker
// spent at each level during that sample period.
static void emitSamplesUpTo(int64_t upToCycle)
{
  // Cycles per sample = 1023000 / 44100 ≈ 23.2
  // Sample N starts at cycle: N * 1023000 / 44100 (using lastFilledTime as base)
  // We iterate sample-by-sample from lastFilledTime.

  while (true) {
    int64_t nextSampleStart = (lastFilledTime + 1) * (int64_t)1023000 / AUDIO_SAMPLE_RATE;
    if (nextSampleStart > upToCycle) break;

    // Close out the current sample: accumulate remaining cycles
    int64_t remain = nextSampleStart - lastToggleCycle;
    if (remain > 0 && internalToggleState) highCyclesAccum += remain;

    // Compute integrated level for this sample
    int64_t totalCycles = nextSampleStart - sampleStartCycle;
    int16_t level;
    if (totalCycles > 0) {
      level = (int16_t)(((int32_t)cachedHigh * highCyclesAccum +
                         (int32_t)cachedLow * (totalCycles - highCyclesAccum))
                        / totalCycles);
    } else {
      level = internalToggleState ? cachedHigh : cachedLow;
    }

    // Ring overrun check
    uint64_t available = emuWriteIdx - emuReadIdx;
    if (available >= EMU_BUF_SAMPLES) {
      emuReadIdx = emuWriteIdx - EMU_BUF_SAMPLES + 1;
      prevTailValid = false;
    }

    emuBuf[emuWriteIdx & EMU_BUF_MASK] = level;
    emuWriteIdx++;
    lastFilledTime++;
    lastWrittenLevel = level;

    // Start new sample period
    sampleStartCycle = nextSampleStart;
    lastToggleCycle = nextSampleStart;
    highCyclesAccum = 0;
  }

  // Accumulate partial cycles into current (incomplete) sample
  int64_t partial = upToCycle - lastToggleCycle;
  if (partial > 0 && internalToggleState) highCyclesAccum += partial;
  lastToggleCycle = upToCycle;
}

void wsola_toggle(int64_t cycles, int16_t highLevel, int16_t lowLevel)
{
  cachedHigh = highLevel;
  cachedLow  = lowLevel;

  if (lastFilledTime == 0) {
    lastFilledTime = cycles * (int64_t)AUDIO_SAMPLE_RATE / 1023000;
    sampleStartCycle = cycles;
    lastToggleCycle = cycles;
  }

  // Emit any complete samples up to this toggle
  emitSamplesUpTo(cycles);

  // Flip speaker state
  internalToggleState = !internalToggleState;
  lastWrittenLevel = internalToggleState ? highLevel : lowLevel;
}

void wsola_flush(int64_t cycles)
{
  if (lastFilledTime == 0 || cycles <= 0) return;
  emitSamplesUpTo(cycles);
}

bool wsola_has_primed_fill(int minSamples)
{
  return (int64_t)(emuWriteIdx - emuReadIdx) >= (int64_t)minSamples;
}

int wsola_peek_emubuf(int16_t *out, int count)
{
  uint64_t avail = emuWriteIdx - emuReadIdx;
  int n = (int)((avail < (uint64_t)count) ? avail : (uint64_t)count);
  for (int i = 0; i < n; i++) {
    out[i] = emuBuf[(emuReadIdx + i) & EMU_BUF_MASK];
  }
  return n;
}

int64_t wsola_buffered()
{
  return (int64_t)(emuWriteIdx - emuReadIdx);
}

// --- WSOLA core (only used when ratio >= RATIO_WSOLA_ON) ---
//
// Produces one SYN_HOP chunk of output into dst[0..SYN_HOP). Reads
// from emuBuf starting at readIdxBase. Returns logical input samples
// consumed (ratio × SYN_HOP), or -1 on underrun.
static int wsolaEmitOneSynhop(int16_t *dst, double ratio,
                              uint64_t readIdxBase, uint64_t available)
{
  if (available < (uint64_t)WSOLA_FRAME) return -1;

  int64_t baseIdx = 0;

  int bestOffset = 0;
  if (prevTailValid) {
    int searchMin = -WSOLA_SEARCH, searchMax = WSOLA_SEARCH;
    if (baseIdx + searchMin < 0) searchMin = (int)(-baseIdx);
    if ((uint64_t)(baseIdx + searchMax) + WSOLA_OVERLAP > available) {
      int64_t m = (int64_t)available - (int64_t)WSOLA_OVERLAP - baseIdx;
      if (m < searchMax) searchMax = (int)m;
    }
    int64_t bestCorr = INT64_MIN;
    for (int off = searchMin; off <= searchMax; off++) {
      int64_t idxBase = baseIdx + off;
      int64_t corr = 0;
      for (int j = 0; j < WSOLA_OVERLAP; j++) {
        uint64_t ring = (readIdxBase + idxBase + j) & EMU_BUF_MASK;
        corr += (int32_t)(prevTail[j] >> 2) * (int32_t)(emuBuf[ring] >> 2);
      }
      if (corr > bestCorr) { bestCorr = corr; bestOffset = off; }
    }
  }

  int64_t frameStart = baseIdx + bestOffset;
  if (frameStart < 0) frameStart = 0;
  if ((uint64_t)(frameStart + WSOLA_FRAME) > available) return -1;

  int16_t frame[WSOLA_FRAME];
  for (int j = 0; j < WSOLA_FRAME; j++) {
    uint64_t ring = (readIdxBase + frameStart + j) & EMU_BUF_MASK;
    frame[j] = emuBuf[ring];
  }

  if (prevTailValid) {
    for (int j = 0; j < WSOLA_SYNHOP; j++) {
      int32_t wIn  = j;
      int32_t wOut = WSOLA_OVERLAP - j;
      int32_t sum  = wOut * (int32_t)prevTail[j] + wIn * (int32_t)frame[j];
      dst[j] = (int16_t)(sum / WSOLA_OVERLAP);
    }
  } else {
    for (int j = 0; j < WSOLA_SYNHOP; j++) dst[j] = frame[j];
  }

  for (int j = 0; j < WSOLA_OVERLAP; j++) {
    prevTail[j] = frame[WSOLA_FRAME - WSOLA_OVERLAP + j];
  }
  prevTailValid = true;

  return (int)(ratio * (double)WSOLA_SYNHOP + 0.5);
}

// Append one SYN_HOP chunk to pendingBuf.
static void appendOneChunk()
{
  if (pendingFill + WSOLA_SYNHOP > PENDING_BUF_SAMPLES) return;

  uint64_t writeIdx = emuWriteIdx;
  uint64_t readIdx  = emuReadIdx;
  uint64_t available = writeIdx - readIdx;

  // Ring overrun recovery.
  if (available > EMU_BUF_SAMPLES - WSOLA_FRAME) {
    readIdx = writeIdx - (EMU_BUF_SAMPLES - WSOLA_FRAME);
    available = writeIdx - readIdx;
    emuReadIdx = readIdx;
    prevTailValid = false;
  }

  // Compute target ratio based on buffer state.
  int64_t desired = (int64_t)available - (int64_t)TARGET_LAG;
  if (desired < WSOLA_SYNHOP) desired = WSOLA_SYNHOP;    // ratio ≥ 1
  if (desired > (int64_t)MAX_RATIO * WSOLA_SYNHOP) desired = MAX_RATIO * WSOLA_SYNHOP;
  if ((uint64_t)desired > available) desired = (int64_t)available;
  double ratio = (double)desired / (double)WSOLA_SYNHOP;

  if (ratio >= RATIO_WSOLA_ON) {
    if (wsolaEngageCount < WSOLA_ENGAGE_CHUNKS) wsolaEngageCount++;
  } else {
    wsolaEngageCount = 0;
  }

  if (ratio < RATIO_WSOLA_ON || wsolaEngageCount < WSOLA_ENGAGE_CHUNKS) {
    int n = WSOLA_SYNHOP;
    if ((uint64_t)n > available) n = (int)available;
    if (n == WSOLA_SYNHOP) {
      for (int j = 0; j < n; j++) {
        pendingBuf[pendingFill + j] = emuBuf[(readIdx + j) & EMU_BUF_MASK];
      }
    } else if (n >= 2) {
      // Underrun: stretch available samples via linear interpolation
      // instead of sample-and-hold, which causes audible choppiness.
      for (int j = 0; j < WSOLA_SYNHOP; j++) {
        int32_t srcPos = j * (n - 1);
        int idx = srcPos / (WSOLA_SYNHOP - 1);
        int frac = srcPos % (WSOLA_SYNHOP - 1);
        int16_t s0 = emuBuf[(readIdx + idx) & EMU_BUF_MASK];
        int16_t s1 = emuBuf[(readIdx + idx + 1) & EMU_BUF_MASK];
        if (idx + 1 >= n) s1 = s0;
        pendingBuf[pendingFill + j] =
          (int16_t)(s0 + ((int32_t)(s1 - s0) * frac) / (WSOLA_SYNHOP - 1));
      }
    } else {
      int16_t hold = (n > 0) ? emuBuf[readIdx & EMU_BUF_MASK] : lastOutputSample;
      for (int j = 0; j < WSOLA_SYNHOP; j++) pendingBuf[pendingFill + j] = hold;
    }
    for (int j = 0; j < WSOLA_OVERLAP; j++) {
      prevTail[j] = pendingBuf[pendingFill + WSOLA_SYNHOP - WSOLA_OVERLAP + j];
    }
    prevTailValid = true;
    pendingFill += WSOLA_SYNHOP;
    emuReadIdx = readIdx + (uint64_t)n;
    silenceChunks = 0;
    return;
  }

  // WSOLA path: engaged only when emulator is sustainedly faster
  // than real-time (ratio ≥ RATIO_WSOLA_ON). Preserves pitch while
  // compressing input:output by ratio.
  int consumed = wsolaEmitOneSynhop(&pendingBuf[pendingFill], ratio,
                                    readIdx, available);
  if (consumed < 0) {
    // Not enough input for a full frame. Drop back to passthrough
    // with linear interpolation stretching when underrunning.
    int n = (int)available;
    if (n > WSOLA_SYNHOP) n = WSOLA_SYNHOP;
    if (n >= 2 && n < WSOLA_SYNHOP) {
      for (int j = 0; j < WSOLA_SYNHOP; j++) {
        int32_t srcPos = j * (n - 1);
        int idx = srcPos / (WSOLA_SYNHOP - 1);
        int frac = srcPos % (WSOLA_SYNHOP - 1);
        int16_t s0 = emuBuf[(readIdx + idx) & EMU_BUF_MASK];
        int16_t s1 = emuBuf[(readIdx + idx + 1) & EMU_BUF_MASK];
        if (idx + 1 >= n) s1 = s0;
        pendingBuf[pendingFill + j] =
          (int16_t)(s0 + ((int32_t)(s1 - s0) * frac) / (WSOLA_SYNHOP - 1));
      }
    } else if (n == WSOLA_SYNHOP) {
      for (int j = 0; j < n; j++)
        pendingBuf[pendingFill + j] = emuBuf[(readIdx + j) & EMU_BUF_MASK];
    } else {
      int16_t hold = (n > 0) ? emuBuf[readIdx & EMU_BUF_MASK] : lastOutputSample;
      for (int j = 0; j < WSOLA_SYNHOP; j++) pendingBuf[pendingFill + j] = hold;
    }
    pendingFill += WSOLA_SYNHOP;
    emuReadIdx = readIdx + (uint64_t)n;
    silenceChunks = (n == 0) ? (silenceChunks + 1) : 0;
    if (silenceChunks > AUDIO_SAMPLE_RATE / WSOLA_SYNHOP) prevTailValid = false;
    return;
  }
  pendingFill += WSOLA_SYNHOP;
  emuReadIdx = readIdx + (uint64_t)consumed;
  silenceChunks = 0;
}

void wsola_produce(int16_t *output, int count)
{
  if (count <= 0) return;

  int written = 0;
  while (written < count) {
    if (pendingFill > 0) {
      int take = count - written;
      if (take > pendingFill) take = pendingFill;
      memcpy(&output[written], pendingBuf, take * sizeof(int16_t));
      written += take;
      if (take < pendingFill) {
        memmove(pendingBuf, &pendingBuf[take],
                (pendingFill - take) * sizeof(int16_t));
      }
      pendingFill -= take;
      lastOutputSample = output[written - 1];
      if (written >= count) break;
    }

    int guard = 0;
    while (pendingFill == 0 && guard++ < 16) {
      appendOneChunk();
    }
    if (pendingFill == 0) {
      while (written < count) output[written++] = lastOutputSample;
      break;
    }
  }
}
