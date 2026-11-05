"""Convert the emulator's P4 screen dump to PNG (scaled 2x): python pbm2png.py in.pbm out.png"""
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read()
hdr, rest = d.split(b'\n', 2)[:2], d.split(b'\n', 2)[2]
w, h = map(int, d.split(b'\n')[1].split())
rb = (w + 7) // 8; s = 2
rows = []
for y in range(h):
    row = bytearray([0])
    line = rest[y*rb:(y+1)*rb]
    px = bytearray()
    for x in range(w):
        v = 0 if (line[x >> 3] >> (7 - (x & 7))) & 1 else 255
        px += bytes([v]) * s
    for _ in range(s): rows.append(b'\x00' + bytes(px))
raw = b''.join(rows)
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w*s, h*s, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
