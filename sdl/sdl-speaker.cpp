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

  if (g_biosInterrupt) {
    audioRunning = 0;
    memset(stream, 0, len);
    pthread_mutex_unlock(&togmutex);
    return;
  }

  if (audioRunning == 0) {
    if (!wsola_has_primed_fill(SDLSIZE)) {
      memset(stream, 0, len);
      pthread_mutex_unlock(&togmutex);
      return;
    }
    audioRunning = 1;
  }

  wsola_produce(out, outputCount);

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

  wsola_reset();
  audioRunning = 0;

  SDL_OpenAudio(&audioDevice, &audioActual);
  printf("Actual: freq %d channels %d samples %d\n",
         audioActual.freq, audioActual.channels, audioActual.samples);
  SDL_PauseAudio(0);
}

void SDLSpeaker::toggle(int64_t c)
{
  // Let WSOLA manage the flip state — it must only flip on writes
  // that actually produce a sample, or polyphonic PWM music comes
  // out with the wrong parity.
  pthread_mutex_lock(&togmutex);
  wsola_toggle(c, HIGHVAL, LOWVAL);
  pthread_mutex_unlock(&togmutex);
}

void SDLSpeaker::maintainSpeaker(int64_t c, uint64_t microseconds) {}
void SDLSpeaker::beginMixing() {}
void SDLSpeaker::mixOutput(uint8_t v) {}
