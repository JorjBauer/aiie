// mb-replay.cpp
//
// Standalone Mockingboard capture-to-WAV replay tool.
//
// Feeds a dataset captured by the emulator (set AIIE_MB_CAPTURE=<path> before
// running an SDL build with a Mockingboard title such as Ultima V) back into a
// real Mockingboard object and renders the audio it produces to a .wav file.
// Because the same object regenerates the sound, this doubles as a golden-file
// regression test: capture once, and any future change that alters the output
// shows up as a diff in the WAV.
//
// The capture format is a text CSV, one CPU->card write per line:
//     cycle,addr,val            (cycle = 1.023 MHz CPU cycle, absolute)
// with an optional header line:
//     # aiie-mockingboard-capture v1 clockHz=1023000
//
// Build:  c++ -O2 -I. -Iapple -o mb-replay mb-replay.cpp apple/mockingboard.cpp
// Run:    ./mb-replay capture.csv [out.wav] [tail_seconds]
//
// Output: 16-bit signed mono, 44.1 kHz (same as the emulator's audio path).

#include "mockingboard.h"
#include "cpu.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// --- link stubs -------------------------------------------------------------
// The Mockingboard object references these (in update()/writeSlotRom(), which we
// never call here). The replay drives the card through applyWrite() +
// mixSample(), neither of which touches the CPU, so a null pointer and no-op
// IRQ handlers are safe and let us avoid linking the whole 6502 core.
Cpu *g_cpu = NULL;
void Cpu::assertIrq()   {}
void Cpu::deassertIrq() {}

#define SR                44100
#define AY_CLOCK_DEFAULT  1023000.0
#define MAX_MINUTES       15          // runaway guard

// --- WAV writer (same little-endian PCM writer as ay-wavtest.c) -------------
static void put_u32(FILE *f, uint32_t v) {
  fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
  fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}
static void put_u16(FILE *f, uint16_t v) {
  fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
}
static void write_wav(const char *name, const int16_t *samp, uint32_t n) {
  FILE *f = fopen(name, "wb");
  if (!f) { perror(name); return; }
  uint32_t dataBytes = n * 2;
  fwrite("RIFF", 1, 4, f); put_u32(f, 36 + dataBytes); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); put_u32(f, 16);
  put_u16(f, 1);            // PCM
  put_u16(f, 1);            // mono
  put_u32(f, SR);
  put_u32(f, SR * 2);       // byte rate
  put_u16(f, 2);            // block align
  put_u16(f, 16);           // bits
  fwrite("data", 1, 4, f); put_u32(f, dataBytes);
  fwrite(samp, 2, n, f);
  fclose(f);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s capture.csv [out.wav] [tail_seconds]\n", argv[0]);
    return 1;
  }
  const char *inPath  = argv[1];
  const char *outPath = (argc > 2) ? argv[2] : "mb_replay.wav";
  double tailSec = (argc > 3) ? atof(argv[3]) : 2.0;

  FILE *f = fopen(inPath, "r");
  if (!f) { perror(inPath); return 1; }

  Mockingboard mb;
  mb.Reset();

  double clockHz = AY_CLOCK_DEFAULT;
  double cyclesPerSample = clockHz / SR;
  const long MAXSAMP = (long)((double)MAX_MINUTES * 60.0 * SR);

  std::vector<int16_t> out;
  long emitted = 0;
  long long t0 = -1;
  long nEvents = 0;
  char line[256];

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
      char *p = strstr(line, "clockHz=");
      if (p) { clockHz = atof(p + 8); if (clockHz > 0) cyclesPerSample = clockHz / SR; }
      continue;
    }
    long long cyc; unsigned addr, val;
    if (sscanf(line, "%lld,%u,%u", &cyc, &addr, &val) != 3) continue;

    if (t0 < 0) t0 = cyc;                     // first event is the time origin
    long target = (long)((double)(cyc - t0) / cyclesPerSample + 0.5);
    if (target > MAXSAMP) target = MAXSAMP;
    while (emitted < target) { out.push_back(mb.mixSample()); emitted++; }

    mb.applyWrite((uint8_t)addr, (uint8_t)val);
    nEvents++;
  }
  fclose(f);

  long tailN = (long)(tailSec * SR);
  for (long i = 0; i < tailN && emitted < MAXSAMP; i++) { out.push_back(mb.mixSample()); emitted++; }

  write_wav(outPath, out.data(), (uint32_t)out.size());
  printf("replayed %ld events -> %s (%zu samples, %.2f s, %.0f Hz clock)\n",
         nEvents, outPath, out.size(), (double)out.size() / SR, clockHz);
  return 0;
}
