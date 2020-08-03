#include <Arduino.h>
#include "teensy-speaker.h"
#include "teensy-println.h"
#include <Audio.h>

TeensyAudio audioDriver;
AudioMixer4             mixer2;                 //xy=280,253
AudioMixer4             mixer1;                 //xy=280,175
//AudioOutputI2S          i2s;                    //xy=452,189
AudioOutputMQS          i2s;                    //xy=452,189

AudioConnection         patchCord1(audioDriver, 0, mixer1, 0);
AudioConnection         patchCord2(audioDriver, 0, mixer2, 0);
AudioConnection         patchCord3(mixer2, 0, i2s, 1);
AudioConnection         patchCord4(mixer1, 0, i2s, 0);

#include "globals.h"

#define HIGHVAL (0x4FFF)
#define LOWVAL (-0x4FFF)

// Ring buffer that we fill with 44.1kHz data
#define BUFSIZE (4096)
#define CACHEMULTIPLIER 2
static volatile uint32_t bufIdx; // 0 .. BUFSIZE-1
static volatile uint64_t skippedSamples; // Who knows where this will
					 // wind up
static volatile uint8_t audioRunning = 0; // FIXME: needs constants abstracted
static volatile int64_t lastFilledTime = 0;

// how full do we want the audio buffer before we start it playing?
#define AUDIO_WATERLEVEL 4096

#define SAMPLEBYTES sizeof(short)
EXTMEM short soundBuf[BUFSIZE*CACHEMULTIPLIER];

static bool toggleState = false;

TeensySpeaker::TeensySpeaker(uint8_t sda, uint8_t scl) : PhysicalSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;

  AudioMemory(8);
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::begin()
{
  mixer1.gain(0, 0.1f); // left channel (off for now)
  mixer1.gain(1, 0.1f); // right channel (full volume for now)

  memset(soundBuf, 0, sizeof(soundBuf));
  
  toggleState = false;
  bufIdx = 0;
  skippedSamples = 0;
  audioRunning = 0;
}

void TeensySpeaker::toggle(int64_t c)
{
  // Figure out when the last time was that we put data in the audio buffer;
  // then figure out how many audio buffer cycles we have to fill from that
  // CPU time to this one.
  __disable_irq();

  // We expect to have filled to this cycle number...
  int64_t expectedCycleNumber = (float)c  * (float)AUDIO_SAMPLE_RATE_EXACT / (float)g_speed;
  // Dynamically initialize the lastFilledTime based on the start time of the
  // audio channel.
  if (lastFilledTime == 0)
    lastFilledTime = expectedCycleNumber;

  // and we have filled to cycle number lastFilledTime. So how many do
  // we need?  This subtracts skippedSamples because those were filled
  // automatically by the audioCallback when we had no data.
  int64_t audioBufferSamples = expectedCycleNumber - lastFilledTime - skippedSamples;
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
    // If the toggle wouldn't result in at least 1 buffer sample
    // change, then we'll blatantly skip it here. If this turns out to
    // be a problem, we could try setting audioBufferSamples++ and
    // then twiddle the lastFilledTime so it looks like it's more in
    // the future, but I suspect that would mean missing more future
    // events, just like we would have missed this one.
    //
    // But I think this is probably okay - because something that's
    // toggling the speaker fast enough that our 44k audio can't keep
    // up with the individual changes is likely to toggle again in a
    // moment without significant distortion?
    __enable_irq();
    return;
  }

  if (newIdx >= sizeof(soundBuf)/SAMPLEBYTES) {
    // Buffer overrun error. Shouldn't happen?
    println("OVERRUN");
    newIdx = (sizeof(soundBuf)/SAMPLEBYTES) - 1;
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
  __enable_irq();
}

void TeensySpeaker::maintainSpeaker(int64_t c, uint64_t microseconds)
{
  begin(); // flush! Hack. FIXME.
}

void TeensySpeaker::maintainSpeaker()
{
}

void TeensySpeaker::beginMixing()
{
  // unused
}

void TeensySpeaker::mixOutput(uint8_t v)
{
  // unused
}

void TeensyAudio::update(void)
{
  audio_block_t *block;
  short *stream;

  if (audioRunning == 0)
    audioRunning = 1;
  
  block = allocate();
  if (!block) {
    return;
  }
  
  stream = block->data;
  
  if (g_biosInterrupt) {
    // While the BIOS is running, we don't put samples in the audio queue.
    audioRunning = 0;
    memset(stream, 0, AUDIO_BLOCK_SAMPLES * SAMPLEBYTES);
    goto done;
  }

  if (audioRunning == 1 && bufIdx >= AUDIO_WATERLEVEL) {
    // We have enough samples in the buffer to fill it, so we're fully
    // up and running.
    audioRunning = 2;
  } else if (audioRunning == 1) {
    // Still waiting for the first fill; return an empty buffer.
    memset(stream, 0, AUDIO_BLOCK_SAMPLES * SAMPLEBYTES);
    goto done;
  }

  static short lastKnownSample = 0; // saved for when the apple is quiescent
  
  if (bufIdx >= AUDIO_BLOCK_SAMPLES) {
    memcpy(stream, (void *)soundBuf, AUDIO_BLOCK_SAMPLES * SAMPLEBYTES);
    lastKnownSample = stream[AUDIO_BLOCK_SAMPLES-1];
      
    if (bufIdx > AUDIO_BLOCK_SAMPLES) {
	// move the remaining data down
      memcpy((void *)soundBuf, (void *)&soundBuf[AUDIO_BLOCK_SAMPLES], (bufIdx - AUDIO_BLOCK_SAMPLES + 1)*SAMPLEBYTES);
      bufIdx -= AUDIO_BLOCK_SAMPLES;
    }
  } else {
    if (bufIdx) {
      // partial buffer exists
      memcpy(stream, (void *)soundBuf, bufIdx * SAMPLEBYTES);
      // and it's a partial underrun. Track the number of samples we skipped
      // so we can keep the audio buffer in sync.
      skippedSamples += AUDIO_BLOCK_SAMPLES - bufIdx;
      for (int32_t i=0; i<AUDIO_BLOCK_SAMPLES-bufIdx; i++) {
	stream[i+bufIdx] = lastKnownSample;
      }
      bufIdx = 0;
    } else {
      // No big deal - buffer underrun might just mean nothing is
      // trying to play audio right now.
      skippedSamples += AUDIO_BLOCK_SAMPLES;
      for (int32_t i=0; i<AUDIO_BLOCK_SAMPLES; i++) {
	stream[i] = 0;
      }
      //      memset(stream, 0, AUDIO_BLOCK_SAMPLES * SAMPLEBYTES);
    }
  }
 done:
  transmit(block, 0);
  release(block);
}
