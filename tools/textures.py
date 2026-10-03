"""Export RP6L texture resources (type 0x2120000x) as DDS. Spec: docs/formats/texture.md.
Usage: textures.py <pack> <outdir> [name-substring]"""
import struct, sys, os
from rpack import Pack

# game format id -> (DXGI format, bits/texel, block size) ; ids 18/19/33 are guesses from byte counts
FMT = {2: (28, 32, 1), 14: (61, 8, 1), 17: (71, 4, 4), 18: (74, 8, 4), 19: (77, 8, 4), 33: (2, 128, 1)}

def mip_chain_size(w, h, mips, bpt, blk):
    n = 0
    for _ in range(mips):
        bw, bh = max(1, -(-w // blk)), max(1, -(-h // blk))
        n += bw * bh * (bpt * blk * blk // 8)
        w, h = max(1, w >> 1), max(1, h >> 1)
    return n

def to_dds(hdr, pixels):
    w, h, _, faces, mips, _, fmt = struct.unpack_from('<6HI', hdr)
    dxgi = FMT[fmt][0]
    caps2 = 0xFE00 | 0x200 if faces == 6 else 0
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x20000 if mips > 1 else 0)
    pf = struct.pack('<8I', 32, 4, 0x30315844, 0, 0, 0, 0, 0)  # 'DX10'
    head = b'DDS ' + struct.pack('<7I', 124, flags, h, w, 0, 0, mips) + b'\0' * 44 + pf \
        + struct.pack('<5I', 0x1000 | 0x8 | 0x400000, caps2, 0, 0, 0)
    return head + struct.pack('<5I', dxgi, 3, 0x4 if faces == 6 else 0, 1, 0) + pixels

def export(pk, r):
    d = pk.data(r)
    hk = [k for k, v in d.items() if len(v) == 151][0]
    hdr = d.pop(hk)
    w, h, _, faces, mips, _, fmt = struct.unpack_from('<6HI', hdr)
    _, bpt, blk = FMT[fmt]
    want = mip_chain_size(w, h, mips, bpt, blk) * faces
    # a texture may carry a second, smaller chunk (bit 8 set); take the one matching the full chain
    for k, v in d.items():
        if len(v) == want:
            return to_dds(hdr, v)
    raise ValueError(f'{r.name}: no chunk of {want} bytes in {[len(v) for v in d.values()]}')

if __name__ == '__main__':
    pk = Pack(sys.argv[1]); os.makedirs(sys.argv[2], exist_ok=True)
    pat = sys.argv[3] if len(sys.argv) > 3 else ''
    ok = bad = 0
    for r in pk.resources:
        if r.flags in (0x21200002, 0x21200003) and pat in r.name:
            try:
                open(os.path.join(sys.argv[2], r.name.replace('/', '_').replace('#', '_') + '.dds'), 'wb').write(export(pk, r)); ok += 1
            except Exception as e:
                bad += 1; print('skip', e)
    print(ok, 'ok', bad, 'skipped')
