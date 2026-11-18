#!/usr/bin/env python3
# Clean-room SC-01 parameter work: what each 4-bit filter code produces.
#
# Builds the F1, F2 and F3 filters from the capacitor values in MAME's
# votrax.cpp (BSD-3-Clause, Olivier Galibert), exactly as its
# build_standard_filter() does (bilinear transform with pre-warp at the
# estimated peak), and reports the resonant peak of the digital filter
# MAME actually runs, plus the analog natural frequency and Q.
# No ROM data is used or needed: the codes are swept 0..15.
#
# Run: python3 -I tools/sc01-cleanroom/filters.py [master_clock_hz]

import cmath, math, sys

MAIN = 720000.0
if __name__ == '__main__' and len(sys.argv) > 1:
    MAIN = float(sys.argv[1])
CCLOCK = MAIN / 36.0   # switched-cap switching rate (20 kHz at 720 kHz)
SCLOCK = MAIN / 18.0   # analog path sample rate (40 kHz)

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
    a = [1 + m0, 3 + m0, 3 - m0, 1 - m0]
    b = [1 + m1 + m2, 3 + m1 - m2, 3 - m1 - m2, 1 - m1 + m2]
    f0 = 1 / (2 * math.pi * math.sqrt(k2))     # analog natural frequency
    q = math.sqrt(k2) / k1                      # analog Q of the poles
    return a, b, f0, q

def resp(a, b, f):
    z = cmath.exp(-2j * math.pi * f / SCLOCK)
    num = sum(a[i] * z ** i for i in range(len(a)))
    den = sum(b[i] * z ** i for i in range(len(b)))
    return abs(num / den)

def peak(a, b, lo=50.0, hi=None):
    hi = hi or SCLOCK / 2 * 0.98
    best, bf = -1, lo
    f = lo
    while f < hi:
        m = resp(a, b, f)
        if m > best:
            best, bf = m, f
        f += 5.0
    # -3 dB bandwidth around the peak
    lvl = best / math.sqrt(2)
    fl = bf
    while fl > 10 and resp(a, b, fl) > lvl:
        fl -= 2.0
    fh = bf
    while fh < hi and resp(a, b, fh) > lvl:
        fh += 2.0
    return bf, 20 * math.log10(best), fh - fl

def f1_filter(code):
    return standard(11247, 11797, 949, 52067,
                    2280 + bits_to_caps(code, [2546, 4973, 9861, 19724]), 166272)

def f2_filter(code5, q):
    return standard(24840, 29154,
                    829 + bits_to_caps(q, [1390, 2965, 5875, 11297]), 38180,
                    2352 + bits_to_caps(code5, [833, 1663, 3164, 6327, 12654]), 34270)

def f3_filter(code):
    return standard(0, 17594, 868, 18828,
                    8480 + bits_to_caps(code, [2226, 4485, 9056, 18111]), 50019)

def steady(target, shift):
    # MAME interpolate(): reg = reg - (reg >> 3) + (target << 1); the filter
    # sees reg >> shift (4 for most, 3 for F2).  Run to steady state from 0
    # and from 255 to show where a 4-bit table value settles.
    out = []
    for start in (0, 255):
        r = start
        for _ in range(200):
            r = (r - (r >> 3) + (target << 1)) & 0xff
        out.append(r >> shift)
    return out

def table():
    rows = {}
    print(f"master clock {MAIN:.0f} Hz, switch {CCLOCK:.0f} Hz, sample {SCLOCK:.0f} Hz\n")
    print("F1 code: digital peak Hz (analog f0, Q)")
    rows['f1'] = []
    for c in range(16):
        a, b, f0, q = f1_filter(c)
        p, g, bw = peak(a, b)
        rows['f1'].append(p)
        print(f"  {c:2d}: {p:6.0f}  (f0 {f0:6.0f}, Q {q:4.2f}, -3dB bw {bw:5.0f})")
    print("\nF2 table code t -> internal 5-bit value, peak Hz for each F2Q 0..15")
    rows['f2'] = []
    for t in range(16):
        s = steady(t, 3)
        line = []
        for q in range(16):
            a, b, f0, qq = f2_filter(s[0], q)
            p, g, bw = peak(a, b)
            line.append(p)
        rows['f2'].append(line)
        print(f"  {t:2d} (5-bit {s[0]:2d}): " + " ".join(f"{x:5.0f}" for x in line))
    print("\nF2Q code: analog Q and -3dB bandwidth at F2 table code 8 (5-bit 16)")
    for q in range(16):
        a, b, f0, qq = f2_filter(16, q)
        p, g, bw = peak(a, b)
        print(f"  {q:2d}: Q {qq:5.2f}  bw {bw:5.0f}  peak {p:5.0f}")
    print("\nF3 code: digital peak Hz (analog f0, Q)")
    rows['f3'] = []
    for c in range(16):
        a, b, f0, q = f3_filter(c)
        p, g, bw = peak(a, b)
        rows['f3'].append(p)
        print(f"  {c:2d}: {p:6.0f}  (f0 {f0:6.0f}, Q {q:4.2f}, -3dB bw {bw:5.0f})")
    print("\nF2 noise injection filter pole radius (>= 1 is UNSTABLE), "
          "rows: 5-bit F2 0..31, columns: F2Q 0..15")
    for f5 in range(32):
        qc = lambda q: 829 + bits_to_caps(q, [1390, 2965, 5875, 11297])
        fc = 2352 + bits_to_caps(f5, [833, 1663, 3164, 6327, 12654])
        cells = []
        for q in range(16):
            k1 = CCLOCK * (29154 * fc / qc(q) - qc(q))
            m = 2 * SCLOCK * 38180
            cells.append(abs((k1 - m) / (k1 + m)))
        print(f"  {f5:2d}: " + " ".join("  X  " if c >= 1 else f"{c:5.2f}" for c in cells))
    print("\nSteady state of the interpolator for a 4-bit target t (from 0, from 255):")
    print("  4-bit fields: " + " ".join(f"{t}:{steady(t,4)}" for t in range(16)))
    print("  F2 (>>3):     " + " ".join(f"{t}:{steady(t,3)}" for t in range(16)))
    print("\nPitch (inflection 0..3) per F1 code, Hz:")
    for c in range(16):
        ps = [CCLOCK / ((0xe0 ^ (i << 5) ^ (c << 1)) + 2) for i in range(4)]
        print(f"  {c:2d}: " + " ".join(f"{p:6.1f}" for p in ps))
    return rows

if __name__ == "__main__":
    table()
