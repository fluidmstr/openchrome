"""RP6L container reader. Spec: docs/formats/rpack.md. Usage: rpack.py <file.rpack> [outdir]"""
import struct, sys, zlib, os

MAGIC = 0x4c365052  # 'RP6L'

def open_rpack(path):
    with open(path, 'rb') as f:
        h = struct.unpack('<8I', f.read(32))
        if h[0] != MAGIC:
            raise ValueError('not an RP6L file')
        table = [struct.unpack('<5I', f.read(20)) for _ in range(h[4])]
        streams = []
        for _, flags, off, usize, csize in table:
            f.seek(off)
            # csize == 0 means stored raw
            streams.append(zlib.decompress(f.read(csize)) if csize else f.read(usize))
    return h, table, streams

if __name__ == '__main__':
    h, table, streams = open_rpack(sys.argv[1])
    print('header', [hex(x) for x in h])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    if out:
        os.makedirs(out, exist_ok=True)
    for i, (t, s) in enumerate(zip(table, streams)):
        print(f'stream {i}: flags={t[1]:#x} x={t[0]:#x} usize={len(s)}')
        if out:
            open(os.path.join(out, f'stream{i}_{t[1]:08x}.bin'), 'wb').write(s)
