#include "sdl-speaker.h"
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

extern "C"
{
#include <SDL.h>
#include <SDL_thread.h>
};

// What values do we use for logical speaker-high and speaker-low?
#define HIGHVAL ((int16_t)((0x4FFF) >> (15-g_volume)))
#define LOWVAL  ((int16_t)(-((0x4FFF) >> (15-g_volume))))

#include "globals.h"

#define SDLSIZE (2048)
#define AUDIO_SAMPLE_RATE_EXACT 44100
#define SAMPLEBYTES sizeof(short)

// --------------------------------------------------------------------
// Pipeline:
//   toggle(c)            emu-rate samples → emuBuf (ring)
//   audioCallback()      emuBuf → WSOLA → stream (wall-clock SDLSIZE)
//
// emuBuf holds samples timed at the real Apple II clock (1.023 MHz ⇒
// 44.1 kHz emulated sample rate). That's independent of how fast the
// host is actually running the CPU. WSOLA in the audio callback does
// the time-scaling needed to fit those emulated-time samples into the
// fixed wall-clock 44.1 kHz output, *preserving pitch*.
//
// At 1× emulation: ratio ≈ 1, WSOLA passes through unchanged.
// At 4× emulation: ratio ≈ 4, WSOLA compresses 4 emulated seconds
//                  into 1 wall second without chipmunking the pitch.
// At slow emulation: ratio < 1 is clamped to 1 — we output the
//                    available samples and pad; we don't stretch.
// --------------------------------------------------------------------

#define EMU_BUF_SAMPLES 32768   // ~740 ms of headroom @ 44.1k (power of 2)
#define EMU_BUF_MASK    (EMU_BUF_SAMPLES - 1)

// WSOLA parameters — 50% overlap, ±1.5ms search.
#define WSOLA_FRAME   256
#define WSOLA_OVERLAP 128
#define WSOLA_SEARCH  64
#define WSOLA_SYNHOP  (WSOLA_FRAME - WSOLA_OVERLAP)  // 128

// Target lag: how many emu samples we aim to keep buffered after each
// callback. Roughly one SDL callback's worth — enough to absorb host
// jitter without adding much latency.
#define TARGET_LAG    SDLSIZE

// Cap compression per callback so we don't create audible dropouts if
// the emu spikes for a moment.
#define MAX_RATIO     5

static int16_t  emuBuf[EMU_BUF_SAMPLES];
static volatile uint64_t emuWriteIdx = 0;   // monotonic
static volatile uint64_t emuReadIdx  = 0;   // monotonic
static int16_t  prevTail[WSOLA_OVERLAP];
static bool     prevTailValid = false;
static int      silenceCallbacks = 0;       // consecutive underrun callbacks

static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int64_t lastFilledTime = 0;

volatile uint8_t audioRunning = 0;

// Debug: dump wall-clock output to a wav file.
//#define DEBUG_OUT_WAV
#ifdef DEBUG_OUT_WAV
static int outputFD = -1;
#endif

// --- WSOLA: compress emuBuf ring into `output` of `outputCount` samples ---
static void wsolaProcess(int16_t *output, int outputCount)
{
  uint64_t writeIdx = emuWriteIdx;
  uint64_t readIdx  = emuReadIdx;
  uint64_t available = writeIdx - readIdx;

  // Underrun: not enough to run even one analysis frame. Hold the last
  // known level (or silence if we've never had any audio).
  if (available < (uint64_t)WSOLA_FRAME) {
    int16_t hold = prevTailValid ? prevTail[WSOLA_OVERLAP - 1] : 0;
    for (int i = 0; i < outputCount; i++) output[i] = hold;
    silenceCallbacks++;
    // After ~1 s of silence, invalidate the tail so the next resume
    // doesn't crossfade into stale content from before the gap.
    if (silenceCallbacks > AUDIO_SAMPLE_RATE_EXACT / SDLSIZE) {
      prevTailValid = false;
    }
    return;
  }
  silenceCallbacks = 0;

  // Ring overrun: the emu got way ahead and wrote over unread data.
  // Force-advance readIdx to the newest data and reset crossfade state.
  if (available > EMU_BUF_SAMPLES - WSOLA_FRAME) {
    readIdx = writeIdx - (EMU_BUF_SAMPLES - WSOLA_FRAME);
    available = writeIdx - readIdx;
    emuReadIdx = readIdx;
    prevTailValid = false;
  }

  // Figure out how much input to consume this callback. Aim to leave
  // ~TARGET_LAG samples in the buffer for next time (smooths jitter).
  int64_t desired = (int64_t)available - (int64_t)TARGET_LAG;
  if (desired < outputCount) desired = outputCount;           // ratio ≥ 1
  if (desired > MAX_RATIO * outputCount) desired = MAX_RATIO * outputCount;
  if ((uint64_t)desired > available) desired = (int64_t)available;

  double ratio = (double)desired / (double)outputCount;
  double analysisHop = ratio * (double)WSOLA_SYNHOP;

  double analysisStart = 0.0;
  int outPos = 0;

  while (outPos + WSOLA_FRAME <= outputCount) {
    int64_t baseIdx = (int64_t)(analysisStart + 0.5);

    // Search ±WSOLA_SEARCH around baseIdx for the offset that makes the
    // new frame's leading OVERLAP align best with prevTail. Simple
    // integer cross-correlation (signs only matter for square-wave
    // signals, so this cheap form works well).
    int bestOffset = 0;
    if (prevTailValid) {
      int searchMin = -WSOLA_SEARCH;
      int searchMax =  WSOLA_SEARCH;
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
          uint64_t ring = (readIdx + idxBase + j) & EMU_BUF_MASK;
          // Scale down before multiplying so the running sum stays
          // within int64 for long OVERLAPs (paranoia — it's fine at
          // 128 but the shift is nearly free).
          corr += (int32_t)(prevTail[j] >> 2) * (int32_t)(emuBuf[ring] >> 2);
        }
        if (corr > bestCorr) { bestCorr = corr; bestOffset = off; }
      }
    }

    int64_t frameStart = baseIdx + bestOffset;
    if (frameStart < 0) frameStart = 0;
    if ((uint64_t)(frameStart + WSOLA_FRAME) > available) {
      // Not enough input to complete this frame — stop here.
      break;
    }

    int16_t frame[WSOLA_FRAME];
    for (int j = 0; j < WSOLA_FRAME; j++) {
      uint64_t ring = (readIdx + frameStart + j) & EMU_BUF_MASK;
      frame[j] = emuBuf[ring];
    }

    // Overlap region: linear crossfade between prevTail and new frame.
    // wIn+wOut = OVERLAP, so output fits in int16 (weighted avg).
    if (prevTailValid) {
      for (int j = 0; j < WSOLA_OVERLAP; j++) {
        int32_t wIn  = j;
        int32_t wOut = WSOLA_OVERLAP - j;
        int32_t sum  = wOut * (int32_t)prevTail[j] + wIn * (int32_t)frame[j];
        output[outPos + j] = (int16_t)(sum / WSOLA_OVERLAP);
      }
    } else {
      for (int j = 0; j < WSOLA_OVERLAP; j++) {
        output[outPos + j] = frame[j];
      }
    }
    // Non-overlap tail: direct copy. Will be overwritten by next
    // iter's crossfade except for the very last iter, which remains.
    for (int j = WSOLA_OVERLAP; j < WSOLA_FRAME; j++) {
      output[outPos + j] = frame[j];
    }

    // Save this frame's tail for next iter's crossfade.
    for (int j = 0; j < WSOLA_OVERLAP; j++) {
      prevTail[j] = frame[WSOLA_FRAME - WSOLA_OVERLAP + j];
    }
    prevTailValid = true;

    outPos += WSOLA_SYNHOP;
    analysisStart += analysisHop;
  }

  // If we bailed early (ran out of input), fill the rest by extending
  // the last produced tail.
  int16_t hold = prevTailValid ? prevTail[WSOLA_OVERLAP - 1] : 0;
  while (outPos < outputCount) output[outPos++] = hold;

  // Advance the read cursor by the amount we logically consumed. The
  // WSOLA search may have peeked slightly past that, but those samples
  // remain in the ring and the next callback's emuWriteIdx-based lag
  // calc corrects for any small drift.
  emuReadIdx = readIdx + (uint64_t)desired;
}

// --------------------------------------------------------------------

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  int outputCount = len / SAMPLEBYTES;
  int16_t *out = (int16_t *)stream;

  pthread_mutex_lock(&togmutex);

  if (g_biosInterrupt) {
    audioRunning = 0;
    memset(stream, 0, len);
    pthread_mutex_unlock(&togmutex);
    return;
  }

  // Initial fill gate: wait for one SDLSIZE's worth of emu samples
  // before we start playing, so WSOLA has content to work with.
  uint64_t available = emuWriteIdx - emuReadIdx;
  if (audioRunning == 0) {
    if (available < (uint64_t)SDLSIZE) {
      memset(stream, 0, len);
      pthread_mutex_unlock(&togmutex);
      return;
    }
    audioRunning = 1;
  }

  wsolaProcess(out, outputCount);

#ifdef DEBUG_OUT_WAV
  if (outputFD == -1) {
    outputFD = open("/tmp/out.wav", O_RDWR | O_CREAT | O_TRUNC, 0600);
    unsigned char buf[44] = {
      'R','I','F','F', 0xff,0xff,0xff,0, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
      0x44,0xAC,0,0, 0x44,0xAC,0,0, 2,0, 16,0,
      'd','a','t','a', 0xff,0xff,0xff,0 };
    write(outputFD, buf, sizeof(buf));
  }
  write(outputFD, (void *)out, outputCount * SAMPLEBYTES);
#endif

  pthread_mutex_unlock(&togmutex);
}

// --------------------------------------------------------------------

SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;
  pthread_mutex_init(&togmutex, NULL);
}

SDLSpeaker::~SDLSpeaker() {}

void SDLSpeaker::begin()
{
  SDL_AudioSpec audioDevice, audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq     = AUDIO_SAMPLE_RATE_EXACT;
  audioDevice.format   = AUDIO_S16;
  audioDevice.channels = 1;
  audioDevice.samples  = SDLSIZE;
  audioDevice.callback = audioCallback;
  audioDevice.userdata = NULL;

  memset((void *)&emuBuf[0], 0, sizeof(emuBuf));
  emuWriteIdx = 0;
  emuReadIdx  = 0;
  lastFilledTime = 0;
  prevTailValid = false;
  silenceCallbacks = 0;
  audioRunning = 0;

  SDL_OpenAudio(&audioDevice, &audioActual);
  printf("Actual: freq %d channels %d samples %d\n",
         audioActual.freq, audioActual.channels, audioActual.samples);
  SDL_PauseAudio(0);
}

void SDLSpeaker::toggle(int64_t c)
{
  pthread_mutex_lock(&togmutex);

  // Map cycle count → emu sample index using the REAL Apple II clock
  // (1.023 MHz), not g_speed. That way samples are laid down in
  // emuBuf at 44.1 kHz *emulated* time regardless of how fast the
  // host is running. The wall-clock rate adjustment is WSOLA's job.
  int64_t expectedSampleNumber = c * (int64_t)AUDIO_SAMPLE_RATE_EXACT / 1023000;

  if (lastFilledTime == 0) {
    lastFilledTime = expectedSampleNumber;
  }

  int64_t delta = expectedSampleNumber - lastFilledTime;
  if (delta <= 0) {
    // Multiple toggles within the same sample cell — skip. Matches
    // the original code's behavior: a toggle too fast for the 44.1k
    // emulated sample rate effectively folds into the last one.
    pthread_mutex_unlock(&togmutex);
    return;
  }

  // Flip first, then fill the range with the new level. (Preserves
  // the original code's convention; off by a half-cycle from a strict
  // "samples before this toggle are the PRE-toggle level" model, but
  // for square-wave audio the pitch comes out the same.)
  toggleState = !toggleState;
  int16_t level = toggleState ? HIGHVAL : LOWVAL;

  // If the emu has been silent for a long time, `delta` can be huge
  // (seconds). Cap it to what the ring can hold; dropping old samples
  // is fine because if we were silent that long, there's no content
  // worth preserving.
  if (delta > EMU_BUF_SAMPLES) {
    emuReadIdx = emuWriteIdx + delta - EMU_BUF_SAMPLES;
    delta = EMU_BUF_SAMPLES;
    prevTailValid = false;
  } else {
    uint64_t available = emuWriteIdx - emuReadIdx;
    if (available + delta > EMU_BUF_SAMPLES) {
      // Drop oldest unread samples to make room for the new write.
      uint64_t drop = (available + delta) - EMU_BUF_SAMPLES + 1;
      emuReadIdx += drop;
      prevTailValid = false;
    }
  }

  for (int64_t i = 0; i < delta; i++) {
    emuBuf[(emuWriteIdx + i) & EMU_BUF_MASK] = level;
  }
  emuWriteIdx += delta;
  lastFilledTime = expectedSampleNumber;

  pthread_mutex_unlock(&togmutex);
}

void SDLSpeaker::maintainSpeaker(int64_t c, uint64_t microseconds) {}
void SDLSpeaker::beginMixing() {}
void SDLSpeaker::mixOutput(uint8_t v) {}
