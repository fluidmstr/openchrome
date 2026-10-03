"""ABDM .mp container reader (optimized_dx11.mp). Spec: docs/formats/mp.md."""
import struct

class MP:
    def __init__(self, path):
        self.f = open(path, 'rb')
        magic, entsz, _, _ = struct.unpack('<4I', self.f.read(16))
        assert magic == 0x4d444241  # 'ABDM'
        d = self.f.read(0x300)
        self.sections = {}  # name -> (count, index offset)
        for o in range(0, len(d) - 0x2f, 0x30):
            nm = d[o:o + 0x20].split(b'\0')[0]
            if not nm: break
            a, _, off = struct.unpack_from('<3I', d, o + 0x20)
            self.sections[nm.decode()] = (a, off)
        self._idx = {}

    def index(self, name):
        """-> list of (key, offset, size); blobs live at absolute file offsets."""
        if name not in self._idx:
            n, off = self.sections[name]
            self.f.seek(off)
            raw = self.f.read(16 * n)
            self._idx[name] = [(k, o, s) for k, o, s, _ in struct.iter_unpack('<4I', raw)]
        return self._idx[name]

    def blob(self, name, i):
        k, o, s = self.index(name)[i]
        self.f.seek(o)
        return self.f.read(s)

    def strings(self):
        return {k: self.blob('strings', i).rstrip(b'\0').decode('latin1') for i, (k, _, _) in enumerate(self.index('strings'))}
