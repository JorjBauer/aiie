// sc01-say: render SC-01 phonemes to a WAV file with aiie's SC-01 voice,
// timed the way the chip times them (each one runs until its A/R rises).
//
//   sc01-say [-c clockHz] [-i inflection] out.wav 03 38 32 03 1F ...
//
// The default clock is the Apple's 1023000Hz, which is what aiie's
// Sound/Speech I uses. The phrase in the example is "The Spy Strikes Back".
// It prints the peak and RMS levels, so the output gain can be checked.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#include "sc01.h"
#include "sc01params.h"

#define RATE 44100

static void put16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

int main(int argc, char **argv)
{
  double clock = 1023000;
  int inflection = 0;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    if (!strcmp(argv[i], "-c") && i+1 < argc) clock = atof(argv[++i]);
    else if (!strcmp(argv[i], "-i") && i+1 < argc) inflection = atoi(argv[++i]) & 3;
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
  }
  if (argc - i < 2) {
    fprintf(stderr, "usage: %s [-c clockHz] [-i inflection] out.wav phoneme...\n", argv[0]);
    return 1;
  }
  const char *out = argv[i++];

  SC01Synth sc(clock);
  std::vector<int16_t> samples;
  // A little silence first
  for (int s = 0; s < RATE / 10; s++) samples.push_back(sc.render(RATE));

  for (; i < argc; i++) {
    uint8_t p = (uint8_t)strtol(argv[i], NULL, 16) & 0x3F;
    sc.write(p, inflection);
    double clocks = 72 + 16.0 * (sc01Phones[p].dur * 4 + 1) * 36 + 2;
    int n = (int)(clocks / clock * RATE);
    for (int s = 0; s < n; s++) samples.push_back(sc.render(RATE));
  }
  // The chip keeps playing the last one, so end on STOP and let it ring out
  sc.write(0x3F, inflection);
  for (int s = 0; s < RATE / 4; s++) samples.push_back(sc.render(RATE));

  int peak = 0;
  double sq = 0;
  for (size_t s = 0; s < samples.size(); s++) {
    int v = abs(samples[s]);
    if (v > peak) peak = v;
    sq += (double)samples[s] * samples[s];
  }
  printf("%zu samples, %.2fs, peak %d, rms %.0f\n", samples.size(),
         samples.size() / (double)RATE, peak, sqrt(sq / samples.size()));

  FILE *f = fopen(out, "wb");
  if (!f) { perror(out); return 1; }
  uint32_t bytes = samples.size() * 2;
  fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f); put32(f, 16);
  put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, RATE * 2);
  put16(f, 2); put16(f, 16);
  fwrite("data", 1, 4, f); put32(f, bytes);
  for (size_t s = 0; s < samples.size(); s++) put16(f, (uint16_t)samples[s]);
  fclose(f);
  return 0;
}
