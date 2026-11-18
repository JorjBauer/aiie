#!/usr/bin/env python3
# Clean-room SC-01 parameter work: a small port of the chip model in MAME's
# votrax.cpp (BSD-3-Clause, Olivier Galibert): the phone timing, the
# interpolator, the closure gain, the pitch and noise generators and the
# analog filter chain.  It takes OUR parameter table (a list of 64 dicts),
# never the chip's ROM, and is used to check levels, envelopes and filter
# stability for the clean-room values.
#
# Import it from params.py; it is not meant to be run on its own.

import math

MAIN = 720000.0
CCLOCK = MAIN / 36.0
SCLOCK = MAIN / 18.0

GLOTTAL = [0, -4 / 7.0, 7 / 7.0, 6 / 7.0, 5 / 7.0, 4 / 7.0, 3 / 7.0, 2 / 7.0, 1 / 7.0]


def bits_to_caps(v, caps):
    t = 0.0
    for c in caps:
        if v & 1:
            t += c
        v >>= 1
    return t


def standard(c1t, c1b, c2t, c2b, c3, c4):
    k0 = c1t / (CCLOCK * c1b)
    k1 = c4 * c2t / (CCLOCK * c1b * c3)
    k2 = c4 * c2b / (CCLOCK * CCLOCK * c1b * c3)
    fpeak = math.sqrt(abs(k0 * k1 - k2)) / (2 * math.pi * k2)
    zc = 2 * math.pi * fpeak / math.tan(math.pi * fpeak / SCLOCK)
    m0, m1, m2 = zc * k0, zc * k1, zc * zc * k2
    return ([1 + m0, 3 + m0, 3 - m0, 1 - m0],
            [1 + m1 + m2, 3 + m1 - m2, 3 - m1 - m2, 1 - m1 + m2])


def lowpass(c1t, c1b):
    k = c1b / (CCLOCK * c1t) * (150.0 / 4000.0)
    fpeak = 1 / (2 * math.pi * k)
    zc = 2 * math.pi * fpeak / math.tan(math.pi * fpeak / SCLOCK)
    m = zc * k
    return [1.0], [1 + m, 1 - m]


def noise_shaper(c1, c2t, c2b, c3, c4):
    k0 = c2t * c3 * c2b / c4
    k1 = c2t * (CCLOCK * c2b)
    k2 = c1 * c2t * c3 / (CCLOCK * c4)
    fpeak = math.sqrt(1 / k2) / (2 * math.pi)
    zc = 2 * math.pi * fpeak / math.tan(math.pi * fpeak / SCLOCK)
    m0, m1, m2 = zc * k0, zc * k1, zc * zc * k2
    return [m0, 0, -m0], [1 + m1 + m2, 2 - 2 * m2, 1 - m1 + m2]


def injection(c1b, c2t, c2b, c3, c4):
    k0 = CCLOCK * c2t
    k1 = CCLOCK * (c1b * c3 / c2t - c2t)
    k2 = c2b
    m = 2 * SCLOCK * k2
    return [k0 + m, k0 - m], [k1 + m, k1 - m]


F2Q_CAPS = [1390, 2965, 5875, 11297]
F2_CAPS = [833, 1663, 3164, 6327, 12654]


def f2_filters(f2_5bit, q):
    qc = 829 + bits_to_caps(q, F2Q_CAPS)
    fc = 2352 + bits_to_caps(f2_5bit, F2_CAPS)
    v = standard(24840, 29154, qc, 38180, fc, 34270)
    n = injection(29154, qc, 38180, fc, 34270)
    return v, n


def injection_stable(f2_5bit, q):
    # Pole of the first-order injection filter is z = -b1/b0.
    (_, _), (a, b) = f2_filters(f2_5bit, q)
    return abs(b[1] / b[0]) < 1.0


def injection_pole(f2_5bit, q):
    (_, _), (a, b) = f2_filters(f2_5bit, q)
    return abs(b[1] / b[0])


def apply(x, y, a, b):
    # x: input history (x[0] newest), y: output history (y[0] newest, before
    # this sample); same arithmetic as MAME's apply_filter().
    total = 0.0
    for i in range(len(a)):
        total += x[i] * a[i]
    for i in range(1, len(b)):
        total -= y[i - 1] * b[i]
    return total / b[0]


def shift(v, h):
    h.insert(0, v)
    h.pop()


class Chip:
    def __init__(self, table, inflection=0):
        self.t = table
        self.inflection = inflection
        self.phone = 0x3F
        self.cur = dict(fa=0, fc=0, va=0, f1=0, f2=0, f2q=0, f3=0)
        self.filt = dict(fa=0, fc=0, va=0, f1=0, f2=0, f2q=0, f3=0)
        self.pitch = 0
        self.closure = 0
        self.update_counter = 0
        self.cur_closure = True
        self.noise = 0
        self.cur_noise = False
        self.phonetick = 0
        self.ticks = 0
        self.unstable_hits = 0
        self.h = {k: [0.0] * 4 for k in
                  ('v1', 'v2', 'v3', 'n1', 'n2', 'n3', 'n4',
                   'vn1', 'vn2', 'vn3', 'vn4', 'vn5', 'vn6')}
        self.f4 = standard(0, 28810, 1165, 21457, 8558, 7289)
        self.fx = lowpass(1122, 23131)
        self.fn = noise_shaper(15500, 14854, 8450, 9523, 14083)
        self.commit()
        self.filters_commit(True)

    def commit(self):
        self.phonetick = 0
        self.ticks = 0
        p = self.t[self.phone]
        self.rom = p
        self.pause = self.phone in (0x03, 0x3E)
        if p['cld'] == 0:
            self.cur_closure = bool(p['cl'])

    @staticmethod
    def interp(reg, target):
        return (reg - (reg >> 3) + (target << 1)) & 0xFF

    def filters_commit(self, force):
        c, f = self.cur, self.filt
        f['fa'] = c['fa'] >> 4
        f['fc'] = c['fc'] >> 4
        f['va'] = c['va'] >> 4
        if force or f['f1'] != c['f1'] >> 4:
            f['f1'] = c['f1'] >> 4
            self.f1 = standard(11247, 11797, 949, 52067,
                               2280 + bits_to_caps(f['f1'], [2546, 4973, 9861, 19724]), 166272)
        if force or f['f2'] != c['f2'] >> 3 or f['f2q'] != c['f2q'] >> 4:
            f['f2'] = c['f2'] >> 3
            f['f2q'] = c['f2q'] >> 4
            self.f2v, self.f2n = f2_filters(f['f2'], f['f2q'])
            if not injection_stable(f['f2'], f['f2q']):
                self.unstable_hits += 1
        if force or f['f3'] != c['f3'] >> 4:
            f['f3'] = c['f3'] >> 4
            self.f3 = standard(0, 17594, 868, 18828,
                               8480 + bits_to_caps(f['f3'], [2226, 4485, 9056, 18111]), 50019)

    def chip_update(self):
        r = self.rom
        if self.ticks != 0x10:
            self.phonetick += 1
            if self.phonetick == ((r['dur'] << 2) | 1):
                self.phonetick = 0
                self.ticks += 1
                if self.ticks == r['cld']:
                    self.cur_closure = bool(r['cl'])
        self.update_counter += 1
        if self.update_counter == 0x30:
            self.update_counter = 0
        tick_625 = not (self.update_counter & 0xF)
        tick_208 = self.update_counter == 0x28
        c, f = self.cur, self.filt
        if tick_208 and (not self.pause or not (f['fa'] or f['va'])):
            for k in ('fc', 'f1', 'f2', 'f2q', 'f3'):
                c[k] = self.interp(c[k], r[k])
        if tick_625:
            if self.ticks >= r['vd']:
                c['fa'] = self.interp(c['fa'], r['fa'])
            if self.ticks >= r['cld']:
                c['va'] = self.interp(c['va'], r['va'])
        if not self.cur_closure and (f['fa'] or f['va']):
            self.closure = 0
        elif self.closure != 7 << 2:
            self.closure += 1
        self.pitch = (self.pitch + 1) & 0xFF
        if self.pitch == (0xE0 ^ (self.inflection << 5) ^ (f['f1'] << 1)) + 2:
            self.pitch = 0
        if (self.pitch & 0xF9) == 0x08:
            self.filters_commit(False)
        inp = self.cur_noise and (self.noise != 0x7FFF)
        self.noise = ((self.noise << 1) & 0x7FFE) | (1 if inp else 0)
        self.cur_noise = not (((self.noise >> 14) ^ (self.noise >> 13)) & 1)

    def analog(self):
        h, f = self.h, self.filt
        v = 0.0 if self.pitch >= (9 << 3) else GLOTTAL[self.pitch >> 3]
        v = v * f['va'] / 15.0
        shift(v, h['v1'])
        v = apply(h['v1'], h['v2'], *self.f1); shift(v, h['v2'])
        v = apply(h['v2'], h['v3'], *self.f2v); shift(v, h['v3'])
        n = 1e4 * (1 if ((self.pitch & 0x40) and self.cur_noise) else -1)
        n = n * f['fa'] / 15.0
        shift(n, h['n1'])
        n = apply(h['n1'], h['n2'], *self.fn); shift(n, h['n2'])
        n2 = n * f['fc'] / 15.0
        shift(n2, h['n3'])
        n2 = apply(h['n3'], h['n4'], *self.f2n); shift(n2, h['n4'])
        vn = v + n2
        shift(vn, h['vn1'])
        vn = apply(h['vn1'], h['vn2'], *self.f3); shift(vn, h['vn2'])
        vn += n * (5 + (15 ^ f['fc'])) / 20.0
        shift(vn, h['vn3'])
        vn = apply(h['vn3'], h['vn4'], *self.f4); shift(vn, h['vn4'])
        vn = vn * (7 ^ (self.closure >> 2)) / 7.0
        shift(vn, h['vn5'])
        vn = apply(h['vn5'], h['vn6'], *self.fx); shift(vn, h['vn6'])
        return vn * 0.35

    def run_phone(self, code, extra_samples=0):
        """Strobe a phone and run until A/R rises (plus extra samples).
        Returns (samples, envelope rows).  Envelope rows are taken once per
        update: (filt_va, filt_fa, closure_gain, ticks)."""
        self.phone = code
        out, env = [], []
        commit_at = 72 // 18          # 72 master clocks, in 40 kHz samples
        total = commit_at + (16 * (self.t[code]['dur'] * 4 + 1) * 36 + 2) // 18
        count = 0
        for i in range(total + extra_samples):
            if i == commit_at:
                self.commit()
            count += 1
            if count & 1:
                self.chip_update()
                env.append((self.filt['va'], self.filt['fa'],
                            (7 ^ (self.closure >> 2)), self.ticks))
            out.append(self.analog())
        return out, env


def rms_db(x):
    if not x:
        return -999.0
    # The noise path carries a DC offset (the gated-off noise sits at a
    # negative level), so measure around the mean.
    m = sum(x) / len(x)
    s = sum((v - m) * (v - m) for v in x) / len(x)
    return 10 * math.log10(s) if s > 0 else -999.0
