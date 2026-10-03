"""Materials from optimized_dx11.mp + texture lookup across packs. Spec: docs/formats/mp.md."""
import glob, json, os, re, struct
from mp import MP
from rpack import Pack, CACHE

DATA = "F:/SteamLibrary/steamapps/common/Dying Light/DW/Data/"
TEX_TYPES = (0x21200002, 0x21200003)

def load_materials(mp):
    """{material name: {'template': hash, 'tex': [(slotflags, texture name)]}}.
    Blob: u32 name-hash, 0, template hash, 1, (nTex<<16|2), then slot records [flags][texture-name hash][arg]
    (record count varies, so textures are found by looking up each word in the string table)."""
    S = mp.strings(); tex = {k for k, v in S.items() if v.endswith('.dds')}
    out = {}
    for i, (k, _, _) in enumerate(mp.index('materials')):
        b = mp.blob('materials', i); w = struct.unpack('<%dI' % (len(b) // 4), b)
        out[S[k]] = {'template': w[2], 'tex': [(w[j - 1], S[w[j]][:-4]) for j in range(10, len(w)) if w[j] in tex]}
    return out

def texture_index(data=DATA):
    """{texture name: (pack path, resource index)} over every pack in the Data dir (cached as JSON)."""
    cf = os.path.join(CACHE, 'texture_index.json')
    if os.path.exists(cf): return {k: tuple(v) for k, v in json.load(open(cf)).items()}
    idx = {}
    for p in sorted(glob.glob(data + '*.rpack')):
        for r in Pack(p).resources:
            if r.flags in TEX_TYPES: idx.setdefault(r.name, (p, r.index))
    os.makedirs(CACHE, exist_ok=True); json.dump(idx, open(cf, 'w'))
    return idx

def classify(name):
    tok = set(name.lower().split('_'))
    if tok & {'nrm', 'normal', 'nm', 'n'}: return 'normal'
    if tok & {'spc', 'spec', 'shn', 'gloss', 'ems', 'msk', 'mask', 'grd', 'colors', 'det', 'detail'}: return 'other'
    return 'diffuse'

def diffuse(mat):
    return next((n for _, n in mat['tex'] if classify(n) == 'diffuse'), None)

if __name__ == '__main__':
    import sys
    mats = load_materials(MP(DATA + 'optimized_dx11.mp')); ti = texture_index()
    print(len(mats), 'materials,', len(ti), 'textures indexed')
    miss = sum(1 for m in mats.values() for _, n in m['tex'] if n not in ti)
    tot = sum(len(m['tex']) for m in mats.values()); print('texture refs', tot, 'missing from packs', miss)
    for n in sys.argv[1:]: print(n, mats.get(n))
