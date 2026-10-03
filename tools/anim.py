"""Animation resources (rpack types 0x01400001 clips, 0x01420002 sets). Spec: docs/formats/anim.md.
Usage: anim.py <pack> [name-substring]   lists clips (ANM2 header) and set records"""
import struct, sys
from rpack import Pack

def clip_header(b):
    """ANM2 clip header. Track data after the bone hash table is not decoded yet."""
    magic, v, fb, f3, size, one = struct.unpack_from('<6I', b)
    assert magic == 0x324d4e41, 'not ANM2'
    frames, bones = fb & 0xffff, fb >> 16
    hashes = struct.unpack_from('<%dI' % bones, b, 32)
    return dict(version=v & 0xffff, flag=v >> 16, frames=frames, bones=bones, size=size, f3=f3, hashes=hashes)

def set_records(a):
    """Clip-set chunk: u32 0, then 14-word records, then a short tail (set name)."""
    n = (len(a) - 4) // 56
    f = lambda x: struct.unpack('<f', struct.pack('<I', x))[0]
    out = []
    for i in range(n):
        w = struct.unpack_from('<14I', a, 4 + 56 * i)
        out.append(dict(loop=w[3], blend=f(w[4]), fps=f(w[5]), start=f(w[6]), end=f(w[7]), events=w[11], event_offset=w[13]))
    return out

if __name__ == '__main__':
    pk = Pack(sys.argv[1]); pat = sys.argv[2] if len(sys.argv) > 2 else ''
    for r in pk.resources:
        if pat not in r.name: continue
        if r.flags == 0x1400001:
            h = clip_header(pk.data(r)[0]); print(f"{r.name}: {h['frames']} frames, {h['bones']} bones, {h['size']} bytes")
        elif r.flags == 0x1420002:
            print(f'{r.name}: {len(set_records(pk.data(r)[1]))} records')
