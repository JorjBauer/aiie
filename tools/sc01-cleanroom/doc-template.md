# Clean-room SC-01A phoneme parameters

This is how the values in `apple/sc01params.h` were made. They replace the
512-byte phone ROM inside the Votrax SC-01A for the Sound/Speech I card. They
were written without looking at that ROM, at any decode of it, or at any
emulator's per-phoneme table. They are new values, worked out from the data
sheet's durations, published formant measurements, and the filter circuit
as MAME models it. The result should sound like an SC-01 family voice, not
exactly like the chip.

Made 2026-11-18. To regenerate the header and every table below:

    python3 -I tools/sc01-cleanroom/params.py              # writes apple/sc01params.h
    python3 -I tools/sc01-cleanroom/params.py --report     # quantization + levels
    python3 -I tools/sc01-cleanroom/params.py --seq        # envelope traces
    python3 -I tools/sc01-cleanroom/params.py --markdown   # the per-phoneme table
    python3 -I tools/sc01-cleanroom/filters.py             # code to frequency tables
    python3 -I tools/sc01-cleanroom/gen_doc.py             # rebuilds this document

`filters.py` sweeps the filter codes. `sim.py` is a port of MAME's chip model
that runs our table, so levels and envelopes can be measured instead of
guessed. `params.py` holds the targets and the class rules, picks the codes,
checks the result, and writes the header. `gen_doc.py` rebuilds this
document from `doc-template.md`, filling in every table from the scripts, so
edit the template, not this file. They need only the Python 3 standard
library.

## What I read, and what I didn't

Read:

- `aiie-docs/sc01-rom-analysis.md`, the analyst's spec (field meanings,
  timing formula, interpolation, closure, pitch, filters). It has no values.
- MAME `src/devices/sound/votrax.cpp` and `votrax.h` (BSD-3-Clause, Olivier
  Galibert), from `reference-only/mame/`: the circuit model and the
  capacitor values. These files hold no ROM data; the ROM is a separate
  binary I never opened. Two comments in `votrax.cpp` do state facts about
  the ROM's contents, and I saw them: that CLD = 0 "does not happen in the
  sc01(a) rom", and that CLD and VD bits are stored reversed. I used the
  first only as confirmation of something the logic forces anyway (the tick
  comparator can never match 0), so every entry has CLD of 1 or more.
- The Votrax SC-01 data sheet and the 1981 SC-01 phonetic dictionary
  (below), and Klatt (1980).

Not read: anything under `reference-only/sc01/`, any `sc01.bin` or
`sc01a.bin`, any MAME ROM set, and any emulator or hobby source with a
per-phoneme SC-01 or Votrax table. I didn't search code hosts for such
tables.

## Sources

1. **Votrax SC-01 Speech Synthesizer data sheet**, Votrax Division of
   Federal Screw Works, copyright 1980, 9 pages, scanned.
   https://redcedar.com/sc01/sc01.pdf (linked from https://redcedar.com/sc01.htm).
   Used: Table 1 (code, symbol, duration in ms, example word), Table 2
   (voiced / voiced fricative / voiced stop / fricative stop / fricative /
   nasal / no sound), Table 3 and the 720 kHz note. This is the SC-01 sheet,
   not the SC-01A. The redcedar page says the A changed some phonemes'
   parameters, and I found no SC-01A sheet.
2. **Votrax, "Phonetic Speech Dictionary for the SC-01 Speech
   Synthesizer"**, copyright 1981.
   https://downloads.reactivemicro.com/Electronics/Speech/SC-01%20Dictionary.pdf
   Used: Table 1 (same durations, except AW reads 253 ms instead of 250),
   Table 2 (the diphthong chart: fate = A1 AY Y, find = AH1 EH3 Y, found =
   AH1 O2 U1, float = O1 U1, few = Y1 IU U1, fear = AY I1, and so on), and
   the word list. It also fixes the data sheet's affricate footnote: T must
   precede CH for the CH sound, D must precede J for the J sound.
3. **D. H. Klatt, "Software for a cascade/parallel formant synthesizer"**,
   J. Acoust. Soc. Am. 67(3), March 1980, pp. 971 to 995.
   https://www.fon.hum.uva.nl/david/ba_shs/2009/klatt-1980.pdf
   Used: Table II (vowel F1, F2, F3, B1, B2, B3 for one adult male, with
   onset and offglide values for diphthongized vowels), Table III (the same
   for [w y r l], fricatives, affricates, plosive bursts and the nasals [m n]),
   and section IV.B on relative source amplitudes (voiced fricatives AF 50,
   AV 47 against AV 60 for a stressed vowel; prevocalic sonorants about 10 dB
   under the vowel; [h] takes the next vowel's formants). I read the tables
   from page images, since text extraction mangled the phonetic symbols.
4. **MAME `votrax.cpp`** (BSD-3-Clause, Olivier Galibert), as above, for the
   circuit and its capacitor values, and the phone name table
   `s_phone_table`.

I did not use Peterson and Barney (1952) or Hillenbrand et al. (1995):
Klatt's tables covered every sound, and I would only have been quoting
those from memory.

## The data sheet chart

Codes, symbols, example words and durations come from the data sheet's
Table 1. One disagreement: the data sheet (and the dictionary) put **A2 at
$05 (71 ms)** and **A1 at $06 (103 ms)**; MAME's name table has them the
other way round. Both are the vowel of "made", so I gave them the same
formants and used the data sheet's duration for each code. The header
comments follow the data sheet.

## Duration: DUR

The spec gives strobe to A/R high as `72 + 16 * (DUR * 4 + 1) * 36 + 2`
master clocks, which at 720 kHz is `(74 + 576 * (4 * DUR + 1)) / 720` ms, or
about 3.2 * DUR + 0.9 ms. Inverting and rounding gives every data sheet
duration to within 1.5 ms (DUR runs from 14 for 47 ms to 78 for 250 ms; the
column "ms (got)" below is the exact result). A tick is
`(4 * DUR + 1) / 20` ms, one sixteenth of the phoneme.

At the 1.0227 MHz clock MAME uses for the Mockingboard, every duration
shrinks and every frequency below rises by 1.42, together. The table doesn't
need to change for that.

## What each filter code produces

From `filters.py`, at 720 kHz (switch rate 20 kHz, analog path 40 kHz).
Each filter is built exactly as `build_standard_filter()` builds it (bilinear
transform pre-warped at the estimated peak), and "peak" is the peak of that
digital filter's response, found by a 5 Hz sweep. The 4-bit table value
settles in the 8-bit interpolation register at 16 times the target, so the
filters see the code itself (F1, F2Q, F3) or twice the code (F2, which uses
the top 5 bits). `filters.py` checks that by running the interpolator to
steady state from 0 and from 255.

### F1 and F3

| Code | F1 peak (Hz) | F1 Q | F3 peak (Hz) | F3 Q |
|---|---|---|---|---|
{{FILTER_TABLES}}

### Fixed parts of the chain

- F4 is a fixed resonance at about 4.0 kHz (Q about 23). Everything passes
  through it.
- The final low-pass sits near 4 kHz (MAME scales its capacitor values to
  put it there).
- MAME's noise shaper comes out flat with its formulas, so the noise is
  effectively white before FC splits it.
- The noise is gated by bit 6 of the pitch counter, and when gated off it
  sits at a constant negative level instead of zero, so every noisy sound
  also carries a pitch-rate buzz. That's in the circuit model, not the
  table.

### The F2 noise injection filter can go unstable

`build_injection_filter()` makes a first-order filter whose pole is
`(k1 - m) / (k1 + m)` with `k1 = cclock * (c1b * c3 / c2t - c2t)`. When F2Q
is high and F2 is low, `c2t` squared exceeds `c1b * c3`, `k1` goes negative,
and the pole leaves the unit circle. That filter carries noise all the time
(scaled by FC), so an entry in that region, or a transition that passes
through it, would blow up. The asterisks in the F2 table mark it. At F2
table code 0, F2Q 0 to 4 keep the pole radius under 0.98 (5 is just
inside the unit circle); at code 8, F2Q up to 13.

`params.py` only picks F2/F2Q pairs whose pole radius is under 0.98, and
`check()` steps the real interpolator between every pair of the 64 entries
(4096 transitions, F2 and F2Q moving together at 208 Hz) and asserts that no
intermediate pair is unstable. The worst pole radius met on any of those
paths is {{WORST_POLE}}. `sim.py` also counts unstable filter builds while it runs,
and none occurred.

### Pitch

The glottal period is `(0xE0 ^ (inflection << 5) ^ (F1 << 1)) + 2` updates
at 20 kHz. With inflection 0 that is 88.5 Hz at F1 code 0 down to 78.1 Hz at
code 15; inflection 1, 2, 3 give about 103 to 89, 123 to 104 and 154 to
125 Hz. So open vowels (high F1) come out a few Hz lower than close ones,
whatever the table says. I didn't try to compensate.

## How the table is chosen

### Formant targets

Each phoneme gets an (F1, F2, F3, B2) target from Klatt, by its example
word and its use in the dictionary's diphthong chart:

- Where Klatt gives an onset and an offglide, a monophthong uses the onset
  ([E] in "get" uses [E] onset). The parts of a diphthong use the matching
  half: A (day) is [e] onset, AY is [e] offglide; E and E1 (meet, be) are
  [i] offglide; U (move) is [u] offglide; IU (you, few) is the [u] onset,
  which Klatt measured fronted, as it is after [y].
- U1 is the offglide in found, foust, float and few, and the end of "you".
  Klatt's offglides for those end between [u] (320, 900) and the [o] and
  [aw] endings (450, 900 and 420, 940), so U1 sits between them at 380,
  920, 2280. That's my judgment.
- O2 (for; found = AH1 O2 U1) uses Klatt's [oy] onset; O and O1 use [o]
  onset; AW, AW1, AW2 use [O] onset; OO and OO1 use [U] onset.
- UH3 (mission) and UH2 (about) are schwa. Klatt has no schwa, so I used the
  uniform tube values 500, 1500, 2500 Hz, the textbook resonances of a
  17.5 cm tube closed at one end. UH and UH1 are Klatt's [V].
- The numbered variants (EH1/EH2/EH3, I1/I2/I3) differ mainly in duration.
  The short ones appear in unstressed syllables (jacket, enlist, the last
  i of inhibit), so I moved EH3 and I3 30% of the way toward schwa and EH2
  and I2 15%, as unstressed vowels centralize.
- Consonants use Klatt's Table III values, which double as loci for the
  formant transitions into and out of the consonant.
- M and N: the SC-01 has no nasal pole or zero, so F1 goes to Klatt's nasal
  pole FNP (270 Hz) to give a low murmur, with Klatt's F2 and F3 and a wider
  F2 bandwidth. NG has no Klatt entry; it gets the same murmur and the velar
  F2 and F3 of Klatt's [k] and [g]. That's a guess.
- H can't copy the next vowel's formants as Klatt suggests, so it uses
  schwa.

### Picking codes

- F1 and F3: the code whose peak is nearest the target on a log scale.
- F2 and F2Q together, over the stable pairs: minimize
  `|ln(peak / F2)| + 0.1 * |ln(bandwidth / B2)|`, so frequency matters most
  and bandwidth breaks ties.

What the hardware can't reach:

- F1 bottoms out at 175 Hz (Klatt's voiced stops want 200: fine) and tops
  out at 735 Hz (enough for [a] at 700).
- F2 can't go below about 725 Hz at a narrow bandwidth, so W (Klatt 610) gets
  725. Lower F2 is only reachable with a wide F2Q, near the unstable region.
- F3 tops out at 2830 Hz, so E, E1, Y and Y1 (Klatt 2960 to 3020) get 2830.
- Most vowels land on F2Q 0, a 70 Hz bandwidth, since most of Klatt's vowel B2
  values are 50 to 110 Hz and F2Q 1 is already 184 Hz.
- Phonemes that send noise through F3 (FA and FC both above 0) don't use
  the bandwidth rule. They get F2Q 8, and F2 is the nearest code at that
  F2Q (next section explains why).

### Amplitudes and noise routing: VA, FA, FC

MAME's analog path is linear in both VA and FA. FC splits the noise: FC/15
of it goes through the injection filter into F3, and (5 + 15 - FC)/20 of it
bypasses F3 and only meets the fixed 4 kHz F4 resonance. So FC 0 gives a
bright hiss centered near 4 kHz, and a high FC gives a noise peaked at the
F3 frequency.

The injection filter is a shelf whose corner moves with F2Q (last column
of the F2Q table). At F2Q 0 the noise reaching F3 is about 24 dB down at
2.75 kHz, so FC does almost nothing; at F2Q 8 it is about 1 dB down. So
every phoneme with FA and FC both above 0 uses F2Q 8. Their voicing is off
or weak, so the wide F2 bandwidth costs little, though the next vowel
starts with a wide F2 that narrows over about 40 ms. Measured with
`sim.py` at FA 15 and F2Q 8 (SH formants), FC 4 leaves about 28% of the
energy in the 2 to 3 kHz band and 52% in 3 to 4.5 kHz, while FC 10 puts
about 80% in 2 to 3 kHz. F3 is a narrow resonance (Q 18 at 2.75 kHz, fixed
by the circuit), so I stopped at FC 10 rather than 15 to keep some
broadband bypass noise under the peak.

Class rules, from data sheet Table 2:

- Vowels: VA 15, FA 0. FC 8, which only matters for the aspiration left
  over from a preceding stop. With the vowels' F2Q 0 the injection path is
  nearly shut, so that aspiration is mostly the bypass hiss.
- Sonorant consonants (L, R, W, Y1) VA 12 to 13, nasals VA 12. Klatt says
  about 10 dB under the vowel; the chip's own filter gains already take
  these down, so the measured levels end up about 8 dB under AH.
- Voiceless fricatives: VA 0. S: FC 0, the 4 kHz hiss. SH and CH: FC 10, so
  the noise peaks at F3 (2.75 to 2.83 kHz), which is the cue that tells
  them from S. F: FC 8, which at F2Q 8 spreads the noise fairly evenly from
  0 to 4 kHz, the nearest thing to Klatt's flat bypass-only [f]. TH: FC 4,
  a little brighter (Klatt's [th] adds some high-frequency A6).
- H: VA 0, FA 4, FC 10, aspiration colored by F3.
- Voiced fricatives (Z, ZH, J, V, THV): both. VA 3 to 4, from Klatt's AV 47
  against 60 (about 13 dB down). Their noise sits 0 to 4 dB under the
  voiceless partner's. Klatt has 10 dB (AF 50 against 60), but with voicing
  this low I kept more noise so the sound doesn't vanish. That's judgment.
- Stops: FA is the burst (see below). P: FC 8 (Klatt's [p] burst is flat,
  bypass only). T and D: FC 0, high-frequency burst (Klatt's A5/A6). K and G:
  FC 10, a burst peaked at F3 near 2.8 kHz (Klatt's A3). Voiced stops keep VA
  8 through the closure so voicing starts at the release; voiceless ones go
  to VA 0.
- Pauses and STOP: VA 0, FA 0.

The numbers were then trimmed against levels measured by `sim.py`: hold
the phoneme four times so everything settles, then take the RMS of a fifth
repeat, in dB against the vowel AH. The targets are my judgment, ordered
after Klatt's section IV.B and his frication amplitudes ([sh] strongest
among the fricatives, then [s], [f] and [th] weakest, voiced fricatives
under voiceless). RMS is taken around the mean, because the noise path
carries a DC offset. The levels are below; FA and VA are 4-bit, so the low
ones move in steps of 2 to 6 dB.

{{LEVELS}}

The vowels span +0.2 to -7.8 dB with VA 15 on all of them: open vowels (high
F1, high filter Q) come out loudest and [i]-like vowels quietest, which is
the usual ordering of intrinsic vowel intensity, so I left VA alone.

### Timing: CL, CLD, VD

How the chip uses them, from the spec and `chip_update()`: the closure
state changes only once per phoneme, at tick CLD, to CL, and then stays
until a later phoneme's CLD tick changes it. VA holds its old level until
tick CLD and FA until tick VD. When the closure state is 1, or when both VA
and FA have reached zero, the output gain ramps to silence over about 1.4 ms;
when it is 0 and either amplitude is nonzero, the gain snaps back to full at
once. CLD = 0 can never match the tick counter, so every entry has CLD of
at least 1.

So a stop can't open and close inside its own phoneme. The scheme here:

- **Stops (P T K B D G):** CL 1. CLD is set so about 12 ms of formant
  transition toward the stop's loci plays before the closure lands (2 or 3
  ticks). FA starts climbing at tick VD, set so about
  30 ms are left in the phoneme (enough for the 625 Hz interpolator to get
  most of the way), all while the output is muted. The closure is then
  released by the **next** phoneme's CLD tick, and the noise built up during
  the closure comes out as the burst and decays as aspiration while the next
  phoneme's VA rises. A voiceless stop leaves VA at 0, so voicing comes in
  after the burst; a voiced stop leaves VA at 8, so voicing starts with the
  burst.
- **DT (the flap in butter):** CL 1 with the closure placed in the last
  20 ms of its 47 ms, no burst, voicing kept on.
- **Vowels, nasals, liquids, glides, pauses, STOP:** CL 0, CLD 1, VD 1.
  CLD 1 releases any closure one tick in, and starts VA moving; VD 1 lets
  frication or aspiration from the previous phoneme decay from one tick in.
- **Fricatives and H:** CL 0, CLD 1, VD 0, so the noise starts moving at
  once.
- **Affricates:** following the dictionary, CH is played as T then CH, and J
  as D then J. CH and J are fricatives (CL 0), so the stop before them
  supplies the closure and they release it.

`params.py --seq` prints the envelopes for AH P AH, AH B AH, PA0 T AH, AH K
PA0, AH S AH, AE1 EH3 T CH and B UH2 DT ER. In AH P AH, for example, the
vowel continues for two P ticks, the output goes silent, FA climbs to 10
under the closure, and the second AH releases it one tick in with a burst
about 15 dB under the vowel, then voicing about 5 ms later. A stop before a
pause releases into the pause and its burst decays away, as an utterance-final
stop should.

## The table

"got" is what the chosen code actually produces. Source tags: K2 is Klatt
Table II, K3 is Klatt Table III, with the row named in brackets. The
brackets are my ASCII for his symbols: [i] beet, [I] bit, [e] bait, [E] bet,
[ae] bat, [a] father, [O] bought, [V] but, [o] boat, [U] book, [u] boot,
[er] bird, [oy] boy, [sh] shoe, [ch] chin, [j] jump, [th] thin, [dh] then.

{{PHONEMES}}

## Uncertain, and worth checking by ear

- **None of this has been listened to.** All checks are numeric (codes,
  levels, envelopes, stability). The values should be tuned by ear once the
  emulator speaks, especially the stop bursts and voice onset, the
  fricative levels, and the nasals.
- **Voice onset after P, T, K is short.** The release and the VA ramp both
  happen at the next phoneme's CLD tick, so aspiration lasts only as long as
  the burst noise takes to decay (about 20 ms), with voicing rising under it.
  English voiceless stops have 50 ms or more. A longer VD in the vowels would
  hold the noise longer but also smear every fricative into the next vowel.
- **F3-routed noise is narrow.** SH, CH, ZH, J, K, G and H get their color
  from F3, which has a fixed Q near 18 here (about 150 Hz wide), where real
  [sh] has a broad peak. It may sound whistly; a lower FC trades that for a
  more S-like hiss.
- **Wide F2Q after noise phonemes.** Those phonemes use F2Q 8, so the
  following vowel's F2 starts wide and narrows over about 40 ms.
- **The SC-01A is not the SC-01.** All durations are from the SC-01 sheet;
  the redcedar page says the A changed some phonemes. I couldn't find an
  SC-01A sheet.
- **A1 and A2:** the data sheet and MAME's name table disagree on which is
  $05 and which is $06. It only matters for duration, and I followed the
  data sheet.
- **NG, H, U1, IU and the reduced variants** rest on my judgment, as
  described above, not on a measured target.
- **Intrinsic pitch:** F1 also sets the glottal period, so open vowels come
  out a few Hz lower. That is the circuit's behavior, and I left it.
- **Klatt's values are one male speaker** and are meant for consonants
  before front vowels; a single table can't follow the coarticulation he
  describes.
