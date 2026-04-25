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

static int16_t pendingBuf[PENDING_BUF_SAMPLES];
static int     pendingFill = 0;

// Internal speaker flip-state. Matches the pre-WSOLA semantics
// exactly: the state only flips when a sample actually gets written
// to emuBuf (i.e., when delta > 0). Sub-sample toggles leave it
// alone — they're effectively folded into the next real write's
// parity, which is what the physical speaker / ear chain expects
// for Apple II PWM-based polyphonic music.
static bool    internalToggleState = false;

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
  pendingFill = 0;
  internalToggleState = false;
}

void wsola_toggle(int64_t cycles, int16_t highLevel, int16_t lowLevel)
{
  // Map CPU cycle → emu-rate sample index. Bit-exact with the
  // pre-WSOLA SDL speaker at g_speed=1023000.
  int64_t sampleIdx = cycles * (int64_t)AUDIO_SAMPLE_RATE / 1023000;

  if (lastFilledTime == 0) lastFilledTime = sampleIdx;

  int64_t delta = sampleIdx - lastFilledTime;
  if (delta <= 0) {
    // Sub-sample toggle — the physical speaker DOES flip here, but
    // at rates above our output Nyquist we can't represent the
    // individual transitions. Match pre-WSOLA behavior: leave the
    // internal toggle state alone; the subsequent write will pick
    // up whichever parity actually landed at the sample boundary.
    return;
  }

  // Extended silence (seconds) can produce a huge `delta`. Cap to
  // the ring — nothing useful in a long constant-level stretch.
  if ((uint64_t)delta > EMU_BUF_SAMPLES) {
    emuReadIdx = emuWriteIdx + (uint64_t)delta - EMU_BUF_SAMPLES;
    delta = EMU_BUF_SAMPLES;
    prevTailValid = false;
  } else {
    uint64_t available = emuWriteIdx - emuReadIdx;
    if (available + (uint64_t)delta > EMU_BUF_SAMPLES) {
      uint64_t drop = (available + (uint64_t)delta) - EMU_BUF_SAMPLES + 1;
      emuReadIdx += drop;
      prevTailValid = false;
    }
  }

  // Flip INTERNAL state — we only flip when we actually write,
  // matching pre-WSOLA behavior.
  internalToggleState = !internalToggleState;
  int16_t level = internalToggleState ? highLevel : lowLevel;

  for (int64_t i = 0; i < delta; i++) {
    emuBuf[(emuWriteIdx + i) & EMU_BUF_MASK] = level;
  }
  emuWriteIdx += (uint64_t)delta;
  lastFilledTime = sampleIdx;
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

  // Fast path: bit-exact passthrough from emuBuf to pendingBuf. This
  // is the ONLY path taken at 1× emulation, and it produces exactly
  // what the pre-WSOLA SDL speaker did — essential for fake-
  // polyphonic music whose audible content is the sequence of
  // constant-block polarities.
  if (ratio < RATIO_WSOLA_ON) {
    int n = WSOLA_SYNHOP;
    if ((uint64_t)n > available) n = (int)available;
    for (int j = 0; j < n; j++) {
      pendingBuf[pendingFill + j] = emuBuf[(readIdx + j) & EMU_BUF_MASK];
    }
    // Pad with the last sample (or silence) if we ran short.
    int16_t hold = (n > 0) ? pendingBuf[pendingFill + n - 1] : 0;
    for (int j = n; j < WSOLA_SYNHOP; j++) pendingBuf[pendingFill + j] = hold;
    // Keep prevTail populated in case we transition up to WSOLA.
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
    // Not enough input for a full frame. Drop back to passthrough.
    int n = (int)available;
    if (n > WSOLA_SYNHOP) n = WSOLA_SYNHOP;
    for (int j = 0; j < n; j++) {
      pendingBuf[pendingFill + j] = emuBuf[(readIdx + j) & EMU_BUF_MASK];
    }
    int16_t hold = (n > 0) ? pendingBuf[pendingFill + n - 1] : 0;
    for (int j = n; j < WSOLA_SYNHOP; j++) pendingBuf[pendingFill + j] = hold;
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
      if (written >= count) break;
    }

    int guard = 0;
    while (pendingFill == 0 && guard++ < 16) {
      appendOneChunk();
    }
    if (pendingFill == 0) {
      while (written < count) output[written++] = 0;
      break;
    }
  }
}
