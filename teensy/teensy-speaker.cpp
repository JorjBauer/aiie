#include <Arduino.h>
#include "teensy-speaker.h"
#include "teensy-println.h"
#include <Audio.h>
#include <SPI.h>

TeensyAudio audioDriver;
//AudioMixer4             mixer2;                 //xy=280,253
AudioMixer4             mixer1;                 //xy=280,175
AudioOutputI2S          i2s;                    //xy=452,189

AudioConnection         patchCord1(audioDriver, 0, mixer1, 0);
//AudioConnection         patchCord2(audioDriver, 0, mixer2, 0);
//AudioConnection         patchCord3(mixer2, 0, i2s, 1);
AudioConnection         patchCord4(mixer1, 0, i2s, 0);
//const float     t_ampx  = 0.8;
//const int       t_lox   = 10;
//const int       t_hix   = 22000;
//const float     t_timex = 10;           // Length of time for the sweep in seconds

#include "globals.h"

//#define BUFSIZE 4096
//EXTMEM uint32_t toggleBuffer[BUFSIZE]; // cycle counts at which state toggles
//uint16_t headptr, tailptr;

// Ring buffer that we fill with 44.1kHz data
#define RINGBUFSIZE 4096
EXTMEM short sampleRingBuffer[RINGBUFSIZE];
volatile uint16_t sampleHeadPtr = 0;
volatile uint16_t sampleTailPtr = 0;
volatile uint32_t lastFilledTime = 0;

volatile uint32_t lastSampleNum = 0;

bool toggleState = false;

// How many cycles do we run the audio behind? Needs to be more than our bulk
// cycle count.
//#define CYCLEDELAY 100

TeensySpeaker::TeensySpeaker(uint8_t sda, uint8_t scl) : PhysicalSpeaker()
{
  toggleState = false;
  mixerValue = numMixed = 0;
  AudioMemory(8);
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::begin()
{
  mixer1.gain(0, 0.5f); // left channel
  
  lastFilledTime = g_cpu->cycles;
  sampleHeadPtr = sampleTailPtr = 0;
  toggleState = false;
  //  memset(toggleBuffer, 0, sizeof(toggleBuffer));
  //  headptr = tailptr = 0;
  lastSampleNum = 0;
}

void TeensySpeaker::toggle(uint32_t c)
{
  // Figure out when the last time was that we put data in the audio buffer;
  // then figure out how many audio buffer cycles we have to fill from that
  // CPU time to this one.
#if 1
  __disable_irq();

  // We expect to have filled to this cycle number...
  uint32_t expectedCycleNumber = (float)c  * (float)AUDIO_SAMPLE_RATE_EXACT / (float)g_speed;

  // and we have filled to cycle number lastFilledTime. So how many do we need?
  uint32_t audioBufferSamples = expectedCycleNumber - lastFilledTime;

  if (audioBufferSamples > RINGBUFSIZE)
    audioBufferSamples = RINGBUFSIZE;
  for (int i=0; i<audioBufferSamples; i++) {
    sampleRingBuffer[sampleTailPtr++] = toggleState ? (32767/2) : (-32767/2); // FIXME: appropriate value?
    sampleTailPtr %= RINGBUFSIZE;
  }
  toggleState = !toggleState;
  lastFilledTime = expectedCycleNumber;
  __enable_irq();
#endif
}

void TeensySpeaker::maintainSpeaker(uint32_t c, uint64_t microseconds)
{
  begin(); // flush! Hack. FIXME.
}

void TeensySpeaker::maintainSpeaker()
{
  // This is called @ 44100Hz, which is the sample rate for the
  // Teensy4 (#define AUDIO_SAMPLE_RATE_EXACT 44100.0f). We fill a FIFO 
  // that is then drained by update(). In theory, as long as we don't fall
  // 128 cycles behind, it should be okay, I think (b/c AUDIO_BLOCK_SAMPLES
  // is 128 on the Teensy 4).
#if 0
  uint32_t curTime = g_cpu->cycles - CYCLEDELAY;
  while (headptr != tailptr) {
    if (curTime >= toggleBuffer[headptr]) {
      toggleState = !toggleState;
      headptr++; headptr %= BUFSIZE;
    } else {
      // The time to deal with this one has not come yet, so we're done for now
      break;
    }
  }
#endif
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
  short *bp;

  // Grab a block and we'll fill it up. It needs AUDIO_BLOCK_SAMPLES short values
  // (which is 128 on the Teensy 4).
  block = allocate();
  if (block) {
    bp = block->data;
#if 1
    uint32_t underflow = 0;
    for (int i=0; i<AUDIO_BLOCK_SAMPLES; i++) {
      static short lastValue = 0;

      if (sampleHeadPtr == sampleTailPtr) {
	//	bp[i] = lastValue; // underflow: just repeat whatever old data we have
	// FIXME: trend toward zero, maybe?
	bp[i] = lastValue;
	underflow++;
      } else {
	lastValue = sampleRingBuffer[sampleHeadPtr++];
	bp[i] = lastValue;
	sampleHeadPtr %= RINGBUFSIZE;
      }

    }
#else
    // Fill in the AUDIO_BLOCK_SAMPLES samples of data, pull them from the FIFO
    memset(bp, 0, AUDIO_BLOCK_SAMPLES * sizeof(short));
#endif
    if (underflow) {
      println("U ", underflow);
    }
    transmit(block, 0);
    release(block);
  }

#if 0
  if (sampleHeadPtr == sampleTailPtr) {
    // The FIFO is empty, so reset...
    if (g_cpu) {
      lastFilledTime = g_cpu->cycles;
    } else {
      lastFilledTime = 0;
    }
    // FIXME:
    //    lastSampleNum = 0;
  }
#endif
}
