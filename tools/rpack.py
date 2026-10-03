"""RP6L container reader. Spec: docs/formats/rpack.md.
Usage: rpack.py ls <pack>            list resources
       rpack.py x <pack> <outdir>    extract every resource part as <outdir>/<res>/<stream>.bin
"""
import mmap, os, struct, sys, zlib
from collections import namedtuple

MAGIC = 0x4c365052  # 'RP6L'
Res = namedtuple('Res', 'index name flags chunks')  # chunks: {stream_idx: (offset, size)}

CACHE = os.environ.get('RPACK_CACHE', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out', 'cache'))

class _Streams:
    """Inflates a stream on first use and keeps it in a cache file (big packs are GBs of one zlib stream)."""
    def __init__(self, path, table):
        self.path, self.table = path, table
        self._mm = {}
    def __len__(self): return len(self.table)
    def __getitem__(self, i):
        if i not in self._mm:
            _, flags, off, usize, csize = self.table[i]
            st = os.stat(self.path)
            cf = os.path.join(CACHE, f'{os.path.basename(self.path)}.{st.st_size}.{i}')
            if not os.path.exists(cf) or os.path.getsize(cf) != usize:
                os.makedirs(CACHE, exist_ok=True)
                with open(self.path, 'rb') as f, open(cf + '.tmp', 'wb') as o:
                    f.seek(off)
                    if csize:
                        z, left = zlib.decompressobj(), csize
                        while left:
                            b = f.read(min(1 << 22, left)); left -= len(b); o.write(z.decompress(b))
                        o.write(z.flush())
                    else:
                        o.write(f.read(usize))
                os.replace(cf + '.tmp', cf)
            f = open(cf, 'rb')
            self._mm[i] = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) if usize else b''
        return self._mm[i]

class Pack:
    def __init__(self, path):
        with open(path, 'rb') as f:
            h = struct.unpack('<8I', f.read(32))
            if h[0] != MAGIC:
                raise ValueError('not an RP6L file')
            self.header = h
            nrec, nstr, nres, strsz = h[3], h[4], h[5], h[6]
            # tables sit between the header and the first stream payload; read only those
            f.seek(32)
            table = [struct.unpack('<5I', f.read(20)) for _ in range(nstr)]
            raw = f.read(16 * nrec + 12 * nres + 4 * (nres + 1) + strsz)
        self.roles = [t[1] & 0xff for t in table]  # 0x10/0x11/0x12 meta, 0xf0 vertices, 0xf1 indices, 0xff names
        self.streams = _Streams(path, table)
        p = 0
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
        return {s: bytes(self.streams[s & 0xff][o:o + n]) for s, (o, n) in res.chunks.items()}

    def by_role(self, res):
        return {self.roles[s & 0xff]: v for s, v in self.data(res).items()}

    def find(self, name, flags=None):
        return next((r for r in self.resources if r.name == name and (flags is None or r.flags == flags)), None)

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
