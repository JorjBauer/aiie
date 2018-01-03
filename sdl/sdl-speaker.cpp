#include "sdl-speaker.h"
#include <pthread.h>
#include <unistd.h>

extern "C"
{
#include <SDL.h>
#include <SDL_thread.h>
};

#include "globals.h"

#include "timeutil.h"

// FIXME: Globals; ick.
static volatile uint32_t bufIdx = 0;
static uint8_t soundBuf[44100]; // 1 second of audio
static pthread_mutex_t sndmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  FILE *f = (FILE *)unused;

  pthread_mutex_lock(&sndmutex);
  if (bufIdx >= len) {
    memcpy(stream, soundBuf, len);

    fwrite(soundBuf, 1, len, f);

    if (bufIdx > len) {
      // move the remaining data down
      memcpy(soundBuf, &soundBuf[len], bufIdx - len + 1);
      bufIdx -= len;
    }
  } else {
    // Audio underrun
    printf("Audio underrun!\n");
    memset(stream, 0, len);
  }
  pthread_mutex_unlock(&sndmutex);
}

void ResetDCFilter(); // FIXME: remove

SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0x8000;

  toggleCount = toggleReadPtr = toggleWritePtr = 0;

  pthread_mutex_init(&togmutex, NULL);
  pthread_mutex_init(&sndmutex, NULL);

  _init_darwin_shim();

  ResetDCFilter();

  lastCycleCount = 0;
  lastSampleCount = 0;

  FILE *f = fopen("out.dat", "w");

  SDL_AudioSpec audioDevice;
  SDL_AudioSpec audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq = 44100;
  audioDevice.format = AUDIO_U8;
  audioDevice.channels = 1;
  audioDevice.samples = 4096; // 4096 bytes @ 44100Hz is about 1/10th second out of sync - should be okay for this testing
  audioDevice.callback = audioCallback;
  audioDevice.userdata = (void *)f;

  SDL_OpenAudio(&audioDevice, &audioActual); // FIXME retval
  printf("Actual: freq %d channels %d samples %d\n", 
	 audioActual.freq, audioActual.channels, audioActual.samples);

  SDL_PauseAudio(0);
}

SDLSpeaker::~SDLSpeaker()
{
  pclose(f);
}

void SDLSpeaker::toggle(uint32_t c)
{
  pthread_mutex_lock(&togmutex);

  toggleTimes[toggleWritePtr] = c;
  if (toggleCount < SPEAKERQUEUESIZE-1) {
    toggleWritePtr++;
    if (toggleWritePtr >= SPEAKERQUEUESIZE)
      toggleWritePtr = 0;
    toggleCount++;
  } else {
    printf("speaker overflow @ cycle %d\n", c);
    for (int i=0; i<SPEAKERQUEUESIZE; i++) {
      printf(" %d [%d]\n", toggleTimes[(toggleReadPtr + i)%SPEAKERQUEUESIZE],
	     toggleTimes[(toggleReadPtr + i - 1)%SPEAKERQUEUESIZE] -
	     toggleTimes[(toggleReadPtr + i)%SPEAKERQUEUESIZE]
	     );
    }
    exit(1);
  }
  pthread_mutex_unlock(&togmutex);
}

// FIXME: make methods
uint16_t dcFilterState = 0;

void ResetDCFilter()
{
  dcFilterState = 32768 + 10000;
}

int16_t DCFilter(int16_t in)
{
  if (dcFilterState == 0)
    return 0;

  if (dcFilterState >= 32768) {
    dcFilterState--;
    return in;
  }

  return ( (int32_t)in * (int32_t)dcFilterState-- ) / (int32_t)32768;
}


void SDLSpeaker::maintainSpeaker(uint32_t c, uint64_t microseconds)
{
  bool didChange = false;

  pthread_mutex_lock(&togmutex);
  while (toggleCount && c >= toggleTimes[toggleReadPtr]) {
    // Override the mixer with a 1-bit "Terribad" audio sample change
    toggleState = !toggleState;
    toggleCount--;
    toggleReadPtr++;
    if (toggleReadPtr >= SPEAKERQUEUESIZE)
      toggleReadPtr = 0;
    didChange = true;
  }
  pthread_mutex_unlock(&togmutex);

  // FIXME: removed all the mixing code

  // Add samples from the last time to this time
  //  mixerValue = (toggleState ? 0x1FF : 0x00);
  mixerValue = (toggleState ? 0x8000 : ~0x8000);
  // FIXME: DC filter isn't correct yet
  //  mixerValue = DCFilter(mixerValue);

  uint64_t sampleCount = (microseconds * 44100) / 1000000;
  uint64_t numSamples = sampleCount - lastSampleCount;

  if (numSamples) {
    lastSampleCount = sampleCount;

    mixerValue >>= 12; // convert from 16 bit to 8 bit; then drop volume by 50%
    
    pthread_mutex_lock(&sndmutex);

    if (bufIdx + numSamples >= sizeof(soundBuf)) {
      printf("Sound overrun!\n");
      numSamples = sizeof(soundBuf) - bufIdx - 1;
    }
    memset(&soundBuf[bufIdx], mixerValue, numSamples);
    bufIdx += numSamples;
    pthread_mutex_unlock(&sndmutex);
  }

}

void SDLSpeaker::beginMixing()
{
}

void SDLSpeaker::mixOutput(uint8_t v)
{
}
