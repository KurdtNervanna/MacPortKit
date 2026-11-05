#!/usr/bin/env python3
"""Convert QuickDraw PICT resources to PNG (stdlib only, no PIL).

  python pict2png.py <app file> <outdir> [id ...]     PICT resources of an app
  python pict2png.py --file <pict.bin> <out.png>      one raw PICT (e.g. from rfork.py dump)

Handles PICT version 1 (1-byte opcodes, black & white) and version 2 (2-byte, word
aligned): BitsRect/BitsRgn/PackBitsRect/PackBitsRgn with BitMaps (1-bit) or indexed
PixMaps (1/2/4/8-bit + embedded colour table), and DirectBitsRect/Rgn (16/32-bit).
Every image opcode is composited into a canvas the size of picFrame. Vector opcodes
(lines, rects, text, ovals...) are SKIPPED with their correct lengths, so a picture
that is mostly vectors comes out partly blank - the report lists how many were
skipped. For those, render with the HLE's DrawPicture instead.

This is the same decoder logic the HLE needs for _DrawPicture; port it to C when you
get there (both Falcon MC's and ATC's runtimes did).
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'macres'))

U16 = lambda d, o: struct.unpack('>H', d[o:o + 2])[0]  # noqa: E731
S16 = lambda d, o: struct.unpack('>h', d[o:o + 2])[0]  # noqa: E731
U32 = lambda d, o: struct.unpack('>I', d[o:o + 4])[0]  # noqa: E731

# Mac default 1-bit: 0 = white, 1 = black
BW = [(255, 255, 255), (0, 0, 0)]


def unpackbits(d, p, n):
    out = bytearray()
    while len(out) < n and p < len(d):
        c = d[p]
        p += 1
        if c < 128:
            out += d[p:p + c + 1]
            p += c + 1
        elif c > 128:
            out += bytes([d[p]]) * (257 - c)
            p += 1
    return bytes(out[:n]) + bytes(max(0, n - len(out)))


def unpackwords(d, p, nwords):
    """PackBits over 16-bit units (packType 3, 16-bit direct pixels)."""
    out = bytearray()
    while len(out) < nwords * 2 and p < len(d):
        c = d[p]
        p += 1
        if c < 128:
            out += d[p:p + (c + 1) * 2]
            p += (c + 1) * 2
        elif c > 128:
            out += d[p:p + 2] * (257 - c)
            p += 2
    return bytes(out[:nwords * 2])


def read_rows(d, p, rb, h, packed):
    rows = []
    for _ in range(h):
        if not packed or rb < 8:
            rows.append(d[p:p + rb])
            p += rb
        else:
            if rb > 250:
                cnt = U16(d, p)
                p += 2
            else:
                cnt = d[p]
                p += 1
            rows.append(unpackbits(d, p, rb))
            p += cnt
    return rows, p


def read_ctab(d, p):
    seed, flags, n = struct.unpack('>IHh', d[p:p + 8])
    p += 8
    pal = [(0, 0, 0)] * 256
    for i in range(n + 1):
        val, r, g, b = struct.unpack('>HHHH', d[p:p + 8])
        p += 8
        idx = i if flags & 0x8000 else val      # device ctab: index = position
        if 0 <= idx < 256:
            pal[idx] = (r >> 8, g >> 8, b >> 8)
    return pal, p


def expand(row, ps, w):
    if ps == 8:
        return list(row[:w])
    out = []
    per = 8 // ps
    mask = (1 << ps) - 1
    for byte in row:
        for k in range(per):
            out.append((byte >> (8 - ps * (k + 1))) & mask)
    return out[:w]


def bits_op(d, p, op, v2):
    """Decode an image opcode. Returns (dst_rect, pixel rows as RGB tuples, mode, new p)."""
    direct = op in (0x9A, 0x9B)
    rgn = op in (0x91, 0x99, 0x9B)
    packed = op in (0x98, 0x99, 0x9A, 0x9B)
    if direct:
        p += 4                                         # baseAddr
    rbw = U16(d, p)
    is_pixmap = bool(rbw & 0x8000) or direct
    rb = rbw & 0x3FFF
    t, l, b, r = struct.unpack('>hhhh', d[p + 2:p + 10])
    w, h = r - l, b - t
    p += 10
    if is_pixmap:
        pack_type = S16(d, p + 2)
        ps = S16(d, p + 18)
        cmp_count = S16(d, p + 20)
        p += 36                                        # rest of the PixMap record
        pal = None
        if not direct:
            pal, p = read_ctab(d, p)
    else:
        ps, pal, pack_type, cmp_count = 1, BW, 0, 1
    src = struct.unpack('>hhhh', d[p:p + 8])
    dst = struct.unpack('>hhhh', d[p + 8:p + 16])
    mode = U16(d, p + 16)
    p += 18
    if rgn:
        p += U16(d, p)
    px = []
    if direct and ps == 32:
        for _ in range(h):
            if pack_type in (0, 4) and rb >= 8:
                cnt = U16(d, p) if rb > 250 else d[p]
                p += 2 if rb > 250 else 1
                ncmp = cmp_count if cmp_count in (3, 4) else 3
                raw = unpackbits(d, p, w * ncmp)
                p += cnt
                off = w * (ncmp - 3)                     # skip alpha plane
                px.append([(raw[off + x], raw[off + w + x], raw[off + 2 * w + x]) for x in range(w)])
            else:
                step = 3 if pack_type == 2 else 4
                raw = d[p:p + w * step]
                p += w * step
                o = 0 if step == 3 else 1
                px.append([(raw[x * step + o], raw[x * step + o + 1], raw[x * step + o + 2]) for x in range(w)])
    elif direct and ps == 16:
        for _ in range(h):
            if pack_type == 3 and rb >= 8:
                cnt = U16(d, p) if rb > 250 else d[p]
                p += 2 if rb > 250 else 1
                raw = unpackwords(d, p, w)
                p += cnt
            else:
                raw = d[p:p + rb]
                p += rb
            row = []
            for x in range(w):
                v = U16(raw, x * 2) if x * 2 + 2 <= len(raw) else 0
                row.append((((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31))
            px.append(row)
    else:
        rows, p = read_rows(d, p, rb, h, packed)
        for row in rows:
            px.append([pal[i] if i < len(pal) else (0, 0, 0) for i in expand(row, ps, w)])
    if v2 and p & 1:
        p += 1
    return dst, src, px, mode, p


def skip(d, p, op, v2):
    """Length of the data that follows a non-image opcode."""
    def wl():
        return U16(d, p)
    fixed = {0x00: 0, 0x02: 8, 0x03: 2, 0x04: 1, 0x05: 2, 0x06: 4, 0x07: 4, 0x08: 2, 0x09: 8,
             0x0A: 8, 0x0B: 4, 0x0C: 4, 0x0D: 2, 0x0E: 4, 0x0F: 4, 0x10: 8, 0x15: 2, 0x16: 2,
             0x17: 0, 0x18: 0, 0x19: 0, 0x1A: 6, 0x1B: 6, 0x1C: 0, 0x1D: 6, 0x1E: 0, 0x1F: 6,
             0x20: 8, 0x21: 4, 0x22: 6, 0x23: 2, 0xA0: 2}
    if op == 0x11:
        return p + (2 if v2 else 1)
    if op == 0x0C00:
        return p + 24
    if op in fixed:
        return p + fixed[op]
    if op in (0x01,) or 0x70 <= op <= 0x77 or 0x80 <= op <= 0x87:
        return p + wl()                                # region / polygon: size includes itself
    if 0x30 <= op <= 0x37 or 0x40 <= op <= 0x47 or 0x50 <= op <= 0x57:
        return p + 8
    if 0x38 <= op <= 0x3F or 0x48 <= op <= 0x4F or 0x58 <= op <= 0x5F:
        return p                                       # "same rect" variants: no data
    if 0x60 <= op <= 0x67:
        return p + 12
    if 0x68 <= op <= 0x6F:
        return p + 4
    if 0x78 <= op <= 0x7F or 0x88 <= op <= 0x8F:
        return p
    if op == 0x28:
        return p + 5 + d[p + 4]
    if op in (0x29, 0x2A):
        return p + 2 + d[p + 1]
    if op == 0x2B:
        return p + 3 + d[p + 2]
    if 0x24 <= op <= 0x27 or 0x2C <= op <= 0x2F or 0x9C <= op <= 0x9F or 0xA2 <= op <= 0xAF:
        return p + 2 + wl()
    if op == 0xA1:
        return p + 4 + U16(d, p + 2)
    if 0xB0 <= op <= 0xCF:
        return p
    if 0xD0 <= op <= 0xFE:
        return p + 4 + U32(d, p)
    if 0x12 <= op <= 0x14:                             # Bk/Pn/FillPixPat
        ptype = U16(d, p)
        q = p + 10
        if ptype == 2:
            return q + 6
        rbw = U16(d, q)
        rb = rbw & 0x3FFF
        t, l, b, r = struct.unpack('>hhhh', d[q + 2:q + 10])
        q += 46
        _, q = read_ctab(d, q)
        _, q = read_rows(d, q, rb, b - t, True)
        return q
    if 0x100 <= op <= 0x7FFF:
        return p + (op >> 8) * 2
    if 0x8000 <= op <= 0x80FF:
        return p
    if op >= 0x8100:
        return p + 4 + U32(d, p)
    raise ValueError(f'unhandled opcode {op:#06x} at {p - 2}')


IMAGE_OPS = {0x90, 0x91, 0x98, 0x99, 0x9A, 0x9B}
VECTOR_OPS = set(range(0x20, 0x24)) | set(range(0x28, 0x2C)) | set(range(0x30, 0x90))


def decode(d):
    t, l, b, r = struct.unpack('>hhhh', d[2:10])
    W, H = max(1, r - l), max(1, b - t)
    canvas = [[(255, 255, 255)] * W for _ in range(H)]
    p = 10
    v2 = d[10:12] == b'\x00\x11' and d[12:14] == b'\x02\xff'
    images = vectors = 0
    while p < len(d):
        if v2:
            if p & 1:
                p += 1
            op = U16(d, p)
            p += 2
        else:
            op = d[p]
            p += 1
        if op == 0xFF or (v2 and op == 0x00FF):
            break
        if op in IMAGE_OPS:
            dst, src, px, mode, p = bits_op(d, p, op, v2)
            images += 1
            dt, dl, db, dr = dst
            st, sl, sb, sr = src
            sh, sw = max(1, sb - st), max(1, sr - sl)
            # srcRect is in the bitmap's own coordinates; px row 0 = its bounds.top,
            # which for PICT image ops is (nearly always) srcRect.top. Scale src->dst.
            for y in range(max(0, dt - t), min(H, db - t)):
                ry = (y + t - dt) * sh // max(1, db - dt)
                if ry >= len(px):
                    continue
                row = px[ry]
                for x in range(max(0, dl - l), min(W, dr - l)):
                    rx = (x + l - dl) * sw // max(1, dr - dl)
                    if rx < len(row):
                        c = row[rx]
                        if (mode & 0x3F) == 1 and c == (255, 255, 255):
                            continue                       # srcOr: white is transparent
                        canvas[y][x] = c
            continue
        if op in VECTOR_OPS:
            vectors += 1
        p = skip(d, p, op, v2)
    return W, H, canvas, images, vectors, (2 if v2 else 1)


def write_png(path, w, h, rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for c in row:
            raw += bytes(c)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def main(argv):
    if len(argv) >= 4 and argv[1] == '--file':
        W, H, c, ni, nv, ver = decode(open(argv[2], 'rb').read())
        write_png(argv[3], W, H, c)
        print(f'{argv[3]}: {W}x{H} v{ver}, {ni} image ops, {nv} vector ops skipped')
        return 0
    if len(argv) < 3:
        print(__doc__)
        return 1
    from rfork import load, by_type
    picts = by_type(load(argv[1]), 'PICT')
    want = {int(x) for x in argv[3:]}
    os.makedirs(argv[2], exist_ok=True)
    ok = 0
    for rid in sorted(picts):
        if want and rid not in want:
            continue
        try:
            W, H, c, ni, nv, ver = decode(picts[rid])
            write_png(os.path.join(argv[2], f'PICT_{rid}.png'), W, H, c)
            ok += 1
            note = f'  ({nv} vector ops not drawn)' if nv else ''
            print(f'PICT {rid}: {W}x{H} v{ver} images={ni}{note}')
        except Exception as e:  # keep going; report
            print(f'PICT {rid}: FAILED {e}')
    print(f'{ok}/{len(picts) if not want else len(want)} converted -> {argv[2]}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
