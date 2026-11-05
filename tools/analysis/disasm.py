#!/usr/bin/env python3
"""Quick 68k disassembler for an app's CODE segments, with Toolbox traps named.

  python disasm.py <app file> <seg> [start_hex] [count]
        listing of CODE <seg> (offsets are CODE-resource offsets: code starts at 4)
  python disasm.py <app file> find <trapword_hex> [before] [after]
        every call site of one trap, with context - the fastest way to learn how the
        app calls a trap (what it pushes = how many bytes the HLE must pop)

Example: python disasm.py Game.bin find A9A0 8 2      (all GetResource calls)

Offsets map to the HLE's runtime addresses as: runtime = segment_handle_ptr + offset
(the HLE logs where each segment landed with HLE_SEGLOG=1). For a whole-program view
with names, use the Ghidra pipeline in tools/ghidra instead.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'macres'))
import capstone  # noqa: E402
from rfork import load, resources  # noqa: E402
from trapnames import name as tname  # noqa: E402


def lines(code, seg):
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_020)
    b = code[seg]
    off, out = 4, []
    while off < len(b) - 1:
        w = struct.unpack('>H', b[off:off + 2])[0]
        if 0xA000 <= w <= 0xAFFF:
            out.append((off, f'_{tname(w)} ({w:04X})' + ('  ; auto-pop' if w & 0x0C00 == 0x0C00 else '')))
            off += 2
            continue
        ins = next(md.disasm(b[off:off + 10], off), None)
        if ins:
            out.append((off, f'{ins.mnemonic} {ins.op_str}'))
            off += ins.size
        else:
            out.append((off, f'dc.w ${w:04X}'))
            off += 2
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    code = {rid: b for t, rid, nm, b, a in resources(load(argv[1])) if t == 'CODE'}
    if argv[2] == 'find':
        w = int(argv[3], 16)
        bef = int(argv[4]) if len(argv) > 4 else 8
        aft = int(argv[5]) if len(argv) > 5 else 2
        for seg in sorted(r for r in code if r):
            L = lines(code, seg)
            for i, (o, s) in enumerate(L):
                if f'({w:04X})' in s:
                    print(f'--- CODE {seg} +{o:04X}')
                    for o2, s2 in L[max(0, i - bef):i + aft + 1]:
                        print(f'   {o2:05X}  {s2}')
    else:
        seg = int(argv[2])
        st = int(argv[3], 16) if len(argv) > 3 else 0
        n = int(argv[4]) if len(argv) > 4 else 1 << 30
        for o, s in lines(code, seg):
            if o >= st and n > 0:
                print(f'{o:05X}  {s}')
                n -= 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
