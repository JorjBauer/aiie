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

#include "globals.h"
#include "wsola-speaker.h"
#include "applevm.h"

#define HIGHVAL ((int16_t)((0x4FFF) >> (15-g_volume)))
#define LOWVAL  ((int16_t)(-((0x4FFF) >> (15-g_volume))))

#define SDLSIZE (2048)
#define AUDIO_SAMPLE_RATE_EXACT 44100
#define SAMPLEBYTES sizeof(int16_t)

static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;
volatile uint8_t audioRunning = 0;

// Debug: dump wall-clock output to /tmp/out.wav. Compare with the
// pre-WSOLA reference capture to see what the WSOLA pipeline is
// doing to the signal.
//#define DEBUG_OUT_WAV
#ifdef DEBUG_OUT_WAV
static int outputFD = -1;
#endif

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  int outputCount = len / SAMPLEBYTES;
  int16_t *out = (int16_t *)stream;

  pthread_mutex_lock(&togmutex);

  if (g_biosInterrupt || g_speed >= AUDIO_MUTE_SPEED) {
    audioRunning = 0;
    memset(stream, 0, len);
    pthread_mutex_unlock(&togmutex);
    return;
  }

  // Speaker: wait for priming before producing, otherwise zero-fill.
  if (audioRunning) {
    wsola_produce(out, outputCount);
  } else {
    memset(stream, 0, len);
    // Prime to ~2x the callback size before starting, matching
    // TARGET_LAG in wsola-speaker.cpp. Starting at just SDLSIZE leaves
    // zero headroom: the first callback's burst-drain bottoms the buffer
    // out and any clock drift then underruns periodically (audible
    // click at a regular interval). The buffer controller only ever
    // drains excess, never refills, so the starting cushion is what we
    // live on.
    if (wsola_has_primed_fill(2 * SDLSIZE))
      audioRunning = 1;
  }

  // Mockingboard: always render regardless of speaker priming state.
  Mockingboard *mb = ((AppleVM *)g_vm)->mockingboard;
  if (mb) {
    int16_t mbBuf[SDLSIZE];
    mb->renderToBuffer(mbBuf, outputCount);
    for (int i = 0; i < outputCount; i++) {
      int32_t mixed = (int32_t)out[i] + (int32_t)mbBuf[i];
      if (mixed > 0x7FFF) mixed = 0x7FFF;
      if (mixed < -0x7FFF) mixed = -0x7FFF;
      out[i] = (int16_t)mixed;
    }
  }

  // Keep-alive dither. The producer-frozen debugger test proved the periodic
  // click is generated below us: when the buffer is drained we emit a constant
  // value (DC, or zeros before priming), and the output device (built-in amp,
  // HDMI, USB/BT DAC) mutes / re-syncs on bit-identical silence and clicks on
  // each transition. A few-LSB broadband dither keeps the stream "alive" so the
  // device never sees silence; at ~-78 dBFS it is inaudible under real audio.
  {
    static uint32_t ditherState = 0x1234567;
    for (int i = 0; i < outputCount; i++) {
      // xorshift32 -> small signed dither in roughly [-4, +4]
      ditherState ^= ditherState << 13;
      ditherState ^= ditherState >> 17;
      ditherState ^= ditherState << 5;
      int d = (int)(ditherState & 0x7) - 3;   // -3..+4
      int32_t v = (int32_t)out[i] + d;
      if (v > 0x7FFF) v = 0x7FFF;
      if (v < -0x7FFF) v = -0x7FFF;
      out[i] = (int16_t)v;
    }
  }

#ifdef DEBUG_OUT_WAV
  if (outputFD == -1) {
    outputFD = open("/tmp/out.wav", O_RDWR | O_CREAT | O_TRUNC, 0600);
    // Signed 16-bit mono @ 44100 Hz.
    unsigned char buf[44] = { 'R','I','F','F',
                              0xff,0xff,0xff,0x7f,
                              'W','A','V','E',
                              'f','m','t',' ',
                              16,0,0,0,
                              1,0,
                              1,0,
                              0x44,0xAC,0,0,
                              0x88,0x58,1,0,
                              2,0,
                              16,0,
                              'd','a','t','a',
                              0xff,0xff,0xff,0x7f };
    write(outputFD, buf, sizeof(buf));
  }
  write(outputFD, (void *)out, outputCount * SAMPLEBYTES);
#endif

  pthread_mutex_unlock(&togmutex);
}

SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;
  pthread_mutex_init(&togmutex, NULL);
}

SDLSpeaker::~SDLSpeaker() {}

void SDLSpeaker::reset()
{
  pthread_mutex_lock(&togmutex);
  wsola_reset();
  pthread_mutex_unlock(&togmutex);
}

void SDLSpeaker::begin()
{
  wsola_reset();
  audioRunning = 0;

#ifndef __EMSCRIPTEN__
  // Desktop: open SDL's audio device; its callback (audioCallback) pulls the speaker.
  //
  // The WASM/playground build must NOT open it.  Emscripten's SDL2 wires SDL_OpenAudio to its OWN
  // Web Audio node (a ScriptProcessorNode at the device's NATIVE rate) that runs audioCallback,
  // i.e. wsola_produce (the overlap-add path we deliberately bypass) plus the keep-alive dither.
  // That is a SECOND audio output: it competes with the playground's aiie_audio_pull path for the
  // same emuBuf and mixes its dither/overlap-add artifacts over the tone, which is audible as static
  // (silent on a headless test box, since it has no audio device to drive the callback).  The
  // playground drives audio entirely through aiie_audio_pull, so we leave SDL's device closed.
  SDL_AudioSpec audioDevice, audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq     = AUDIO_SAMPLE_RATE_EXACT;
  audioDevice.format   = AUDIO_S16;
  audioDevice.channels = 1;
  audioDevice.samples  = SDLSIZE;
  audioDevice.callback = audioCallback;
  audioDevice.userdata = NULL;

  SDL_OpenAudio(&audioDevice, &audioActual);
  printf("Actual: freq %d channels %d samples %d\n",
         audioActual.freq, audioActual.channels, audioActual.samples);
  SDL_PauseAudio(0);
#endif
}

void SDLSpeaker::toggle(int64_t c)
{
  // While muted for speed, don't record toggles at all; the speaker is
  // reset when the BIOS exits, so state resyncs on any speed change.
  if (g_speed >= AUDIO_MUTE_SPEED)
    return;

  pthread_mutex_lock(&togmutex);
  wsola_toggle(c, HIGHVAL, LOWVAL);
  pthread_mutex_unlock(&togmutex);
}

void SDLSpeaker::maintainSpeaker(int64_t c, uint64_t microseconds)
{
  if (g_speed >= AUDIO_MUTE_SPEED)
    return;

  pthread_mutex_lock(&togmutex);
  wsola_flush(c);
  pthread_mutex_unlock(&togmutex);
}
void SDLSpeaker::beginMixing() {}
void SDLSpeaker::mixOutput(uint8_t v) {}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The WASM build does not open SDL's audio device (see SDLSpeaker::begin), so audioCallback never
// runs here.  The playground drives audio itself: a JS AudioWorklet calls aiie_audio_pull() each
// block to fetch `count` mono int16 samples straight from the //e speaker.
extern "C" EMSCRIPTEN_KEEPALIVE void aiie_audio_pull(int16_t *out, int count) {
  // Drain the speaker straight from WSOLA, not through audioCallback, which (a) gates on a
  // 4096-sample "primed" fill (that swallowed breakout's short early beeps until several had queued)
  // and (b) renders the mockingboard into a fixed stack buffer (the earlier "null function" crash).
  // The playground pulls exactly wsola_buffered() samples, so there is nothing to prime or stretch;
  // wsola_produce holds the last level (silence) if asked for more than it has.  No mockingboard
  // program runs in the playground, so skipping its mix is fine.
  pthread_mutex_lock(&togmutex);
  wsola_drain(out, count);   // raw passthrough, no overlap-add -> stable pitch (playground runs at 1x)
  pthread_mutex_unlock(&togmutex);
}

// How many emu-rate samples are ready to pull right now.  The playground fills its audio ring with
// exactly this many per frame; pulling MORE makes WSOLA time-stretch the little it has, smearing
// the //e's square wave toward DC (which sounded like clicks/noise).  Pull only what's really there.
extern "C" EMSCRIPTEN_KEEPALIVE int aiie_audio_avail() {
  int64_t n = wsola_buffered();
  if (n < 0) n = 0;
  if (n > 1000000000) n = 1000000000;
  return (int)n;
}
#endif
