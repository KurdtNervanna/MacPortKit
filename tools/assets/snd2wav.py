#!/usr/bin/env python3
"""Convert Mac 'snd ' resources (Sound Manager format 1 and 2) to WAV.

  python snd2wav.py <app file> <outdir> [TYPE]     TYPE defaults to 'snd '

Walks the command list for a soundCmd/bufferCmd (0x8050/0x8051, data-offset flag set),
reads the SoundHeader there and writes PCM WAV. Handles the standard header (encode
0x00, 8-bit unsigned mono) and the extended header (0xFF, 8/16-bit, N channels).
Compressed (MACE, encode 0xFE) sounds are reported, not guessed.

Some games keep sounds in their own resource type, or drive the hardware directly
(Falcon MC fills the Mac Plus SoundBase buffer itself - see docs/07-input-sound-files.md);
then there is nothing to convert, and the HLE must emulate the output path instead.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'macres'))
from rfork import load, resources  # noqa: E402

U16=lambda d,o: struct.unpack(">H",d[o:o+2])[0]
U32=lambda d,o: struct.unpack(">I",d[o:o+4])[0]
def fixed(d,o): return U32(d,o)/65536.0

def sound_header_offset(d):
    fmt=U16(d,0)
    if fmt==1:
        n=U16(d,2); p=4
        for _ in range(n): p+=2+4           # dataType + initOption
        ncmd=U16(d,p); p+=2
        for _ in range(ncmd):
            cmd=U16(d,p); p1=U16(d,p+2); p2=U32(d,p+4); p+=8
            if (cmd & 0x7fff) in (0x50,0x51) and (cmd & 0x8000):  # soundCmd/bufferCmd + dataOffset
                return p2
    elif fmt==2:
        ncmd=U16(d,4); p=6
        for _ in range(ncmd):
            cmd=U16(d,p); p2=U32(d,p+4); p+=8
            if (cmd & 0x7fff) in (0x50,0x51) and (cmd & 0x8000): return p2
    return None

def to_wav(path, pcm, rate, bits, chans):
    byte_rate=rate*chans*bits//8; block=chans*bits//8
    with open(path,"wb") as f:
        f.write(b"RIFF"+struct.pack("<I",36+len(pcm))+b"WAVE")
        f.write(b"fmt "+struct.pack("<IHHIIHH",16,1,chans,rate,byte_rate,block,bits))
        f.write(b"data"+struct.pack("<I",len(pcm))+pcm)

def convert(rid,d,outdir):
    off=sound_header_offset(d)
    if off is None or off+22>len(d): return "no SoundHeader"
    length=U32(d,off+4); rate=int(round(fixed(d,off+8))); enc=d[off+20]
    if enc==0x00:          # standard: 8-bit unsigned mono
        data=off+22; pcm=d[data:data+length]; bits,chans=8,1
    elif enc==0xFF:        # extended
        nchan=U32(d,off); numFrames=U32(d,off+22); sampleSize=U16(d,off+48)
        hdr=64; bits=sampleSize or 8; chans=nchan or 1
        pcm=d[off+hdr: off+hdr + numFrames*chans*bits//8]
        if bits==16:       # Mac 16-bit is big-endian signed; WAV wants little-endian
            pcm=b"".join(struct.pack("<h",struct.unpack(">h",pcm[i:i+2])[0]) for i in range(0,len(pcm)-1,2))
    elif enc==0xFE:        return "compressed (MACE) - skipped"
    else:                  return "unknown encode 0x%02x"%enc
    if not pcm: return "empty"
    to_wav(os.path.join(outdir,"snd_%d.wav"%rid), pcm, rate or 11127, bits, chans)
    return (len(pcm),rate,bits,chans)

def main(argv):
    if len(argv) < 3:
        print(__doc__); return 1
    want = (argv[3] if len(argv) > 3 else 'snd ')
    outdir = argv[2]; os.makedirs(outdir, exist_ok=True)
    snds = [(rid, d) for t, rid, nm, d, a in resources(load(argv[1])) if t == want]
    ok = total = 0; reasons = {}
    for rid, d in snds:
        r = convert(rid, d, outdir)
        if isinstance(r, tuple): ok += 1; total += r[0]
        else: reasons[r] = reasons.get(r, 0) + 1
    print("%r -> WAV: %d / %d converted (%d KB PCM) -> %s" % (want, ok, len(snds), total // 1024, outdir))
    for k, v in reasons.items(): print("  %d x %s" % (v, k))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
