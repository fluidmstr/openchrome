"""Static-object lists of a map (.sobj inside Data2.pak). Spec: docs/formats/sobj.md.
Usage: sobj.py <map name>   (e.g. old_town) prints counts and the most used types"""
import struct, sys, zipfile
import numpy as np

PAK = "F:/SteamLibrary/steamapps/common/Dying Light/DW/Data2.pak"

def load(data):
    """-> header words, types [(mesh, surface, template, flags)], instances structured array"""
    h = struct.unpack_from('<22I', data)
    assert h[0] == 0x384f5331 or data[:4] == b'SO18', 'not SO18'
    ntypes, n = h[9], h[7]
    def s(o):
        k = struct.unpack_from('<H', data, o)[0]
        return data[o + 2:o + 2 + k].decode('latin1'), o + 2 + k
    o, types = h[1], []
    for _ in range(ntypes):
        a, o = s(o); b, o = s(o); o += 28; c, o = s(o)
        types.append((a, b, c, struct.unpack_from('<H', data, o)[0])); o += 2
    assert o == h[2], 'type table does not end at the instance table'
    raw = np.frombuffer(data, np.uint8, n * 48, o).reshape(n, 48)
    inst = dict(
        pos=raw[:, :12].copy().view('<f4').reshape(n, 3),
        scale=raw[:, 12:24].copy().view('<f4').reshape(n, 3),
        quat=raw[:, 24:32].copy().view('<i2').reshape(n, 4) / 32767.0,  # x y z w
        type=raw[:, 34:36].copy().view('<u2').ravel(),
        tag=raw[:, 32:34].copy().view('<u2').ravel())  # low 16 bits of word 8, meaning unknown
    return h, types, inst

def read_map(name):
    with zipfile.ZipFile(PAK) as z:
        return load(z.read(f'data/maps/{name}/{name}.sobj'))

def matrices(inst, idx):
    """4x4 column-vector transforms (scale, rotate by quaternion, translate)."""
    q = inst['quat'][idx]; q = q / np.linalg.norm(q, axis=1, keepdims=True)
    x, y, z, w = q.T
    R = np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                  2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                  2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], 1).reshape(-1, 3, 3)
    M = np.zeros((len(idx), 4, 4)); M[:, :3, :3] = R * inst['scale'][idx][:, None, :]
    M[:, :3, 3] = inst['pos'][idx]; M[:, 3, 3] = 1
    return M

if __name__ == '__main__':
    import collections
    h, types, inst = read_map(sys.argv[1] if len(sys.argv) > 1 else 'old_town')
    print(len(types), 'types', len(inst['type']), 'instances')
    for t, c in collections.Counter(inst['type'].tolist()).most_common(8): print(c, types[t])
