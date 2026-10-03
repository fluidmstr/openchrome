"""Skeleton of a skinned mesh resource (rpack type 0x01100005). Spec: docs/formats/anim.md.
Usage: skel.py <pack> <mesh>   prints bones: index parent name hash local translation"""
import struct, sys
from rpack import Pack

def bone_hash(name):
    """Bone name hash used by ANM2 clips: h = h * 41 + c (u32), starting at 0, case as written."""
    h = 0
    for c in name.encode('latin1'): h = (h * 41 + c) & 0xffffffff
    return h

def skeleton(meta, rel):
    """Bone nodes are 208-byte records in the meta chunk (node 0 at 0x90), each with a name pointer at +136
    (string offset + 1), a local 3x4 matrix at +16 and an inverse bind 3x4 at +64 (column vectors, [R|t]).
    The parent is not stored directly: it is the earlier bone p with world[p] * local == world[k], where
    world = inverse(inv_bind). Returns list of dict(name, parent, local, inv_bind) (4x4 numpy matrices)."""
    import numpy as np
    n = struct.unpack_from('<I', rel, 4)[0]
    count = next(a for t, a, o in (struct.unpack_from('<3I', rel, 16 + 12 * i) for i in range(n)) if t & 0xffff == 2)
    m4 = lambda off: np.vstack([np.array(struct.unpack_from('<12f', meta, off)).reshape(3, 4), [0, 0, 0, 1]])
    bones = []
    for k in range(count):
        base = 144 + 208 * k
        ptr = struct.unpack_from('<I', meta, base + 136)[0] - 1
        bones.append(dict(name=meta[ptr:meta.index(bytes([0]), ptr)].decode('latin1'), local=m4(base + 16), inv_bind=m4(base + 64)))
    world = [np.linalg.inv(b['inv_bind']) for b in bones]
    for k, b in enumerate(bones):
        err = [(np.abs(world[p] @ b['local'] - world[k]).max(), p) for p in range(k)]
        b['parent'] = min(err)[1] if err and min(err)[0] < 1e-3 else -1
    return bones

if __name__ == '__main__':
    pk = Pack(sys.argv[1]); d = pk.by_role(pk.find(sys.argv[2], 0x1100005))
    for i, b in enumerate(skeleton(d[0x10], d[0x11])):
        print(i, b['parent'], b['name'], hex(bone_hash(b['name'])), ['%.3f' % b['local'][k, 3] for k in range(3)])
