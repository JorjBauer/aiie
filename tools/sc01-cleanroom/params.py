#!/usr/bin/env python3
# Clean-room Votrax SC-01A phoneme parameters for aiie.
#
# Every value here comes from public, non-ROM sources:
#   * phoneme codes, symbols, example words and durations: the Votrax SC-01
#     data sheet (1980), Table 1, and the Votrax "Phonetic Speech Dictionary
#     for the SC-01" (1981), Tables 1 and 2;
#   * formant targets: Klatt (1980), JASA 67(3), Tables II and III;
#   * the code-to-frequency mapping: the capacitor values and filter
#     formulas in MAME's votrax.cpp (BSD-3-Clause), via sim.py/filters.py.
# The chip's own ROM was never read.  See aiie-docs/sc01-cleanroom-params.md.
#
#   python3 -I tools/sc01-cleanroom/params.py            write apple/sc01params.h
#   python3 -I tools/sc01-cleanroom/params.py --report   also print the
#        quantization, the measured levels and the transition checks

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sim  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
HEADER = os.path.join(ROOT, 'apple', 'sc01params.h')

# Neutral (uniform tube) vowel used for reduction and for H.
SCHWA = (500, 1500, 2500)

# ---------------------------------------------------------------------------
# The 64 phonemes: data sheet symbol, data sheet duration (ms), class, and
# formant targets (F1, F2, F3, B2 in Hz).  "red" pulls a short, unstressed
# variant that fraction of the way toward SCHWA (vowel reduction).
# Source tags: K2 = Klatt Table II row, K3 = Klatt Table III row.
# ---------------------------------------------------------------------------
P = [
    # code  sym     ms   class        F1    F2    F3   B2   red  source
    (0x00, 'EH3',   59, 'vowel',    530, 1680, 2500,  90, 0.30, 'K2 [E] onset, reduced (jacket)'),
    (0x01, 'EH2',   71, 'vowel',    530, 1680, 2500,  90, 0.15, 'K2 [E] onset, reduced (enlist)'),
    (0x02, 'EH1',  121, 'vowel',    530, 1680, 2500,  90, 0.00, 'K2 [E] onset (heavy)'),
    (0x03, 'PA0',   47, 'pause',    500, 1500, 2500, 100, 0.00, 'silence'),
    (0x04, 'DT',    47, 'flap',     200, 1600, 2600, 100, 0.00, 'K3 [d] (butter)'),
    (0x05, 'A2',    71, 'vowel',    480, 1720, 2520, 100, 0.00, 'K2 [e] onset (made, short)'),
    (0x06, 'A1',   103, 'vowel',    480, 1720, 2520, 100, 0.00, 'K2 [e] onset (made)'),
    (0x07, 'ZH',    90, 'vfric',    300, 1840, 2750, 100, 0.00, 'K3 [sh] formants (azure)'),
    (0x08, 'AH2',   71, 'vowel',    700, 1220, 2600,  70, 0.00, 'K2 [a] (honest)'),
    (0x09, 'I3',    55, 'vowel',    400, 1800, 2570, 100, 0.30, 'K2 [I] onset, reduced'),
    (0x0A, 'I2',    80, 'vowel',    400, 1800, 2570, 100, 0.15, 'K2 [I] onset, reduced'),
    (0x0B, 'I1',   121, 'vowel',    400, 1800, 2570, 100, 0.00, 'K2 [I] onset (inhibit)'),
    (0x0C, 'M',    103, 'nasal',    270, 1270, 2130, 200, 0.00, 'K3 [m], F1 at FNP'),
    (0x0D, 'N',     80, 'nasal',    270, 1340, 2470, 300, 0.00, 'K3 [n], F1 at FNP'),
    (0x0E, 'B',     71, 'vstop',    200, 1100, 2150, 110, 0.00, 'K3 [b]'),
    (0x0F, 'V',     71, 'vfric',    220, 1100, 2080,  90, 0.00, 'K3 [v]'),
    (0x10, 'CH',    71, 'fric',     350, 1800, 2820,  90, 0.00, 'K3 [ch] frication'),
    (0x11, 'SH',   121, 'fric',     300, 1840, 2750, 100, 0.00, 'K3 [sh]'),
    (0x12, 'Z',     71, 'vfric',    240, 1390, 2530,  60, 0.00, 'K3 [z]'),
    (0x13, 'AW1',  146, 'vowel',    600,  990, 2570, 100, 0.00, 'K2 [O] onset (lawful)'),
    (0x14, 'NG',   121, 'nasal',    270, 1990, 2850, 300, 0.00, 'murmur F1 as [m]/[n], velar loci of K3 [k]'),
    (0x15, 'AH1',  146, 'vowel',    700, 1220, 2600,  70, 0.00, 'K2 [a] (father)'),
    (0x16, 'OO1',  103, 'vowel',    450, 1100, 2350, 100, 0.00, 'K2 [U] onset (looking)'),
    (0x17, 'OO',   185, 'vowel',    450, 1100, 2350, 100, 0.00, 'K2 [U] onset (book)'),
    (0x18, 'L',    103, 'liquid',   310, 1050, 2880, 100, 0.00, 'K3 [l]'),
    (0x19, 'K',     80, 'stop',     300, 1990, 2850, 160, 0.00, 'K3 [k]'),
    (0x1A, 'J',     47, 'vfric',    260, 1800, 2820,  80, 0.00, 'K3 [j] frication (judge)'),
    (0x1B, 'H',     71, 'aspir',    500, 1500, 2500, 200, 0.00, 'neutral tube; K text: H takes the next vowel'),
    (0x1C, 'G',     71, 'vstop',    200, 1990, 2850, 150, 0.00, 'K3 [g]'),
    (0x1D, 'F',    103, 'fric',     340, 1100, 2080, 120, 0.00, 'K3 [f]'),
    (0x1E, 'D',     55, 'vstop',    200, 1600, 2600, 100, 0.00, 'K3 [d]'),
    (0x1F, 'S',     90, 'fric',     320, 1390, 2530,  80, 0.00, 'K3 [s]'),
    (0x20, 'A',    185, 'vowel',    480, 1720, 2520, 100, 0.00, 'K2 [e] onset (day)'),
    (0x21, 'AY',    65, 'vowel',    330, 2020, 2600, 100, 0.00, 'K2 [e] offglide (day)'),
    (0x22, 'Y1',    80, 'glide',    260, 2070, 3020, 250, 0.00, 'K3 [y] (yard)'),
    (0x23, 'UH3',   47, 'vowel',    500, 1500, 2500, 100, 0.00, 'schwa (mission)'),
    (0x24, 'AH',   250, 'vowel',    700, 1220, 2600,  70, 0.00, 'K2 [a] (mop)'),
    (0x25, 'P',    103, 'stop',     400, 1100, 2150, 150, 0.00, 'K3 [p]'),
    (0x26, 'O',    185, 'vowel',    540, 1100, 2300,  70, 0.00, 'K2 [o] onset (cold)'),
    (0x27, 'I',    185, 'vowel',    400, 1800, 2570, 100, 0.00, 'K2 [I] onset (pin)'),
    (0x28, 'U',    185, 'vowel',    320,  900, 2200, 110, 0.00, 'K2 [u] offglide (move)'),
    (0x29, 'Y',    103, 'vowel',    310, 2020, 2960, 200, 0.00, 'K2 [i] onset (any)'),
    (0x2A, 'T',     71, 'stop',     400, 1600, 2600, 120, 0.00, 'K3 [t]'),
    (0x2B, 'R',     90, 'liquid',   310, 1060, 1380, 100, 0.00, 'K3 [r]'),
    (0x2C, 'E',    185, 'vowel',    290, 2070, 2960, 200, 0.00, 'K2 [i] offglide (meet)'),
    (0x2D, 'W',     80, 'glide',    290,  610, 2150,  80, 0.00, 'K3 [w]'),
    (0x2E, 'AE',   185, 'vowel',    620, 1660, 2430, 150, 0.00, 'K2 [ae] onset (dad)'),
    (0x2F, 'AE1',  103, 'vowel',    620, 1660, 2430, 150, 0.00, 'K2 [ae] onset (after)'),
    (0x30, 'AW2',   90, 'vowel',    600,  990, 2570, 100, 0.00, 'K2 [O] onset (salty)'),
    (0x31, 'UH2',   71, 'vowel',    500, 1500, 2500, 100, 0.00, 'schwa (about)'),
    (0x32, 'UH1',  103, 'vowel',    620, 1220, 2550,  50, 0.00, 'K2 [V] (uncle)'),
    (0x33, 'UH',   185, 'vowel',    620, 1220, 2550,  50, 0.00, 'K2 [V] (cup)'),
    (0x34, 'O2',    80, 'vowel',    550,  960, 2400,  50, 0.00, 'K2 [oy] onset (for)'),
    (0x35, 'O1',   121, 'vowel',    540, 1100, 2300,  70, 0.00, 'K2 [o] onset (aboard)'),
    (0x36, 'IU',    59, 'vowel',    350, 1250, 2200, 110, 0.00, 'K2 [u] onset, fronted after [y] (you)'),
    (0x37, 'U1',    90, 'vowel',    380,  920, 2280, 110, 0.00, 'between K2 [u] and [o]/[aw] offglides'),
    (0x38, 'THV',   80, 'vfric',    270, 1290, 2540,  80, 0.00, 'K3 [dh] (the)'),
    (0x39, 'TH',    71, 'fric',     320, 1290, 2540,  90, 0.00, 'K3 [th] (thin)'),
    (0x3A, 'ER',   146, 'vowel',    470, 1270, 1540,  60, 0.00, 'K2 [er] onset (bird)'),
    (0x3B, 'EH',   185, 'vowel',    530, 1680, 2500,  90, 0.00, 'K2 [E] onset (get)'),
    (0x3C, 'E1',   121, 'vowel',    290, 2070, 2960, 200, 0.00, 'K2 [i] offglide (be)'),
    (0x3D, 'AW',   250, 'vowel',    600,  990, 2570, 100, 0.00, 'K2 [O] onset (call)'),
    (0x3E, 'PA1',  185, 'pause',    500, 1500, 2500, 100, 0.00, 'silence'),
    (0x3F, 'STOP',  47, 'pause',    500, 1500, 2500, 100, 0.00, 'silence'),
]

# ---------------------------------------------------------------------------
# Source amplitudes and noise routing, per phoneme.  VA and FA are linear on
# the die (MAME analog_calc).  FC splits the noise: FC/15 of it goes through
# the injection filter into the F3 resonator, (5 + 15 - FC)/20 bypasses F3
# and only meets the fixed F4 resonance near 4 kHz.  The injection path is
# only open when F2Q is wide (see NOISE_F2Q), so the vowels' FC 8 adds little
# F3 color to the aspiration left by a stop.  Values were set from the phonetic class (data
# sheet Table 2), then trimmed against the levels sim.py measures (see
# LEVEL_TARGETS and the doc).
# ---------------------------------------------------------------------------
#          VA  FA  FC
AMP = {
    'vowel':  (15,  0,  8),
    'liquid': (12,  0,  8),
    'glide':  (12,  0,  8),
    'nasal':  (12,  0,  8),
    'pause':  ( 0,  0,  8),
}
AMP_BY_NAME = {
    # voiceless fricatives and aspiration
    'S':   ( 0,  8,  0),
    'SH':  ( 0, 10, 10),
    'CH':  ( 0, 10, 10),
    'F':   ( 0,  2,  8),
    'TH':  ( 0,  2,  4),
    'H':   ( 0,  4, 10),
    # voiced fricatives
    'Z':   ( 4,  5,  0),
    'ZH':  ( 4,  6, 10),
    'J':   ( 4,  6, 10),
    'V':   ( 3,  2,  8),
    'THV': ( 3,  2,  4),
    # stops: FA is the release burst, built up silently during the closure
    'P':   ( 0, 10,  8),
    'T':   ( 0, 10,  0),
    'K':   ( 0,  9, 10),
    'B':   ( 8,  5,  8),
    'D':   ( 8,  5,  0),
    'G':   ( 8,  8, 10),
    'DT':  (12,  0,  8),
    # sonorant consonants
    'L':   (13,  0,  8),
    'R':   (13,  0,  8),
}

# Output level we aim for, in dB relative to the steady vowel AH, measured
# by sim.py over the steady part of each phoneme.  These are judgment
# values ordered after Klatt (1980) section IV.B: voiced fricatives under
# voiceless ones, sonorant consonants under vowels, [sh] strongest among the
# fricatives (Klatt's largest frication amplitudes), [f] and [th] weakest.
LEVEL_TARGETS = {
    'S': -12, 'SH': -10, 'CH': -10, 'F': -24, 'TH': -26, 'H': -18,
    'Z': -14, 'ZH': -13, 'J': -13, 'V': -20, 'THV': -21,
    'M': -8, 'N': -8, 'NG': -8, 'L': -7, 'R': -7, 'W': -7, 'Y1': -7,
}


def ms_to_dur(ms):
    # Strobe to A/R high = 72 + 16*(DUR*4+1)*36 + 2 master clocks (spec).
    d = round(((ms * 720.0 - 74.0) / 576.0 - 1.0) / 4.0)
    return max(0, min(127, d))


def dur_to_ms(d):
    return (72 + 16 * (d * 4 + 1) * 36 + 2) / 720.0


def tick_ms(d):
    return (d * 4 + 1) / 20.0


# ---------------------------------------------------------------------------
# Filter code tables (computed from the capacitor model).
# ---------------------------------------------------------------------------
def build_tables():
    import filters as F
    f1 = []
    for c in range(16):
        a, b, _, _ = F.f1_filter(c)
        f1.append(F.peak(a, b)[0])
    f3 = []
    for c in range(16):
        a, b, _, _ = F.f3_filter(c)
        f3.append(F.peak(a, b)[0])
    f2 = {}
    for t in range(16):
        for q in range(16):
            a, b, _, _ = F.f2_filter(2 * t, q)
            pk, gain, bw = F.peak(a, b)
            f2[(t, q)] = (pk, bw, gain, sim.injection_pole(2 * t, q))
    return f1, f2, f3


POLE_MARGIN = 0.98      # injection filter pole radius we allow


def nearest_log(table, target):
    return min(range(16), key=lambda c: abs(math.log(table[c] / target)))


# F2Q also sets the corner of the noise injection shelf: at F2Q 0 the noise
# reaching F3 is about 24 dB down at 2.75 kHz, at F2Q 8 about 1 dB down.
# So phonemes that route noise through F3 (FA and FC both above 0) use a
# wide F2Q; their voicing is off or weak, so B2 doesn't matter for them.
NOISE_F2Q = 8


def pick_f2(f2tab, target, bw_target, noise=False):
    best = None
    for (t, q), (pk, bw, gain, pole) in f2tab.items():
        if pk < 300 or pole >= POLE_MARGIN:
            continue
        if noise and q != NOISE_F2Q:
            continue
        cost = abs(math.log(pk / target)) + 0.10 * abs(math.log(bw / bw_target))
        if best is None or cost < best[0]:
            best = (cost, t, q)
    return best[1], best[2]


def stops_timing(ms):
    d = ms_to_dur(ms)
    tk = tick_ms(d)
    # About 12 ms of formant transition before the closure lands.
    cld = max(1, min(15, round(12.0 / tk)))
    # Start the burst noise early enough to settle (about 30 ms, i.e. 18
    # steps of the 625 Hz interpolator) before the phoneme ends.
    vd = max(cld, min(15, 16 - math.ceil(30.0 / tk)))
    return cld, vd


def build():
    f1tab, f2tab, f3tab = build_tables()
    rows = []
    for code, sym, ms, cls, F1, F2, F3, B2, red, src in P:
        F1t = F1 + red * (SCHWA[0] - F1)
        F2t = F2 + red * (SCHWA[1] - F2)
        F3t = F3 + red * (SCHWA[2] - F3)
        f1 = nearest_log(f1tab, F1t)
        f3 = nearest_log(f3tab, F3t)
        if sym in AMP_BY_NAME:
            va, fa, fc = AMP_BY_NAME[sym]
        else:
            va, fa, fc = AMP[cls]
        f2, f2q = pick_f2(f2tab, F2t, B2, noise=(fa > 0 and fc > 0))
        dur = ms_to_dur(ms)
        cl, cld, vd = 0, 1, 1
        if cls in ('stop', 'vstop'):
            cl = 1
            cld, vd = stops_timing(ms)
        elif cls == 'flap':
            cl = 1
            # A flap closure is short, about 20 ms, at the end of the phoneme.
            cld = max(1, min(15, 16 - round(20.0 / tick_ms(dur))))
            vd = cld
        elif cls in ('fric', 'vfric', 'aspir'):
            vd = 0
        rows.append(dict(code=code, sym=sym, ms=ms, cls=cls, src=src,
                         tgt=(round(F1t), round(F2t), round(F3t), B2),
                         f1=f1, f2=f2, f2q=f2q, f3=f3, va=va, fa=fa, fc=fc,
                         cld=cld, vd=vd, cl=cl, dur=dur))
    return rows, (f1tab, f2tab, f3tab)


def check(rows):
    assert len(rows) == 64
    for i, r in enumerate(rows):
        assert r['code'] == i
        for k in ('f1', 'f2', 'f2q', 'f3', 'va', 'fa', 'fc', 'cld', 'vd'):
            assert 0 <= r[k] <= 15, (r['sym'], k)
        assert r['cl'] in (0, 1)
        assert 0 <= r['dur'] <= 127
        assert r['cld'] >= 1, r['sym']     # tick comparator never matches 0
    for c in (0x03, 0x3E, 0x3F):
        assert rows[c]['va'] == 0 and rows[c]['fa'] == 0
    # Every F2/F2Q pair the interpolator can pass through, between any two
    # phonemes, must keep the injection filter stable.
    worst = 0.0
    for a in rows:
        for b in rows:
            f2 = 16 * a['f2']
            q = 16 * a['f2q']
            for _ in range(80):
                f2 = (f2 - (f2 >> 3) + (b['f2'] << 1)) & 0xFF
                q = (q - (q >> 3) + (b['f2q'] << 1)) & 0xFF
                p = sim.injection_pole(f2 >> 3, q >> 4)
                worst = max(worst, p)
                assert p < 1.0, (a['sym'], b['sym'], f2 >> 3, q >> 4)
    return worst


def table_for_sim(rows):
    return [{k: r[k] for k in ('f1', 'f2', 'f2q', 'f3', 'va', 'fa', 'fc',
                               'cld', 'vd', 'cl', 'dur')} for r in rows]


def steady_level(table, code, settle=4):
    """Hold the phoneme long enough to settle and measure its RMS (dB)."""
    chip = sim.Chip(table)
    for _ in range(settle):
        chip.run_phone(code)
    out, _ = chip.run_phone(code)
    return sim.rms_db(out), chip.unstable_hits


def write_header(rows):
    out = []
    out.append('#ifndef __SC01PARAMS_H')
    out.append('#define __SC01PARAMS_H')
    out.append('')
    out.append('#include <stdint.h>')
    out.append('')
    out.append('// Votrax SC-01A phoneme parameters: a CLEAN-ROOM replacement for the')
    out.append("// chip's internal phone ROM. None of these values came from that ROM.")
    out.append('//')
    out.append('// Generated by tools/sc01-cleanroom/params.py on 2026-11-18; edit the')
    out.append('// script, not this file. Method and sources: aiie-docs/sc01-cleanroom-params.md.')
    out.append('//')
    out.append('// Sources:')
    out.append('//  - Votrax SC-01 Speech Synthesizer data sheet (1980), Table 1 (codes,')
    out.append('//    symbols, durations) and Table 2 (voicing classes):')
    out.append('//    https://redcedar.com/sc01/sc01.pdf')
    out.append('//  - Votrax "Phonetic Speech Dictionary for the SC-01" (1981), Tables 1, 2:')
    out.append('//    https://downloads.reactivemicro.com/Electronics/Speech/SC-01%20Dictionary.pdf')
    out.append('//  - D. H. Klatt, "Software for a cascade/parallel formant synthesizer",')
    out.append('//    JASA 67(3), 1980, Tables II and III (formant targets):')
    out.append('//    https://www.fon.hum.uva.nl/david/ba_shs/2009/klatt-1980.pdf')
    out.append("//  - MAME votrax.cpp (BSD-3-Clause, Olivier Galibert): the filter capacitor")
    out.append('//    values, used to work out which 4-bit code gives which frequency.')
    out.append('//')
    out.append('// Method: DUR inverts the strobe to A/R formula against the data sheet')
    out.append('// durations (720 kHz). F1/F3 are the codes whose computed resonance is')
    out.append('// nearest the target; F2 and F2Q are chosen together (frequency first,')
    out.append('// then bandwidth) inside the region where the F2 noise injection filter')
    out.append('// stays stable. VA/FA/FC follow the phonetic class, trimmed against')
    out.append('// levels measured in a port of the MAME circuit model. Stops close at')
    out.append('// tick CLD and build their burst noise silently from tick VD; the next')
    out.append('// phoneme releases it.')
    out.append('//')
    out.append('// PA0 ($03) and PA1 ($3E) are pauses by hard-wired logic on the die, not')
    out.append('// by anything in this table; the emulator applies that rule itself.')
    out.append('')
    out.append('struct SC01Phone {')
    out.append('  uint8_t f1, f2, f2q, f3;  // 4-bit filter codes')
    out.append('  uint8_t va, fa, fc;       // 4-bit amplitudes')
    out.append('  uint8_t cld, vd;          // 4-bit tick delays')
    out.append('  uint8_t cl;               // closure bit')
    out.append('  uint8_t dur;              // 7-bit tick length')
    out.append('};')
    out.append('')
    out.append('// Indexed by phoneme code $00..$3F.')
    out.append('static const SC01Phone sc01Phones[64] = {')
    out.append('  //  f1  f2 f2q  f3   va  fa  fc  cld vd cl  dur')
    for r in rows:
        body = ('{ %2d, %2d, %2d, %2d,  %2d, %2d, %2d,  %2d, %2d, %d, %3d }'
                % (r['f1'], r['f2'], r['f2q'], r['f3'], r['va'], r['fa'], r['fc'],
                   r['cld'], r['vd'], r['cl'], r['dur']))
        out.append('  %s,  // $%02X %-4s %3d ms' % (body, r['code'], r['sym'], r['ms']))
    out.append('};')
    out.append('')
    out.append('#endif')
    out.append('')
    with open(HEADER, 'w') as fh:
        fh.write('\n'.join(out))


def report(rows, tabs):
    f1tab, f2tab, f3tab = tabs
    print('code sym   ms->dur(ms)  F1 tgt/got     F2 tgt/got(bw)        F3 tgt/got   '
          'va fa fc cld vd cl')
    for r in rows:
        t = r['tgt']
        pk, bw, _, _ = f2tab[(r['f2'], r['f2q'])]
        print('$%02X %-4s %3d->%3d(%5.1f) %4d/%4d(%2d) %4d/%4d(%2d,q%2d,bw%4d) %4d/%4d(%2d)  '
              '%2d %2d %2d %2d %2d %d'
              % (r['code'], r['sym'], r['ms'], r['dur'], dur_to_ms(r['dur']),
                 t[0], f1tab[r['f1']], r['f1'],
                 t[1], pk, r['f2'], r['f2q'], bw,
                 t[2], f3tab[r['f3']], r['f3'],
                 r['va'], r['fa'], r['fc'], r['cld'], r['vd'], r['cl']))
    table = table_for_sim(rows)
    ref, _ = steady_level(table, 0x24)
    print('\nSteady levels, dB relative to AH ($24):')
    for r in rows:
        if r['cls'] == 'pause':
            continue
        lv, bad = steady_level(table, r['code'])
        tgt = LEVEL_TARGETS.get(r['sym'])
        extra = '' if tgt is None else '  target %+d' % tgt
        print('  $%02X %-4s %6.1f%s%s' % (r['code'], r['sym'], lv - ref, extra,
                                          '  UNSTABLE' if bad else ''))


def markdown(rows, tabs):
    f1tab, f2tab, f3tab = tabs
    print('| Code | Sym | Class | ms (got) | DUR | F1 target/got (code) | '
          'F2 target/got (code, F2Q) | F3 target/got (code) | VA | FA | FC | CL | CLD | VD | Target source |')
    print('|' + '---|' * 15)
    for r in rows:
        t = r['tgt']
        pk = f2tab[(r['f2'], r['f2q'])][0]
        print('| $%02X | %s | %s | %d (%.1f) | %d | %d / %d (%d) | %d / %d (%d, %d) | '
              '%d / %d (%d) | %d | %d | %d | %d | %d | %d | %s |'
              % (r['code'], r['sym'], r['cls'], r['ms'], dur_to_ms(r['dur']), r['dur'],
                 t[0], f1tab[r['f1']], r['f1'], t[1], pk, r['f2'], r['f2q'],
                 t[2], f3tab[r['f3']], r['f3'], r['va'], r['fa'], r['fc'],
                 r['cl'], r['cld'], r['vd'], r['src']))


SEQUENCES = [
    ('AH P AH', ['AH', 'P', 'AH']),
    ('AH B AH', ['AH', 'B', 'AH']),
    ('PA0 T AH', ['PA0', 'T', 'AH']),
    ('AH K PA0', ['AH', 'K', 'PA0']),
    ('AH S AH', ['AH', 'S', 'AH']),
    ('AE1 EH3 T CH', ['AE1', 'EH3', 'T', 'CH', 'PA0']),
    ('B UH2 DT ER', ['B', 'UH2', 'DT', 'ER', 'PA0']),
]


def seq_report(rows, frame_ms=5.0):
    import sim as S
    table = table_for_sim(rows)
    by = {r['sym']: r['code'] for r in rows}
    ref, _ = steady_level(table, 0x24)
    n = int(frame_ms * 40)
    for title, syms in SEQUENCES:
        chip = S.Chip(table)
        chip.run_phone(by['PA1'])
        print('\n%s: per %g ms frame, level dB re AH : VA/FA/closure gain(0..7)' % (title, frame_ms))
        for sym in syms:
            out, env = chip.run_phone(by[sym])
            cells = []
            for i in range(0, len(out) - n + 1, n):
                lv = S.rms_db(out[i:i + n]) - ref
                e = env[min(len(env) - 1, (i + n) // 2 - 1)]
                cells.append('%s:%d/%d/%d' % ('mute' if lv < -60 else '%.0f' % lv,
                                             e[0], e[1], e[2]))
            print('  %-4s' % sym + ' '.join(cells))


if __name__ == '__main__':
    rows, tabs = build()
    worst = check(rows)
    write_header(rows)
    print('wrote', HEADER, '(worst injection pole radius %.3f)' % worst)
    if '--report' in sys.argv:
        report(rows, tabs)
    if '--markdown' in sys.argv:
        markdown(rows, tabs)
    if '--seq' in sys.argv:
        seq_report(rows)
