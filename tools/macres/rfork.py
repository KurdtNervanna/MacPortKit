#!/usr/bin/env python3
"""Classic Mac resource-fork helper (stdlib only).

Modern file systems don't keep a Mac file's *resource fork*, so classic software
reaches you wrapped in one of these containers. This module unwraps all of them:

  * MacBinary I/II/III  (.bin)        - 128-byte header, data fork, resource fork
  * AppleDouble         (._name, name.rsrc from `unar -k visible`) - magic 00051607
  * AppleSingle                                                    - magic 00051600
  * a raw resource fork (e.g. extracted by `hcopy -r`, or from a previous `extract`)

Commands
  python rfork.py info    <file>                 container type, fork sizes, type summary
  python rfork.py list    <file> [TYPE ...]      every resource: type, id, size, name
  python rfork.py extract <file> <out.rsrc>      write the raw resource fork
  python rfork.py data    <file> <out.bin>       write the data fork (MacBinary/AppleSingle)
  python rfork.py dump    <file> <outdir> [TYPE ...]
                                                 one file per resource: <outdir>/<TYPE>/<id>[_name].bin

Library use
  from rfork import load, resources
  fork = load(path)                  # raw resource-fork bytes from any container
  for rtype, rid, name, data, attrs in resources(fork): ...

TYPE arguments are four-character codes; pad with spaces where needed, e.g. 'snd '.
"""
import os
import struct
import sys

APPLEDOUBLE = b'\x00\x05\x16\x07'
APPLESINGLE = b'\x00\x05\x16\x00'


def _entries(b):
    n = struct.unpack('>H', b[24:26])[0]
    for i in range(n):
        eid, off, ln = struct.unpack('>III', b[26 + i * 12:38 + i * 12])
        yield eid, off, ln


def is_macbinary(b):
    # MacBinary I/II/III: byte 0 = 0, filename length 1..63, bytes 74 and 82 = 0,
    # and the fork lengths must fit inside the file.
    if len(b) < 128 or b[0] != 0 or not (0 < b[1] < 64) or b[74] != 0 or b[82] != 0:
        return False
    dlen, rlen = struct.unpack('>II', b[83:91])
    return 128 + dlen + rlen <= len(b) + 256


def container(b):
    if b[:4] == APPLEDOUBLE:
        return 'AppleDouble'
    if b[:4] == APPLESINGLE:
        return 'AppleSingle'
    if is_macbinary(b):
        return 'MacBinary'
    if looks_like_fork(b):
        return 'raw resource fork'
    return 'unknown (no resource fork found)'


def looks_like_fork(b):
    if len(b) < 256:
        return False
    doff, moff, dlen, mlen = struct.unpack('>IIII', b[:16])
    return doff >= 16 and moff >= doff and moff + mlen <= len(b) and mlen >= 28


def raw_fork(b):
    """Resource fork bytes from any supported container (raw input is returned as is)."""
    if b[:4] in (APPLEDOUBLE, APPLESINGLE):
        for eid, off, ln in _entries(b):
            if eid == 2:
                return b[off:off + ln]
        raise ValueError('AppleDouble/AppleSingle file without a resource fork')
    if is_macbinary(b):
        dlen, rlen = struct.unpack('>II', b[83:91])
        rs = 128 + (dlen + 127) // 128 * 128
        return b[rs:rs + rlen]
    return b


def data_fork(b):
    if b[:4] == APPLESINGLE:
        for eid, off, ln in _entries(b):
            if eid == 1:
                return b[off:off + ln]
        return b''
    if is_macbinary(b):
        dlen = struct.unpack('>I', b[83:87])[0]
        return b[128:128 + dlen]
    raise ValueError('only MacBinary and AppleSingle carry the data fork in the same file')


def finder_info(b):
    """(type, creator) as strings when the container records them, else None."""
    if is_macbinary(b):
        return b[65:69].decode('mac_roman'), b[69:73].decode('mac_roman')
    if b[:4] in (APPLEDOUBLE, APPLESINGLE):
        for eid, off, ln in _entries(b):
            if eid == 9 and ln >= 8:
                return b[off:off + 4].decode('mac_roman'), b[off + 4:off + 8].decode('mac_roman')
    return None


def load(path):
    return raw_fork(open(path, 'rb').read())


def resources(r):
    """Yield (type, id, name, data, attrs) for every resource in a raw fork."""
    doff, moff, dlen, mlen = struct.unpack('>IIII', r[:16])
    m = r[moff:moff + mlen]
    tlo, nlo = struct.unpack('>HH', m[24:28])
    nt = (struct.unpack('>H', m[tlo:tlo + 2])[0] + 1) & 0xFFFF
    for i in range(nt):
        t = m[tlo + 2 + i * 8:tlo + 6 + i * 8].decode('mac_roman')
        cnt, rlo = struct.unpack('>HH', m[tlo + 6 + i * 8:tlo + 10 + i * 8])
        for j in range(cnt + 1):
            e = m[tlo + rlo + j * 12:tlo + rlo + j * 12 + 12]
            rid, nmo = struct.unpack('>hH', e[:4])
            attrs = e[4]
            ro = int.from_bytes(e[5:8], 'big')
            ln = struct.unpack('>I', r[doff + ro:doff + ro + 4])[0]
            nm = '' if nmo == 0xFFFF else m[nlo + nmo + 1:nlo + nmo + 1 + m[nlo + nmo]].decode('mac_roman')
            yield t, rid, nm, r[doff + ro + 4:doff + ro + 4 + ln], attrs


def by_type(r, rtype):
    """{id: data} for one resource type."""
    return {rid: d for t, rid, nm, d, a in resources(r) if t == rtype}


def _safe(s):
    return ''.join(c if c.isalnum() or c in '-_.' else '_' for c in s)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, path = argv[1], argv[2]
    whole = open(path, 'rb').read()
    if cmd == 'info':
        print(f'{path}: {container(whole)}, {len(whole)} bytes')
        fi = finder_info(whole)
        if fi:
            print(f'  Finder type/creator: {fi[0]!r} / {fi[1]!r}')
        fork = raw_fork(whole)
        if not looks_like_fork(fork):
            print('  no resource fork')
            return 0
        counts = {}
        for t, rid, nm, d, a in resources(fork):
            c = counts.setdefault(t, [0, 0])
            c[0] += 1
            c[1] += len(d)
        print(f'  resource fork: {len(fork)} bytes, {sum(c[0] for c in counts.values())} resources')
        for t in sorted(counts):
            print(f'    {t!r:8s} {counts[t][0]:5d} x  {counts[t][1]:9d} bytes')
    elif cmd == 'list':
        want = set(argv[3:])
        for t, rid, nm, d, a in resources(raw_fork(whole)):
            if not want or t in want:
                print(f'{t!r} {rid:6d} {len(d):8d}  attrs={a:02X}  {nm}')
    elif cmd == 'extract':
        fork = raw_fork(whole)
        open(argv[3], 'wb').write(fork)
        print(f'{argv[3]}: {len(fork)} bytes')
    elif cmd == 'data':
        d = data_fork(whole)
        open(argv[3], 'wb').write(d)
        print(f'{argv[3]}: {len(d)} bytes')
    elif cmd == 'dump':
        out, want, n = argv[3], set(argv[4:]), 0
        for t, rid, nm, d, a in resources(raw_fork(whole)):
            if want and t not in want:
                continue
            td = os.path.join(out, _safe(t))
            os.makedirs(td, exist_ok=True)
            fn = f'{rid}' + (f'_{_safe(nm)}' if nm else '') + '.bin'
            open(os.path.join(td, fn), 'wb').write(d)
            n += 1
        print(f'dumped {n} resources to {out}')
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
