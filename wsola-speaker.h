#ifndef __WSOLA_SPEAKER_H
#define __WSOLA_SPEAKER_H

#include <stdint.h>

// Shared pitch-preserving time-scale resampler for the Apple II
// speaker. Decouples the emulated-time sample stream (always at
// 1.023 MHz / 44.1 kHz emu) from the wall-clock audio output rate
// (always 44.1 kHz wall). When the host CPU runs the emulator
// faster than real time, WSOLA compresses the audio without
// chipmunking its pitch; at 1× speed it's near pass-through.
//
// The algorithm itself has no threading story — the caller is
// responsible for wrapping these functions in whichever critical-
// section primitive its platform uses (pthread_mutex on SDL,
// __disable_irq on Teensy).
//
// Audio format: signed 16-bit mono @ 44.1 kHz.

// Clear all internal state. Call before starting audio output.
void wsola_reset();

// Register a speaker toggle at CPU cycle `cycles`. Takes the
// volume-scaled HIGH and LOW levels and manages the toggle-state
// flip INTERNALLY — only flipping when a sample actually gets
// written. Must be so: flipping on every call (including sub-
// sample toggles that get dropped by the cycle-to-sample
// truncation) would accumulate wrong parity and destroy the output
// for PWM-based polyphonic music.
void wsola_toggle(int64_t cycles, int16_t highLevel, int16_t lowLevel);

// Fill emuBuf with the current speaker level up to the given CPU
// cycle. Call periodically (e.g., from cpuMaintenance or the audio
// callback) so the buffer stays populated between toggles.
void wsola_flush(int64_t cycles);

// Drain `count` wall-clock samples out of the pipeline into
// `output[]`. Never blocks. If the emu hasn't produced enough
// samples yet (underrun), holds the last level (silence) to pad.
void wsola_produce(int16_t *output, int count);

// Gate for initial buffer fill. Returns true once we've accumulated
// at least `minSamples` worth of emu-rate audio — useful to let the
// platform speaker write silence during startup rather than running
// WSOLA on an almost-empty ring.
bool wsola_has_primed_fill(int minSamples);

// Diagnostic: copy up to `count` recently-written emu-rate samples
// (from emuReadIdx forwards) into `out`. Returns the number actually
// written. Used by the platform speaker to spy on what toggle() is
// putting into the pipeline.
int wsola_peek_emubuf(int16_t *out, int count);

// Diagnostic: current count of samples buffered (emuWriteIdx - emuReadIdx).
int64_t wsola_buffered();

#endif
