#!/usr/bin/env python3
"""Which Mac Toolbox/OS traps does an application call, how often, and where first?

  python traps.py <app file>            (MacBinary / AppleDouble / AppleSingle / raw fork)

Output, one line per distinct trap word, sorted:
  A9A0 GetResource          37  CODE3+01F2

This is the work list for the HLE runtime: every word here must be handled by
hle/src (run tools/analysis/trap_audit.py to see which are not yet).

Needs capstone (pip install capstone) to step over instructions correctly, so that
an A-line word that is really an immediate operand is not counted.
"""
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'macres'))
import capstone  # noqa: E402
from rfork import load, resources  # noqa: E402
from trapnames import name  # noqa: E402


def scan(fork):
    """Counter of trap words + {word: (segment, offset) of first use}."""
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_020)
    tr, first = collections.Counter(), {}
    for t, rid, nm, b, a in resources(fork):
        if t != 'CODE' or rid == 0:
            continue
        off = 4
        while off < len(b) - 1:
            w = struct.unpack('>H', b[off:off + 2])[0]
            if 0xA000 <= w <= 0xAFFF:
                tr[w] += 1
                first.setdefault(w, (rid, off))
                off += 2
                continue
            ins = next(md.disasm(b[off:off + 10], off), None)
            off += ins.size if ins else 2
    return tr, first


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    tr, first = scan(load(argv[1]))
    for w in sorted(tr):
        print(f'{w:04X} {name(w):18s} {tr[w]:4d}  CODE{first[w][0]}+{first[w][1]:04X}')
    print(f'# {len(tr)} distinct trap words, {sum(tr.values())} call sites', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
