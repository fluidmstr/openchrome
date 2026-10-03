"""RP6L container reader. Spec: docs/formats/rpack.md.
Usage: rpack.py ls <pack>            list resources
       rpack.py x <pack> <outdir>    extract every resource part as <outdir>/<res>/<stream>.bin
"""
import struct, sys, zlib, os
from collections import namedtuple

MAGIC = 0x4c365052  # 'RP6L'
Res = namedtuple('Res', 'index name flags chunks')  # chunks: {stream_idx: (offset, size)}

class Pack:
    def __init__(self, path):
        with open(path, 'rb') as f:
            raw = f.read()
        h = struct.unpack_from('<8I', raw)
        if h[0] != MAGIC:
            raise ValueError('not an RP6L file')
        self.header = h
        nrec, nstr, nres, strsz = h[3], h[4], h[5], h[6]
        p = 32
        self.streams = []
        for i in range(nstr):
            _, flags, off, usize, csize = struct.unpack_from('<5I', raw, p + 20 * i)
            # csize == 0: stored raw
            self.streams.append(zlib.decompress(raw[off:off + csize]) if csize else raw[off:off + usize])
        p += 20 * nstr
        rows = [struct.unpack_from('<4I', raw, p + 16 * i) for i in range(nrec)]
        p += 16 * nrec
        dirs = [struct.unpack_from('<3I', raw, p + 12 * i) for i in range(nres)]
        p += 12 * nres
        offs = struct.unpack_from('<%dI' % (nres + 1), raw, p)
        p += 4 * (nres + 1)
        strs = raw[p:p + strsz]
        chunks = {}
        for _, rid, off, size in rows:
            chunks.setdefault(rid >> 16, {})[rid & 0xffff] = (off, size)
        # offs[0] is a pack-level name, resource i is offs[i+1]
        def name(i):
            s = offs[i + 1]
            return strs[s:strs.index(b'\0', s)].decode('latin1')
        self.resources = [Res(i, name(i), dirs[i][1], chunks.get(i, {})) for i in range(nres)]

    def data(self, res):
        # bit 8 of the part id is set on some chunks (meaning unknown); stream index is the low byte
        return {s: self.streams[s & 0xff][o:o + n] for s, (o, n) in res.chunks.items()}

if __name__ == '__main__':
    pk = Pack(sys.argv[2])
    if sys.argv[1] == 'ls':
        for r in pk.resources:
            print(f'{r.index:5} {r.flags:08x} {r.name:40} ' + ' '.join(f'{s}:{n}' for s, (o, n) in sorted(r.chunks.items())))
    elif sys.argv[1] == 'x':
        for r in pk.resources:
            d = os.path.join(sys.argv[3], f'{r.index}_{r.name}'.replace('/', '_'))
            os.makedirs(d, exist_ok=True)
            for s, b in pk.data(r).items():
                open(os.path.join(d, f'{s}.bin'), 'wb').write(b)
