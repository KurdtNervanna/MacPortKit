#!/usr/bin/env python3
"""Recon step 1 for a 68k Mac application: pull its code apart.

  python code_extract.py <app file> <workdir>

<app file> may be MacBinary, AppleDouble, AppleSingle or a raw resource fork.
Writes, under <workdir>:

  segments/CODE_NN.bin        raw bytes of each CODE resource (including the 4-byte
                              near-model segment header, so file offset == CODE offset)
  recon/jumptable.tsv         CODE 0 decoded: jt_index, segment, offset_in_seg
  recon/segmap.md             human-readable segment table + entry points
  recon/manifest.json         machine-readable inventory (used by the ghidra/ tools)
  recon/macsbug_symbols.tsv   procedure names embedded by the compiler, if any
  recon/fingerprint.txt       compiler / runtime hints found in the code

Everything written here is derived from the copyrighted program: keep <workdir>
out of any public repository (see docs/legal.md).
"""
import collections
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rfork import load, resources  # noqa: E402

# --- compiler / runtime fingerprints: (bytes or regex, meaning) ------------------
FINGERPRINTS = [
    (rb'%_Static_Constructor', 'THINK C / Symantec C++ runtime (C++ static ctor table)'),
    (rb'__CplusInit', 'THINK C 5+/Symantec C++ (C++ init)'),
    (rb'THINK C', 'THINK C string'),
    (rb'Symantec', 'Symantec tools string'),
    (rb'Lightspeed', 'Lightspeed C/Pascal string'),
    (rb'THINK Pascal', 'THINK Pascal string'),
    (rb'%_MethTables', 'THINK Pascal / Object Pascal method tables'),
    (rb'%A5Init', 'MPW runtime (%A5Init segment)'),
    (rb'_DataInit', 'MPW C runtime (_DataInit)'),
    (rb'PLStrs', 'MPW PasLib strings'),
    (rb'Metrowerks', 'Metrowerks CodeWarrior string'),
    (rb'__start', 'CodeWarrior/MPW __start entry'),
    (rb'MacApp', 'MacApp framework'),
    (rb'TApplication', 'MacApp / TCL object framework'),
    (rb'CApplication', 'THINK Class Library (TCL)'),
]

IDENT = re.compile(rb'^[A-Za-z_%.$][A-Za-z0-9_%.$:]*$')


def macsbug_names(seg, body):
    """Find MacsBug procedure-name records. Compilers (MPW, THINK, CodeWarrior with
    'MacsBug symbols' on) emit, right after a procedure's final RTS / JMP (A0) / RTD:
        [len|0x80][name...][pad to even][literal-data length.W][literals]
    (or 0x80, len.B, name for names > 31 chars). Each name ENDS a procedure, and the
    next procedure starts after its literal data. Returns [(start, end, name)]."""
    out = []
    start = 4
    i = 4
    n = len(body)
    while i < n - 4:
        w = struct.unpack('>H', body[i:i + 2])[0]
        if w in (0x4E75, 0x4ED0):
            p = i + 2
        elif w == 0x4E74:
            p = i + 4
        else:
            i += 2
            continue
        if p >= n:
            break
        b = body[p]
        name = None
        if 0x81 <= b <= 0x9F:
            ln, q = b - 0x80, p + 1
            name = body[q:q + ln]
            q += ln
        elif b == 0x80 and p + 1 < n:
            ln, q = body[p + 1], p + 2
            name = body[q:q + ln]
            q += ln
        if name and IDENT.match(name):
            if q & 1:
                q += 1
            lit = struct.unpack('>H', body[q:q + 2])[0] if q + 2 <= n else 0
            end = q + 2 + lit
            if end & 1:
                end += 1
            out.append((start, p, name.decode('mac_roman')))
            start = end
            i = end
            continue
        i += 2
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    fork = load(argv[1])
    work = argv[2]
    for d in ('segments', 'recon'):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    res = collections.OrderedDict()
    for t, rid, nm, d, a in resources(fork):
        res.setdefault(t, {})[rid] = (nm, d)
    code = {rid: d for rid, (nm, d) in res.get('CODE', {}).items()}
    if 0 not in code:
        print('No CODE 0 (jump table): not a classic 68k application '
              '(a PowerPC app keeps its code in the data fork; a DA/driver/INIT has no CODE 0).')
        return 2

    c0 = code[0]
    above, below, jtlen, jtoff = struct.unpack('>IIII', c0[:16])
    exports = []
    for i in range(jtlen // 8):
        e = c0[16 + i * 8:24 + i * 8]
        off, push, seg, trap = struct.unpack('>HHHH', e)
        exports.append((i, seg, off, push == 0x3F3C and trap == 0xA9F0))
    per_seg = collections.defaultdict(list)
    for idx, seg, off, ok in exports:
        per_seg[seg].append((idx, off))

    far = []
    with open(os.path.join(work, 'recon', 'jumptable.tsv'), 'w') as f:
        f.write('jt_index\tsegment\toffset_in_seg\twell_formed\n')
        for idx, seg, off, ok in exports:
            f.write(f'{idx}\t{seg}\t0x{off:04x}\t{ok}\n')

    seg_info, syms = [], []
    for rid in sorted(code):
        body = code[rid]
        open(os.path.join(work, 'segments', f'CODE_{rid:02d}.bin'), 'wb').write(body)
        hdr = None
        if rid and len(body) >= 4:
            first, cnt = struct.unpack('>HH', body[:4])
            if first == 0xFFFF:
                far.append(rid)
            hdr = {'first_jt_offset': first, 'jt_entry_count': cnt, 'code_bytes': len(body) - 4}
            for s, e, name in macsbug_names(rid, body):
                syms.append((rid, s, e, name))
        seg_info.append({'segment': rid, 'name': res['CODE'][rid][0], 'size': len(body),
                         'near_header': hdr, 'num_entry_points': len(per_seg.get(rid, []))})

    with open(os.path.join(work, 'recon', 'macsbug_symbols.tsv'), 'w') as f:
        f.write('segment\tstart\tend\tname\n')
        for rid, s, e, name in syms:
            f.write(f'{rid}\t0x{s:04x}\t0x{e:04x}\t{name}\n')

    allcode = b''.join(code[r] for r in sorted(code) if r)
    hints = [meaning for pat, meaning in FINGERPRINTS if pat in allcode]
    if syms:
        hints.append(f'{len(syms)} MacsBug procedure names embedded (compiled with symbols on)')
    if far:
        hints.append(f'FAR-model segments (header 0xFFFF): {far} - the near-model loader in hle/ '
                     'does not handle these yet (see docs/02-recon.md)')
    links = sum(1 for i in range(0, len(allcode) - 1, 2) if allcode[i:i + 2] == b'\x4e\x56')
    hints.append(f'{links} LINK A6 instructions (compiled C/Pascal functions with frames)')
    with open(os.path.join(work, 'recon', 'fingerprint.txt'), 'w') as f:
        f.write('\n'.join(hints) + '\n')

    with open(os.path.join(work, 'recon', 'segmap.md'), 'w') as f:
        f.write('# Segment map\n\n')
        f.write(f'Jump table: {len(exports)} entries; above A5 = 0x{above:x}; '
                f'below A5 (app globals) = 0x{below:x} ({below:,} bytes); '
                f'jump table at A5+0x{jtoff:x}\n\n')
        f.write('| Segment | Name | Size | Code bytes | JT entries |\n|--:|---|--:|--:|--:|\n')
        for s in seg_info:
            cb = s['near_header']['code_bytes'] if s['near_header'] else '-'
            f.write(f"| CODE {s['segment']} | {s['name']} | {s['size']:,} | {cb} | {s['num_entry_points']} |\n")
        f.write('\n## Entry points per segment (jt_index @ offset)\n\n')
        for seg in sorted(per_seg):
            f.write(f'- **CODE {seg}**: ' + ', '.join(f'#{i}@0x{o:04x}' for i, o in per_seg[seg]) + '\n')

    manifest = {
        'jump_table': {'entries': len(exports), 'above_a5': above,
                       'below_a5_globals': below, 'jt_offset_from_a5': jtoff},
        'code_segments': seg_info,
        'resource_types': {t: {'count': len(d), 'bytes': sum(len(x[1]) for x in d.values())}
                           for t, d in res.items()},
        'fingerprint': hints,
    }
    json.dump(manifest, open(os.path.join(work, 'recon', 'manifest.json'), 'w'), indent=2)

    print(f'{len(code) - 1} CODE segments, {len(exports)} jump-table entries, '
          f'{below:,} bytes of A5 globals')
    for h in hints:
        print('  -', h)
    print(f'wrote {work}/segments/ and {work}/recon/')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
