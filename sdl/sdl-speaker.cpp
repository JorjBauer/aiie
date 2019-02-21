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

// FIXME: 4096 is the right value here, I'm just debugging
#define SDLSIZE (4096)

// FIXME: Globals; ick.
static volatile uint32_t bufIdx = 0;
static uint8_t soundBuf[44100];
static pthread_mutex_t togmutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec sdlEmptyTime, sdlStartTime;
extern struct timespec startTime; // defined in aiie (main)

static void audioCallback(void *unused, Uint8 *stream, int len)
{
  pthread_mutex_lock(&togmutex);
  if (g_biosInterrupt) {
    // While the BIOS is running, we don't put samples in the audio
    // queue.
    memset(stream, 0x80, len);
    pthread_mutex_unlock(&togmutex);
    return;
  }

  // calculate when the buffer will be empty again
  do_gettime(&sdlEmptyTime);
  timespec_add_us(&sdlEmptyTime, ((float)len * (float)1000000)/(float)44100, &sdlEmptyTime);
  sdlEmptyTime = tsSubtract(sdlEmptyTime, sdlStartTime);

  static uint8_t lastKnownSample = 0; // saved for when the apple is quiescent

  if (bufIdx >= len) {
    memcpy(stream, soundBuf, len);
    lastKnownSample = stream[len-1];

    if (bufIdx > len) {
      // move the remaining data down
      memcpy(soundBuf, &soundBuf[len], bufIdx - len + 1);
      bufIdx -= len;
    }
  } else {
    if (bufIdx) {
      // partial buffer exists
      memcpy(stream, soundBuf, bufIdx);

      // and it's a partial underrun
      memset(&stream[bufIdx], lastKnownSample, len-bufIdx);
      bufIdx = 0;
    } else {
      // Total audio underrun. This is normal if nothing is toggling the
      // speaker; we stay at the last known level.
      memset(stream, lastKnownSample, len);
    }
  }
  pthread_mutex_unlock(&togmutex);
}

void ResetDCFilter(); // FIXME: remove

SDLSpeaker::SDLSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;

  pthread_mutex_init(&togmutex, NULL);

  _init_darwin_shim();

  ResetDCFilter();

  lastCycleCount = 0;
  lastSampleCount = 0;
}

SDLSpeaker::~SDLSpeaker()
{
}

void SDLSpeaker::begin()
{
  do_gettime(&sdlStartTime);
  do_gettime(&sdlEmptyTime);
  sdlEmptyTime = tsSubtract(sdlEmptyTime, sdlStartTime);

  SDL_AudioSpec audioDevice;
  SDL_AudioSpec audioActual;
  SDL_memset(&audioDevice, 0, sizeof(audioDevice));
  audioDevice.freq = 44100; // count of 8-bit samples
  audioDevice.format = AUDIO_U8;
  audioDevice.channels = 1;
  audioDevice.samples = SDLSIZE; // SDLSIZE 8-bit samples @ 44100Hz: 4096 is about 1/10th second out of sync
  audioDevice.callback = audioCallback;
  audioDevice.userdata = NULL;

  memset(&soundBuf[0], 0, SDLSIZE);
  bufIdx = SDLSIZE/2;

  SDL_OpenAudio(&audioDevice, &audioActual); // FIXME retval
  printf("Actual: freq %d channels %d samples %d\n", 
	 audioActual.freq, audioActual.channels, audioActual.samples);
  SDL_PauseAudio(0);
}

void SDLSpeaker::toggle(uint32_t c)
{
  pthread_mutex_lock(&togmutex);

  /* Figuring out what to do:
   *
   * The wallclock time we started the app is in startTime.
   *
   * The wallclock time when the SDL audio buffer will be totally
   * drained is in sdlEmptyTime. When that time comes, we want to have
   * at least SDLSIZE samples in soundBuf[] - which is currently filled
   * to bufIdx samples.
   * 
   * So given the cycle number at which this toggle happened (c), we
   * know we need to fill soundBuf[bufIdx..?] with either 0 or 127
   * (adjusted for volume). The end of that area that we need to fill is 
   * based on what time cycle 'c' refers to, 
   *
   * The wallclock time of cycle (c) is calculable from 
   *   timespec_add_cycles(&startTime, c, &outputTime);
   *
   * And the point at which the SDL buffer will be drained is the same
   * as the time at which soundBuf begins. So the difference between
   * the two tells us where the end point is.
   *
   * Then we need to fill soundBuf[bufIdx .. endPoint] with that 0 or 127, 
   * and set bufIdx = endPoint.
   *
   * Bonus: if it looks like we're not filling enough buffer, then we
   * should tell the emulation layer above to run more cycles in bulk
   * to build up more speaker backlog.
   */

  // calculate the timespec that refers to the cycle where this
  // speaker toggle happened
  struct timespec blipTime;
  timespec_add_cycles(&startTime, c, &blipTime);
  timespec_add_us(&blipTime, ((float)SDLSIZE * (float)1000000)/(float)44100, &blipTime); // it's delayed one SDL buffer naturally, and there's some drift between the start of the CPU and the start of the speaker. :/
  
  // determine how long there will be between the start of the buffer
  // and that cycle time. (tsSubtract bounds at 0 and is never
  // negative.)
  struct timespec timeOffset = tsSubtract(blipTime, sdlEmptyTime);

  // Turn that in to a sample index in the soundBuf[] buffer. There are 44100 of them per second,
  // so this is straightforward
  float newIdx = (float)timeOffset.tv_sec + ((float)timeOffset.tv_nsec / (float)NANOSECONDS_PER_SECOND);
  newIdx *= 44100.0;

  if (newIdx >= sizeof(soundBuf)) {
    // Buffer overrun
    printf("ERROR: buffer overrun, dropping data\n");
    newIdx = sizeof(soundBuf)-1;
  }

  // Flip the toggle state
  toggleState = !toggleState;

  // Fill from bufIdx .. newIdx and set bufIdx to newIdx when done
  if (newIdx > bufIdx) {
    long count = (long)newIdx - bufIdx;
    memset(&soundBuf[bufIdx], toggleState ? 127 : 0, count);
    bufIdx = newIdx;
  } else {
    // Why are we backtracking? This does happen, and it's a bug.
    if (newIdx >= 1) {
      bufIdx = newIdx-1;
      long count = (long)newIdx - bufIdx;
      memset(&soundBuf[bufIdx], toggleState ? 127 : 0, count);
      bufIdx = newIdx;
    } else {
      // ... and it's zero?
    }
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
}

void SDLSpeaker::beginMixing()
{
}

void SDLSpeaker::mixOutput(uint8_t v)
{
}
