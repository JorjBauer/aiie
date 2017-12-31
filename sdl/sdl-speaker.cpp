#include "sdl-speaker.h"
#include <pthread.h>

extern "C"
{
#include <SDL.h>
#include <SDL_thread.h>
};

#include "timeutil.h"

#include "globals.h"


// FIXME: Globals; ick.
static pthread_t speakerThreadID;
static uint8_t curSpeakerData = 0x00;
static volatile uint16_t bufIdx = 0;
static uint8_t soundBuf[4096];
static pthread_mutex_t sndmutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;


static uint64_t hitcount;
static uint64_t misscount;

static uint64_t copycount = 0;

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  pthread_mutex_lock(&sndmutex);
  if (bufIdx >= len) {
    memcpy(stream, soundBuf, len);
    if (bufIdx > len) {
      // move the remaining data down
      memcpy(soundBuf, &soundBuf[len], bufIdx - len);
      bufIdx -= len;
      copycount += len;
    }
  } else {
    // Audio underrun
    memset(stream, 0, len);
  }
  pthread_mutex_unlock(&sndmutex);
}

static void *speaker_thread(void *dummyptr) {
  struct timespec currentTime;
  struct timespec startTime;
  struct timespec nextSampleTime;

  pthread_mutex_init(&sndmutex, NULL);

  SDL_AudioSpec audioDevice;
  SDL_AudioSpec audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq = 22050;
  audioDevice.format = AUDIO_U8;
  audioDevice.channels = 1;
  audioDevice.samples = 2048; // 2048 bytes @ 22050Hz is about 1/10th second out of sync - should be okay for this testing
  audioDevice.callback = audioCallback;
  audioDevice.userdata = NULL;

  SDL_OpenAudio(&audioDevice, &audioActual); // FIXME retval
  printf("Actual: freq %d channels %d samples %d\n", 
	 audioActual.freq, audioActual.channels, audioActual.samples);

  _init_darwin_shim();
  do_gettime(&startTime);
  do_gettime(&nextSampleTime);

  SDL_PauseAudio(0);


  uint64_t sampleCount = 0;
  while (1) {
    do_gettime(&currentTime);
    struct timespec diff = tsSubtract(nextSampleTime, currentTime);
    if (diff.tv_sec >= 0 && diff.tv_nsec >= 0) {
      nanosleep(&diff, NULL);
      hitcount++;
    } else
      misscount++;

    if ((sampleCount & 0xFFFF) == 0) {
      printf("sound hit: %lld miss: %lld copy: %lld\n", hitcount, misscount, copycount);
    }

    pthread_mutex_lock(&sndmutex);
    soundBuf[bufIdx++] = curSpeakerData & 0xFF;
    if (bufIdx >= sizeof(soundBuf)) {
      // Audio overrun; start dropping data
      bufIdx--;
    }
    pthread_mutex_unlock(&sndmutex);

    // set nextSampleTime to the absolute reference time of when the 
    // next sample should start (based on our start time).
    timespec_add_us(&startTime, (sampleCount * 1000000) / 22050  , &nextSampleTime);
    sampleCount++;
  }
}


SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0;
  _init_darwin_shim(); // set up the clock interface

  toggleCount = toggleReadPtr = toggleWritePtr = 0;

  pthread_mutex_init(&togmutex, NULL);

  if (!pthread_create(&speakerThreadID, NULL, &speaker_thread, (void *)NULL)) {
    printf("speaker thread created\n");
  }
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

void SDLSpeaker::maintainSpeaker(uint32_t c)
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

  if (didChange) {
    mixerValue = (toggleState ? 0x1FF : 0x00);

    // FIXME: g_volume
    
    curSpeakerData = (mixerValue & 0xFF) >> 4;
  }
}

void SDLSpeaker::beginMixing()
{
}

void SDLSpeaker::mixOutput(uint8_t v)
{
}
