#!/usr/bin/env python3
"""Make cross-segment calls readable in the combined decompilation.

  python name_thunks.py <workdir>

Every cross-segment call compiles to JSR through the loaded jump table, which Ghidra
prints as (*_LAB_0080xxxx)(). This maps each jump-table slot to the function it
reaches and rewrites the call to jt_<name> (jt_FUN_xxxxxx if not yet named in
<workdir>/symbols.tsv). Writes <workdir>/recon/thunk_map.tsv.
Re-run after every Ghidra rebuild, AFTER name_traps.py.
"""
import os
import re
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    w = argv[1]
    meta = dict(l.rstrip('\n').split('\t') for l in open(os.path.join(w, 'combined', 'meta.tsv')) if '\t' in l)
    jt_base = int(meta['JT_BASE'], 16)
    base0, stride = int(meta['SEG_BASE0'], 16), int(meta['SEG_STRIDE'], 16)
    names = {}
    sp = os.path.join(w, 'symbols.tsv')
    if os.path.exists(sp):
        for line in open(sp):
            p = line.strip().split('\t')
            if len(p) >= 3 and not line.startswith('#') and p[2] == 'F':
                names[int(p[0], 16)] = p[1]
    repl, rows = {}, []
    for line in open(os.path.join(w, 'recon', 'jumptable.tsv')):
        if line.startswith('jt_index'):
            continue
        idx, seg, off, ok = line.rstrip('\n').split('\t')
        thunk = jt_base + int(idx) * 8 + 2
        target = base0 + int(seg) * stride + 4 + int(off, 16)
        nm = names.get(target, 'FUN_%06x' % target)
        repl['_LAB_%08x' % thunk] = 'jt_' + nm
        rows.append((thunk, target, nm))
    f = os.path.join(w, 'ghidra_out', 'combined.c')
    t = open(f, encoding='utf-8', errors='replace').read()
    count = [0]

    def sub(m):
        r = repl.get(m.group(0))
        if r:
            count[0] += 1
            return r
        return m.group(0)
    t = re.sub(r'_LAB_[0-9a-f]{8}', sub, t)
    open(f, 'w', encoding='utf-8').write(t)
    with open(os.path.join(w, 'recon', 'thunk_map.tsv'), 'w') as out:
        out.write('thunk_addr\ttarget_addr\tname\n')
        for th, tg, nm in sorted(rows):
            out.write('0x%06X\t0x%06X\t%s\n' % (th, tg, nm))
    print(f'rewrote {count[0]} jump-table call sites; map in recon/thunk_map.tsv')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
