#include <Arduino.h>
#include <TeensyThreads.h>
#include "teensy-speaker.h"
#include "teensy-println.h"
#include <i2c_device.h>

#include "globals.h"

I2CDevice dac = I2CDevice(Master, 0x60, _BIG_ENDIAN);

Threads::Mutex togmutex;
#define BUFSIZE 4096
EXTMEM uint32_t toggleBuffer[BUFSIZE]; // cycle counts at which state toggles
uint16_t headptr, tailptr;
uint32_t lastCycleCount, toggleAtCycle;

// How many cycles do we run the audio behind? Needs to be more than our bulk
// cycle count.
#define CYCLEDELAY 100

TeensySpeaker::TeensySpeaker(uint8_t sda, uint8_t scl) : PhysicalSpeaker()
{
  toggleState = false;
  mixerValue = numMixed = 0;
  Master.begin(1000000); // 100000 or 400000 or 1000000
}

TeensySpeaker::~TeensySpeaker()
{
}

void TeensySpeaker::begin()
{
  lastCycleCount = g_cpu->cycles;
  toggleState = false;
  memset(toggleBuffer, 0, sizeof(toggleBuffer));
  headptr = tailptr = 0;
}

void TeensySpeaker::toggle(uint32_t c)
{
  Threads::Scope lock(togmutex);
  // Queue the speaker toggle time; maintainSpeaker will pick it up
  toggleBuffer[tailptr++] = c; tailptr %= BUFSIZE;
 
}

void TeensySpeaker::maintainSpeaker(uint32_t c, uint64_t microseconds)
{
  begin(); // flush! Hack. FIXME.
}

void TeensySpeaker::maintainSpeaker()
{
  // This is called @ SAMPLERATE (8k, as of this writing) and looks for
  // any transitions that have passed before sending data to the DAC.
  // The idea is that this will be called fast enough for the given number
  // of cycles that we run behind, so that the buffer can't overflow.
  //
  // at 8kHz, that means that .000125 seconds have passed since our last
  // call; where the CPU has executed about 128 instructions in the same
  // time. Therefore, it can't have toggled more than 128 times. We're
  // also trying to stay 100 cycles "behind", so it's possible that we have
  // 228 cycles difference between an event that just fired-and-queued,
  // and where we need to catch up to in this loop.

  // First, reconcile the "correct" toggleState. We pretend it's currently
  // some time in the past, based on our cycle delay. In theory (as above),
  // this shouldn't be more than about 228 cycles off (maybe slightly more,
  // since we actually run cycles in batches).
  uint32_t curTime = g_cpu->cycles - CYCLEDELAY;
  // And then find any events that should have happened, accounting for them:
  togmutex.lock();
  while (headptr != tailptr) {
    if (curTime >= toggleBuffer[headptr]) {
      toggleState = !toggleState;
      headptr++; headptr %= BUFSIZE;
    } else {
      // The time to deal with this one has not come yet, so we're done for now
      break;
    }
  }
  togmutex.unlock();

  // Now we can safely update the DAC based on the current toggleState
  //  uint16_t v = (toggleState ? 0xFFF : 0x000);
  //  dac.write((uint8_t) ((v >> 8) & 0xFF), (uint8_t) (v & 0xFF), true);

}

void TeensySpeaker::beginMixing()
{
  // unused
}

void TeensySpeaker::mixOutput(uint8_t v)
{
  // unused
}

