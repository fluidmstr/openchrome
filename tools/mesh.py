"""Mesh resource (rpack type 0x01100005) -> OBJ. Spec: docs/formats/mesh.md.
Usage: mesh.py <pack> <outdir> [name-substring]"""
import os, re, struct, sys
import numpy as np
from rpack import Pack

R_META, R_REL, R_VB, R_IB = 0x10, 0x11, 0xf0, 0xf1
ALIGN = 160  # each group's vertex block is padded to a multiple of this many bytes
# stride -> (position decoder, uv byte offset); layouts for other strides unknown
def _pos(a, s):
    if s <= 24: return np.ascontiguousarray(a[:, :6]).view('<f2').astype(np.float32).reshape(-1, 3)  # half3 (+ half w=1)
    return np.ascontiguousarray(a[:, :12]).view('<f4').reshape(-1, 3)
UV_OFF = {20: 12, 24: 12, 32: 16, 40: 24}

def groups(d):
    """Type-6 object at o: V=u32[o-16], N=u32[o-8], per-submesh index counts=u32[o:o+N]."""
    m, rel = d[R_META], d[R_REL]
    out = []
    for i in range(struct.unpack_from('<I', rel, 4)[0]):
        t, _, o = struct.unpack_from('<3I', rel, 16 + 12 * i)
        if t & 0xffff == 6:
            V, N = struct.unpack_from('<I', m, o - 16)[0], struct.unpack_from('<I', m, o - 8)[0]
            if N > 512 or o + 4 * N > len(m): raise ValueError('bad type-6 object')
            out.append((V, list(struct.unpack_from('<%dI' % N, m, o))))
    return out

def _up(x): return (x + ALIGN - 1) // ALIGN * ALIGN

def _edge(a, s, V, tri):
    """Mean triangle edge length: a wrong stride scrambles vertices and inflates it."""
    p = np.nan_to_num(_pos(a, s).astype(np.float64), nan=1e9, posinf=1e9, neginf=-1e9)
    tri = tri[(tri < V).all(1)][:2000]
    if not len(tri): return 0.0
    return float(np.log1p(np.abs(p[tri[:, 0]] - p[tri[:, 1]]).sum(1).mean()))

def strides(g, vb, ib):
    """Per-group stride. Layout: groups back to back, each padded to ALIGN bytes. Search all stride
    assignments whose total equals len(vb); score by mesh smoothness (DP over byte offset)."""
    tris, io = [], 0
    for _, cnt in g:
        n = sum(cnt); tris.append(np.frombuffer(ib, '<u2', n, io * 2).reshape(-1, 3).astype(np.int64)); io += n
    best = {0: (0.0, [])}
    for (V, _), tri in zip(g, tris):
        nxt = {}
        for off, (sc, path) in best.items():
            for s in range(20, 69, 4):
                end = off + _up(V * s)
                if end > len(vb) or off + V * s > len(vb): continue
                c = sc + _edge(np.frombuffer(vb, np.uint8, V * s, off).reshape(V, s), s, V, tri)
                if end not in nxt or c < nxt[end][0]: nxt[end] = (c, path + [s])
        best = nxt
        if not best: raise ValueError('no stride assignment')
    if len(vb) not in best: raise ValueError('stride assignment does not fill vertex buffer')
    return best[len(vb)][1]

def load(pk, r):
    """-> list of (positions, uvs|None, triangle index array, submesh_index_counts) per group, material names"""
    d = pk.by_role(r); g = groups(d); vb, ib = d[R_VB], d[R_IB]
    ss = strides(g, vb, ib)
    out, io, off = [], 0, 0
    for s, (V, cnt) in zip(ss, g):
        a = np.frombuffer(vb, np.uint8, V * s, off).reshape(V, s); off += _up(V * s)
        n = sum(cnt); tri = np.frombuffer(ib, '<u2', n, io * 2).reshape(-1, 3).astype(np.int64); io += n
        uv = None
        if s in UV_OFF:
            u = UV_OFF[s]; uv = np.ascontiguousarray(a[:, u:u + 4]).view('<f2').astype(np.float32).reshape(-1, 2)
        out.append((_pos(a, s), uv, tri, cnt))
    mats = [x.decode() for x in re.findall(rb'[\w#.\-]+\.mat', d[R_META])]
    return ss, out, mats

def to_obj(name, groups_, mats):
    L, base = [f'# {name}\n'], 1
    for gi, (pos, uv, tri, cnt) in enumerate(groups_):
        for p in pos: L.append('v %g %g %g\n' % tuple(p))
        if uv is not None:
            for u in uv: L.append('vt %g %g\n' % (u[0], 1 - u[1]))
        o = 0
        for si, c in enumerate(cnt):
            L.append(f'g g{gi}_s{si}\n')
            if gi == 0 and len(mats) == len(cnt): L.append(f'usemtl {mats[si][:-4]}\n')
            for t in tri[o // 3:(o + c) // 3] + base:
                L.append('f ' + ' '.join(f'{i}/{i}' if uv is not None else str(i) for i in t) + '\n')
            o += c
        base += len(pos)
    return ''.join(L)

if __name__ == '__main__':
    pk = Pack(sys.argv[1]); os.makedirs(sys.argv[2], exist_ok=True)
    pat = sys.argv[3] if len(sys.argv) > 3 else ''
    ok = bad = 0
    for r in pk.resources:
        if r.flags == 0x1100005 and pat in r.name and any(True for k in r.chunks):
            try:
                s, g, mats = load(pk, r)
                open(os.path.join(sys.argv[2], r.name.replace('/', '_').replace('#', '_') + '.obj'), 'w').write(to_obj(r.name, g, mats)); ok += 1
            except Exception as e:
                bad += 1; print('skip', r.name, e)
    print(ok, 'ok', bad, 'skipped')
