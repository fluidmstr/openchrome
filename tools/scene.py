"""Assemble a map region from static objects and render a shaded preview / export OBJ.
Usage: scene.py <map> <cx> <cz> <radius> <out.png> [out.obj]"""
import glob, json, os, sys
import numpy as np
import sobj, mesh
from rpack import Pack, CACHE
from materials import DATA

SKIP = ('blood', 'decal', 'dummy', 'dead_body', 'trigger', 'collision', 'physics')

def mesh_index():
    cf = os.path.join(CACHE, 'mesh_index.json')
    if os.path.exists(cf): return {k: tuple(v) for k, v in json.load(open(cf)).items()}
    idx = {}
    for p in sorted(glob.glob(DATA + '*.rpack')):
        for r in Pack(p).resources:
            if r.flags == 0x1100005: idx.setdefault(r.name, (p, r.index))
    os.makedirs(CACHE, exist_ok=True); json.dump(idx, open(cf, 'w'))
    return idx

_packs = {}
def geometry(index, name):
    """LOD0 (group 0) of a mesh -> (vertices Nx3, triangles Mx3) or None."""
    if name not in index: return None
    p, i = index[name]
    pk = _packs.setdefault(p, Pack(p))
    try:
        _, groups, _ = mesh.load(pk, pk.resources[i])
    except Exception:
        return None
    pos, _, tri, _ = groups[0]
    ok = np.isfinite(pos).all(1)
    tri = tri[(tri < len(pos)).all(1)]
    tri = tri[ok[tri].all(1)]
    return (pos.astype(np.float64), tri) if len(tri) else None

def build(mapname, cx, cz, radius, index, limit=4000):
    h, types, inst = sobj.read_map(mapname)
    d = np.hypot(inst['pos'][:, 0] - cx, inst['pos'][:, 2] - cz)
    sel = np.nonzero(d < radius)[0]
    keep = [i for i in sel if not any(s in types[inst['type'][i]][0].lower() for s in SKIP)][:limit]
    keep = np.array(keep, int); M = sobj.matrices(inst, keep)
    cache, V, T, off = {}, [], [], 0
    for k, i in enumerate(keep):
        t = int(inst['type'][i]); name = types[t][0].rsplit('.', 1)[0]
        if t not in cache: cache[t] = geometry(index, name)
        g = cache[t]
        if g is None: continue
        v = g[0] @ M[k][:3, :3].T + M[k][:3, 3]
        V.append(v); T.append(g[1] + off); off += len(v)
    return np.concatenate(V), np.concatenate(T), len(keep), sum(1 for g in cache.values() if g is not None), len(cache)

def render(V, T, out, eye_dir=(0.6, 0.55, 0.6), size=1400):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    c = V.mean(0); f = -np.array(eye_dir) / np.linalg.norm(eye_dir)
    up = np.array([0, 1.0, 0]); r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
    P = (V - c) @ np.stack([r, u, f]).T            # x right, y up, z depth
    tri = P[T]; depth = tri[:, :, 2].mean(1)
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    shade = 0.25 + 0.75 * np.abs(n @ np.array([0.3, 0.5, -0.8]))
    o = np.argsort(-depth)
    fig = plt.figure(figsize=(size / 100, size / 100), dpi=100); ax = fig.add_axes([0, 0, 1, 1])
    ax.add_collection(PolyCollection(tri[o][:, :, :2], facecolors=np.stack([shade[o]] * 3, 1), edgecolors='none'))
    lim = np.abs(P[:, :2]).max(); ax.set_xlim(-lim, lim); ax.set_ylim(-lim, lim); ax.axis('off'); ax.set_facecolor('#d8e4f0')
    fig.savefig(out, facecolor='#d8e4f0'); plt.close(fig)

if __name__ == '__main__':
    m, cx, cz, rad, out = sys.argv[1], *map(float, sys.argv[2:5]), sys.argv[5]
    V, T, ni, ng, nt = build(m, cx, cz, rad, mesh_index())
    print(ni, 'instances,', ng, 'of', nt, 'types had geometry,', len(T), 'triangles')
    render(V, T, out)
    if len(sys.argv) > 6:
        with open(sys.argv[6], 'w') as f:
            f.writelines('v %g %g %g\n' % tuple(v) for v in V)
            f.writelines('f %d %d %d\n' % tuple(t + 1) for t in T)
