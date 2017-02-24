#include "dummy-speaker.h"
#include <pthread.h>


#include "timeutil.h"

// FIXME: Globals; ick.
static pthread_t speakerThreadID;
static uint8_t curSpeakerData = 0x00;

static uint64_t hitcount;
static uint64_t misscount;

static void *speaker_thread(void *dummyptr) {
  struct timespec currentTime;
  struct timespec startTime;
  struct timespec nextSampleTime;

  _init_darwin_shim();
  do_gettime(&startTime);
  do_gettime(&nextSampleTime);

  FILE *f = popen("play -q -t raw -b 8 -e unsigned-integer -r 8000 -", "w");

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
      printf("sound hit: %lld miss: %lld\n", hitcount, misscount);
    }

    fputc(curSpeakerData & 0xFF, f); fflush(f);
    nextSampleTime = startTime;
    timespec_add_ms(&startTime, sampleCount * 1000 / 8000, &nextSampleTime);
    sampleCount++;
  }
}


DummySpeaker::DummySpeaker()
{
  mixerValue = 0;
  _init_darwin_shim(); // set up the clock interface

  if (!pthread_create(&speakerThreadID, NULL, &speaker_thread, (void *)NULL)) {
    printf("speaker thread created\n");
  }
}

DummySpeaker::~DummySpeaker()
{
  pclose(f);
}

void DummySpeaker::toggleAtCycle(uint32_t c)
{
  nextTransitionAt = c;
}

void DummySpeaker::maintainSpeaker(uint32_t c)
{
  /*  if (nextTransitionAt && c >= nextTransitionAt) {
    // Override the mixer with a 1-bit "Terribad" audio sample change
    mixerValue = speakerState ? 0x00 : (0xFF<<3); // <<3 b/c of the >>=3 below
    nextTransitionAt = 0;
    }*/

  if (numMixed) {
    mixerValue /= numMixed;
  }
  speakerState = mixerValue;

  // FIXME: duplication of above? using a global? fix fix fix.
  curSpeakerData = mixerValue & 0xFF;
}

bool DummySpeaker::currentState()
{
  return speakerState;
}

void DummySpeaker::beginMixing()
{
  mixerValue = 0;
  numMixed = 0;
}

void DummySpeaker::mixOutput(uint8_t v)
{
  mixerValue += v;
  numMixed++;
}
