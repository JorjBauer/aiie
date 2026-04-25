#include <Arduino.h>
#include "teensy-speaker.h"
#include "teensy-println.h"
#include <Audio.h>
#include <string.h>

#include "globals.h"
#include "wsola-speaker.h"
#include "applevm.h"

TeensyAudio audioDriver;
AudioMixer4             mixer2;
AudioMixer4             mixer1;
AudioOutputMQS          i2s;

AudioConnection         patchCord1(audioDriver, 0, mixer1, 0);
AudioConnection         patchCord2(audioDriver, 0, mixer2, 0);
AudioConnection         patchCord3(mixer2, 0, i2s, 1);
AudioConnection         patchCord4(mixer1, 0, i2s, 0);

#define HIGHVAL ((int16_t)0x4FFF)
#define LOWVAL  ((int16_t)-0x4FFF)

static volatile uint8_t audioRunning = 0;

TeensySpeaker::TeensySpeaker(uint8_t sda, uint8_t scl) : PhysicalSpeaker()
{
  toggleState = false;
  mixerValue = 0x80;
  AudioMemory(8);
}

TeensySpeaker::~TeensySpeaker() {}

void TeensySpeaker::begin()
{
  float curVolume = (float)g_volume / 15.0f;
  mixer1.gain(0, curVolume);
  mixer1.gain(1, curVolume);
  mixer2.gain(0, curVolume);
  mixer2.gain(1, curVolume);

  __disable_irq();
  wsola_reset();
  toggleState = false;
  audioRunning = 0;
  __enable_irq();
}

void TeensySpeaker::reset()
{
  __disable_irq();
  wsola_reset();
  __enable_irq();
}

void TeensySpeaker::toggle(int64_t c)
{
  __disable_irq();
  wsola_toggle(c, HIGHVAL, LOWVAL);
  __enable_irq();
}

void TeensySpeaker::maintainSpeaker(int64_t c, uint64_t microseconds)
{
  __disable_irq();
  wsola_flush(c);
  __enable_irq();
}

void TeensySpeaker::maintainSpeaker() {}

void TeensySpeaker::beginMixing() {}
void TeensySpeaker::mixOutput(uint8_t v) {}

// Audio library pulls one block (AUDIO_BLOCK_SAMPLES samples, typically
// 128 on Teensy 4.x) at a time. WSOLA's staging buffer decouples that
// block size from the internal 256-sample analysis frame.
void TeensyAudio::update(void)
{
  audio_block_t *block = allocate();
  if (!block) return;

  if (audioRunning == 0) audioRunning = 1;

  if (g_biosInterrupt) {
    audioRunning = 0;
    memset(block->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    transmit(block, 0);
    release(block);
    return;
  }

  __disable_irq();

  // Speaker: wait for priming before producing, otherwise zero-fill.
  if (audioRunning >= 2) {
    wsola_produce(block->data, AUDIO_BLOCK_SAMPLES);
  } else {
    memset(block->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    if (wsola_has_primed_fill(AUDIO_BLOCK_SAMPLES * 16))
      audioRunning = 2;
  }

  // Mockingboard: always render regardless of speaker priming state.
  Mockingboard *mb = g_vm ? ((AppleVM *)g_vm)->mockingboard : NULL;
  if (mb) {
    int16_t mbBuf[AUDIO_BLOCK_SAMPLES];
    mb->renderToBuffer(mbBuf, AUDIO_BLOCK_SAMPLES);
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t mixed = (int32_t)block->data[i] + (int32_t)mbBuf[i];
      if (mixed > 0x7FFF) mixed = 0x7FFF;
      if (mixed < -0x7FFF) mixed = -0x7FFF;
      block->data[i] = (int16_t)mixed;
    }
  }

  __enable_irq();

  transmit(block, 0);
  release(block);
}
