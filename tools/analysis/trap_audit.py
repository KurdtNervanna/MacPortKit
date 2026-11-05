#!/usr/bin/env python3
"""Trap-coverage audit: which traps does the app call that the HLE does not handle?

  python trap_audit.py <app file> [hle/src dir]

Statically scans the app's CODE for A-line words (like traps.py) and the HLE source
for the trap words it implements, and prints the difference. Run it BEFORE the first
boot (to size the work) and again whenever behaviour is odd: an unhandled trap is the
single most common cause of silent breakage, because the fallback path returns
without popping the caller's Pascal arguments -> the 68k stack is skewed and the
game misbehaves much later, far from the cause.

How "implemented" is detected (heuristic, by design simple):
  * Toolbox traps (A800-AFFF): any `case 0xA8xx:` .. `case 0xABxx:` in the C source.
    Lookup uses word & ~0x0400 (the auto-pop bit), as the dispatcher does.
  * OS traps (A000-A7FF): two-digit `case 0xNN:` labels in trap.c and fs.c
    (dispatch is on word & 0xFF).
  * Selector-dispatched packages (_Pack0.._Pack15, _HFSDispatch A060, _Gestalt A1AD,
    _QDExtensions AB1D, _SoundDispatch A800, _PrGlue A8FD ...) are listed as "dispatcher":
    the trap may be handled while a particular SELECTOR is not. Check those with
    disasm.py find <word> and look at what the app pushes as the selector.

Static scanning cannot see traps reached through GetTrapAddress + JSR, or traps
inside code resources other than CODE (WDEF/CDEF/MDEF/LDEF, 'Jnth'...). The runtime
log line "[trap] unimplemented XXXX at PC" is the authoritative second opinion.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'macres'))
from rfork import load  # noqa: E402
from traps import scan  # noqa: E402
from trapnames import name  # noqa: E402

DISPATCHERS = {0xA9E7, 0xA9E8, 0xA9E9, 0xA9EA, 0xA9EB, 0xA9EC, 0xA9ED, 0xA9EE, 0xA9EF,
               0xA82F, 0xA9E5, 0xA9E6, 0xA060, 0xA1AD, 0xA0AD, 0xAB1D, 0xA800, 0xA8FD,
               0xA9F3, 0xA89F}


def implemented(src):
    tb, os_ = set(), set()
    for f in glob.glob(os.path.join(src, '*.c')):
        txt = open(f, encoding='latin-1').read()
        for m in re.finditer(r'case\s+0x(A[89AB][0-9A-Fa-f]{2})\b', txt):
            tb.add(int(m.group(1), 16) & 0xFBFF)
        if os.path.basename(f) in ('trap.c', 'fs.c'):
            for m in re.finditer(r'case\s+0x([0-9A-Fa-f]{2})\s*:', txt):
                os_.add(int(m.group(1), 16))
    return tb, os_


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    src = argv[2] if len(argv) > 2 else os.path.join(HERE, '..', '..', 'hle', 'src')
    tr, first = scan(load(argv[1]))
    tb, os_ = implemented(src)
    missing, disp, unknown = [], [], []
    for w in sorted(tr):
        base = (w & 0xFBFF) if w >= 0xA800 else (0xA000 | (w & 0xFF))
        ok = base in tb if w >= 0xA800 else (w & 0xFF) in os_
        if base in DISPATCHERS:
            disp.append(w)
        elif not ok and name(w) == '?':
            unknown.append(w)
        elif not ok:
            missing.append(w)
    print(f'{len(tr)} distinct trap words used; {len(missing)} not found in {os.path.normpath(src)}\n')
    if missing:
        print('NOT IMPLEMENTED (first call site):')
        for w in missing:
            seg, off = first[w]
            print(f'  {w:04X} {name(w):20s} x{tr[w]:<4d} CODE{seg}+{off:04X}')
    if unknown:
        print('\nUNKNOWN WORDS (no name in trapnames.py). Often DATA inside code (tables,')
        print('constants) - confirm with disasm.py before implementing anything:')
        print('  ' + ' '.join(f'{w:04X}' for w in unknown))
    if disp:
        print('\nSELECTOR DISPATCHERS (check each selector the app uses):')
        for w in disp:
            print(f'  {w:04X} {name(w):20s} x{tr[w]}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
