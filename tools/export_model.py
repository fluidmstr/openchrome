"""Mesh + materials + diffuse textures -> OBJ/MTL/PNG.
Usage: export_model.py <pack> <mesh name> <outdir>"""
import io, os, sys
from PIL import Image
from mp import MP
import materials, mesh, textures
from rpack import Pack

def export(pack_path, name, out, mats=None, tindex=None):
    pk = Pack(pack_path); r = pk.find(name, 0x1100005)
    mats = mats or materials.load_materials(MP(materials.DATA + 'optimized_dx11.mp'))
    tindex = tindex or materials.texture_index()
    _, groups, names = mesh.load(pk, r)
    os.makedirs(out, exist_ok=True)
    mtl = []
    for mn in dict.fromkeys(names):
        mtl.append(f'newmtl {mn[:-4]}\n')
        m = mats.get(mn); t = materials.diffuse(m) if m else None
        if t and t in tindex:
            try:
                p, i = tindex[t]; tp = Pack(p)
                Image.open(io.BytesIO(textures.export(tp, tp.resources[i]))).convert('RGBA').save(os.path.join(out, t + '.png'))
                mtl.append(f'map_Kd {t}.png\n')
            except Exception as e:
                print('texture', t, 'failed:', e)
    open(os.path.join(out, name + '.mtl'), 'w').write(''.join(mtl))
    open(os.path.join(out, name + '.obj'), 'w').write(f'mtllib {name}.mtl\n' + mesh.to_obj(name, groups, names))

if __name__ == '__main__':
    export(*sys.argv[1:4])
