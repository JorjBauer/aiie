#include "sdl-speaker.h"
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h> // for open()

extern "C"
{
#include <SDL.h>
#include <SDL_thread.h>
};

// What values do we use for logical speaker-high and speaker-low?
#define HIGHVAL (0x1FFF)
#define LOWVAL (-(0x1FFF))

#include "globals.h"

#define SDLSIZE (2048)
// But we want to keep more than just that, so we can fill it full every time
#define CACHEMULTIPLIER 2

#define WATERLEVEL SDLSIZE

// FIXME: Globals; ick.
static volatile uint32_t bufIdx = 0;
static volatile short soundBuf[CACHEMULTIPLIER*SDLSIZE];
static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;
static volatile uint32_t skippedSamples = 0;
#define SAMPLEBYTES sizeof(short)

volatile uint8_t audioRunning = 0;
volatile uint32_t lastFilledTime = 0;


// Debugging by writing a wav file with the sound output...
//#define DEBUG_OUT_WAV
#ifdef DEBUG_OUT_WAV
int outputFD = -1;
#endif

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  if (audioRunning==0)
    audioRunning=1;
  pthread_mutex_lock(&togmutex);
  if (g_biosInterrupt) {
    // While the BIOS is running, we don't put samples in the audio
    // queue.
    audioRunning = 0;
    memset(stream, 0, SDLSIZE*SAMPLEBYTES);
    pthread_mutex_unlock(&togmutex);
    return;
  }

  if (audioRunning==1 && bufIdx >= WATERLEVEL) {
    // Fully up and running now; we got a full cache
    audioRunning = 2;
  } else if (audioRunning==1) {
    // waiting for first fill; return an empty buffer.
    memset(stream, 0, SDLSIZE*SAMPLEBYTES);
    return;
  }
  
  static short lastKnownSample = 0; // saved for when the apple is quiescent

  if (bufIdx >= SDLSIZE) {
    memcpy(stream, (void *)soundBuf, SDLSIZE*SAMPLEBYTES);
    lastKnownSample = stream[SDLSIZE-1];

    if (bufIdx > SDLSIZE) {
      // move the remaining data down
      memcpy((void *)soundBuf, (void *)&soundBuf[SDLSIZE], (bufIdx - SDLSIZE + 1)*SAMPLEBYTES);
      bufIdx -= SDLSIZE;
    }
  } else {
    if (bufIdx) {
      // partial buffer exists
      memcpy(stream, (void *)soundBuf, bufIdx*SAMPLEBYTES);
      // and it's a partial underrun. Track the number of samples we skipped
      // so we can keep the audio buffer in sync.
      skippedSamples += SDLSIZE-bufIdx;
      for (long i=0; i<SDLSIZE-bufIdx; i++) {
	stream[bufIdx+i] = lastKnownSample;
      }
      bufIdx = 0;
    } else {
      // No big deal - buffer underrun might just mean nothing
      // is trying to play audio right now.
      skippedSamples += SDLSIZE;

      memset(stream, 0, SDLSIZE*SAMPLEBYTES);
      //      memset(stream, lastKnownSample, SDLSIZE);
      // Trend toward DC voltage = 0v
      //      if (lastKnownSample < 0x7F) lastKnownSample++;
      //      if (lastKnownSample >= 0x80) lastKnownSample--;
    }
  }
#ifdef DEBUG_OUT_WAV
  if (outputFD == -1) {
    outputFD = open("/tmp/out.wav", O_RDWR | O_CREAT | O_TRUNC, 0600);
    unsigned char buf[44] = { 'R', 'I', 'F', 'F',
			      0xff,0xff,0xff,0, // size == 0 for now
			      'W', 'A', 'V', 'E',
			      'f', 'm', 't', ' ',
			      16,0,0,0, // no extensions
			      1,0, // PCM
			      1,0, // 1 channel
			      0x44, 0xAC, 0, 0, // 44100 Hz
			      0x44, 0xAC, 0, 0, // (sample rate * bits * channels)/8
			      1,0, // sample size (1 byte here b/c 1 channel @ 8bit)
			      8,0, // bits per sample
			      'd', 'a', 't', 'a',
			      0xff, 0xff, 0xff, 0, // size of data chunk
    };
    write(outputFD, buf, sizeof(buf));
  }
			    
  write(outputFD, (void *)(stream), SDLSIZE*SAMPLEBYTES);
#endif
  
  pthread_mutex_unlock(&togmutex);
}

SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;

  pthread_mutex_init(&togmutex, NULL);
}

SDLSpeaker::~SDLSpeaker()
{
}

void SDLSpeaker::begin()
{
  SDL_AudioSpec audioDevice;
  SDL_AudioSpec audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq = 44100; // count of 16-bit samples
  audioDevice.format = AUDIO_S16;
  audioDevice.channels = 1;
  audioDevice.samples = SDLSIZE; // SDLSIZE 16-bit samples @ 44100Hz: 4096 is about 1/10th second out of sync
  audioDevice.callback = audioCallback;
  audioDevice.userdata = NULL;

  memset((void *)&soundBuf[0], 0, CACHEMULTIPLIER*SDLSIZE*SAMPLEBYTES);
  bufIdx = 0;
  skippedSamples = 0;
  audioRunning = 0;

  SDL_OpenAudio(&audioDevice, &audioActual); // FIXME retval
  printf("Actual: freq %d channels %d samples %d\n", 
	 audioActual.freq, audioActual.channels, audioActual.samples);
  // FIXME: if any of those don't match the orginal we're gonna be unhappy
  SDL_PauseAudio(0);
}

void SDLSpeaker::toggle(uint32_t c)
{
  pthread_mutex_lock(&togmutex);

  uint32_t expectedCycleNumber = (float)c * (float)44100 / (float)g_speed;
  if (lastFilledTime == 0) {
    lastFilledTime = expectedCycleNumber;
  }
  // This subtracts skippedSamples because those were filled automatically
  // by the audioCallback when we had no data.
  int32_t audioBufferSamples = expectedCycleNumber - lastFilledTime - skippedSamples;
  // If audioBufferSamples < 0, then we need to keep some
  // skippedSamples for later; otherwise we can keep moving forward.
  if (audioBufferSamples < 0) {
    skippedSamples = -audioBufferSamples;
    audioBufferSamples = 0;
  } else {
    // Otherwise we consumed them and can forget about it.
    skippedSamples = 0;
  }
  
  int32_t newIdx = bufIdx + audioBufferSamples;
  
  if (audioBufferSamples == 0) {
    // If the toggle wouldn't result in at least 1 buffer sample change,
    // then we'll blatantly skip it here. If this turns out to be
    // a problem, we could try setting audioBufferSamples++ and then
    // twiddle the lastFilledTime so it looks like it's more in the
    // future, but I suspect that would mean missing more future events,
    // just like we would have missed this one.
    //
    // But I think this is probably okay - because something that's
    // toggling the speaker fast enough that our 44k audio can't keep
    // up with the individual changes is likely to toggle again in a
    // moment without significant distortion?
    pthread_mutex_unlock(&togmutex);
    return;
  }

  if (newIdx >= sizeof(soundBuf)/SAMPLEBYTES) {
    printf("ERROR: buffer overrun: size %lu idx %d\n", sizeof(soundBuf)/SAMPLEBYTES, newIdx);
    newIdx = (sizeof(soundBuf)/SAMPLEBYTES)-1;
  }
  lastFilledTime = expectedCycleNumber;

  // Flip the toggle state
  toggleState = !toggleState;

  // Fill from bufIdx .. newIdx and set bufIdx to newIdx when done.
  if (newIdx > bufIdx) {
    long count = (long)newIdx - bufIdx;
    for (long i=0; i<count; i++) {
      soundBuf[bufIdx+i] = toggleState ? HIGHVAL : LOWVAL;
    }
    bufIdx = newIdx;
  }

  pthread_mutex_unlock(&togmutex);
}

void SDLSpeaker::maintainSpeaker(uint32_t c, uint64_t microseconds)
{
}

void SDLSpeaker::beginMixing()
{
}

void SDLSpeaker::mixOutput(uint8_t v)
{
}
