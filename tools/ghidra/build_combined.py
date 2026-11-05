#!/usr/bin/env python3
"""Lay out a whole 68k Mac app as ONE flat program for Ghidra.

  python build_combined.py <workdir>          (run tools/macres/code_extract.py first)

Why: each CODE segment alone decompiles badly - cross-segment calls go through the
jump table (JSR d16(A5)) and globals are A5-relative, so Ghidra sees neither. This
places every segment at its own base, synthesizes the LOADED jump table (what the
Segment Loader builds at run time) and pins A5, so that calls, globals and the jump
table all resolve. BuildCombined.java consumes the files written here.

Layout (fixed, so addresses are stable across rebuilds and easy to read):
  CODE n        at SEG_BASE0 + n*SEG_STRIDE   (0x100000 + n*0x10000; offset = CODE offset)
  app globals   [A5 - belowA5, A5)            (uninitialized)
  A5 world      [A5, A5 + aboveA5)            (loaded jump table at A5 + jtOffset)
  A5            0x800000
So: address 0x1F1522 = CODE 15 + 0x1522, and DAT_007fxxxx are globals (A5 - n).

Writes <workdir>/combined/{jt_block.bin,layout.tsv,entries.txt,meta.tsv,dummy.bin}
"""
import json
import os
import struct
import sys

A5 = 0x00800000
SEG_BASE0 = 0x00100000
SEG_STRIDE = 0x00010000          # segments are < 32 KB in the near model


def seg_base(n):
    return SEG_BASE0 + n * SEG_STRIDE


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    w = argv[1]
    os.makedirs(os.path.join(w, 'combined'), exist_ok=True)
    m = json.load(open(os.path.join(w, 'recon', 'manifest.json')))
    below = m['jump_table']['below_a5_globals']
    above = m['jump_table']['above_a5']
    jt_off = m['jump_table']['jt_offset_from_a5']
    if max(s['segment'] for s in m['code_segments']) * SEG_STRIDE + SEG_BASE0 >= A5 - below:
        print('too many segments for this layout: raise A5 in build_combined.py')
        return 2
    sizes = {s['segment']: s['size'] for s in m['code_segments'] if s['segment']}
    exports = []
    for line in open(os.path.join(w, 'recon', 'jumptable.tsv')):
        if line.startswith('jt_index'):
            continue
        idx, seg, off, ok = line.rstrip('\n').split('\t')
        exports.append((int(idx), int(seg), int(off, 16)))

    size = (above + 1) & ~1
    jt = bytearray(size)
    entries = []
    for idx, seg, off in exports:
        target = seg_base(seg) + 4 + off            # entry offsets are relative to code start (+4)
        pos = jt_off + idx * 8
        struct.pack_into('>HHI', jt, pos, off & 0xFFFF, 0x4EF9, target)   # [off][JMP abs.L][addr]
        entries.append(target)
    cdir = os.path.join(w, 'combined')
    open(os.path.join(cdir, 'jt_block.bin'), 'wb').write(jt)
    with open(os.path.join(cdir, 'layout.tsv'), 'w') as f:
        for n in sorted(sizes):
            f.write('CODE_%02d\t0x%X\t0x%X\t1\tsegments/CODE_%02d.bin\n' % (n, seg_base(n), sizes[n], n))
        f.write('GLOBALS\t0x%X\t0x%X\t0\tZERO\n' % (A5 - below, (below + 1) & ~1))
        f.write('A5WORLD\t0x%X\t0x%X\t1\tcombined/jt_block.bin\n' % (A5, size))
    with open(os.path.join(cdir, 'entries.txt'), 'w') as f:
        for a in sorted(set(entries)):
            f.write('%X\n' % a)
    with open(os.path.join(cdir, 'meta.tsv'), 'w') as f:
        f.write('A5\t0x%X\nJT_BASE\t0x%X\nJT_COUNT\t%d\nSEG_BASE0\t0x%X\nSEG_STRIDE\t0x%X\n'
                % (A5, A5 + jt_off, len(exports), SEG_BASE0, SEG_STRIDE))
    open(os.path.join(cdir, 'dummy.bin'), 'wb').write(b'\x4e\x71')
    print('A5=0x%X globals=[0x%X,0x%X) jt=0x%X entries=%d segments=%d'
          % (A5, A5 - below, A5, A5 + jt_off, len(exports), len(sizes)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
