#!/usr/bin/env python3
# Rebuild aiie-docs/sc01-cleanroom-params.md from doc-template.md, filling in
# the tables computed by filters.py, sim.py and params.py, so no number in
# the tables is typed by hand.
#
#   python3 -I tools/sc01-cleanroom/gen_doc.py

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import filters as F  # noqa: E402
import params  # noqa: E402
import sim  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'aiie-docs', 'sc01-cleanroom-params.md')


def filter_tables():
    out = []
    for c in range(16):
        a, b, f0, q = F.f1_filter(c)
        p1 = F.peak(a, b)[0]
        a, b, f03, q3 = F.f3_filter(c)
        p3 = F.peak(a, b)[0]
        out.append('| %d | %.0f | %.1f | %.0f | %.1f |' % (c, p1, q, p3, q3))
    out.append('')
    out.append('### F2, by table code and F2Q')
    out.append('')
    out.append('Peak frequency in Hz. Rows are the 4-bit table code (the filter sees '
               'twice it); columns are F2Q. A hyphen means the response has no resonant '
               'peak there (it is a low-pass); an asterisk marks a cell where the noise '
               'injection filter is unstable (see below).')
    out.append('')
    out.append('| F2 \\ F2Q | ' + ' | '.join(str(q) for q in range(16)) + ' |')
    out.append('|---' * 17 + '|')
    for t in range(16):
        cells = []
        for q in range(16):
            a, b, _, _ = F.f2_filter(2 * t, q)
            pk = F.peak(a, b)[0]
            s = '-' if pk < 100 else '%.0f' % pk
            if not sim.injection_stable(2 * t, q):
                s += '*'
            cells.append(s)
        out.append('| %d | ' % t + ' | '.join(cells) + ' |')
    out.append('')
    out.append('### F2Q: bandwidth, and the noise path')
    out.append('')
    out.append('Analog Q and measured -3 dB bandwidth of the F2 filter at table code 8 '
               '(about 1.8 kHz), and the gain of the noise injection filter at 2.75 kHz '
               'for the same code. Code 0 is the narrowest. Higher codes pull the peak '
               'down, which is why F2 and F2Q are chosen together, and open the noise '
               'path into F3.')
    out.append('')
    out.append('| F2Q | Q | Bandwidth (Hz) | Peak (Hz) | Noise into F3 at 2.75 kHz (dB) |')
    out.append('|---|---|---|---|---|')
    for q in range(16):
        a, b, f0, qq = F.f2_filter(16, q)
        pk, g, bw = F.peak(a, b)
        if sim.injection_stable(16, q):
            _, (na, nb) = sim.f2_filters(16, q)
            ng = '%.1f' % (20 * F.math.log10(F.resp(na, nb, 2750.0)))
        else:
            ng = 'unstable'
        out.append('| %d | %.2f | %.0f | %.0f | %s |' % (q, qq, bw, pk, ng))
    return '\n'.join(out)


def levels(rows):
    tab = params.table_for_sim(rows)
    ref, _ = params.steady_level(tab, 0x24)
    out = ['| Code | Sym | Level (dB re AH) | Target |', '|---|---|---|---|']
    for r in rows:
        if r['cls'] == 'pause':
            continue
        if r['cl']:
            out.append('| $%02X | %s | silent when held (closure) | |' % (r['code'], r['sym']))
            continue
        lv, _ = params.steady_level(tab, r['code'])
        t = params.LEVEL_TARGETS.get(r['sym'])
        out.append('| $%02X | %s | %.1f | %s |'
                   % (r['code'], r['sym'], lv - ref, '' if t is None else str(t)))
    return '\n'.join(out)


def phonemes(rows, tabs):
    f1tab, f2tab, f3tab = tabs
    out = ['| Code | Sym | Class | ms (got) | DUR | F1 target/got (code) | '
           'F2 target/got (code, F2Q) | F3 target/got (code) | VA | FA | FC | CL | CLD | VD '
           '| Target source |',
           '|' + '---|' * 15]
    for r in rows:
        t = r['tgt']
        pk = f2tab[(r['f2'], r['f2q'])][0]
        out.append('| $%02X | %s | %s | %d (%.1f) | %d | %d / %.0f (%d) | %d / %.0f (%d, %d) | '
                   '%d / %.0f (%d) | %d | %d | %d | %d | %d | %d | %s |'
                   % (r['code'], r['sym'], r['cls'], r['ms'], params.dur_to_ms(r['dur']),
                      r['dur'], t[0], f1tab[r['f1']], r['f1'], t[1], pk, r['f2'], r['f2q'],
                      t[2], f3tab[r['f3']], r['f3'], r['va'], r['fa'], r['fc'],
                      r['cl'], r['cld'], r['vd'], r['src']))
    return '\n'.join(out)


def main():
    rows, tabs = params.build()
    worst = params.check(rows)
    with open(os.path.join(HERE, 'doc-template.md')) as fh:
        doc = fh.read()
    doc = doc.replace('{{FILTER_TABLES}}', filter_tables())
    doc = doc.replace('{{LEVELS}}', levels(rows))
    doc = doc.replace('{{PHONEMES}}', phonemes(rows, tabs))
    doc = doc.replace('{{WORST_POLE}}', '%.2f' % worst)
    assert '{{' not in doc
    with open(OUT, 'w') as fh:
        fh.write(doc)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
