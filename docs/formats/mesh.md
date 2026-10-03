# Mesh resources (rpack type `0x01100005`)

Chunks are addressed by stream role (low byte of the stream flags): `0x10` object block, `0x11` object table, `0x12` shader slot names, `0xf0` vertex buffer, `0xf1` index buffer (u16 triangle lists).

## Object block (role 0x10) and table (role 0x11)

The block is a serialized object graph (pointers are u64 `offset|1`, strings like `foo.msh`, `foo.mat`, `foo.scr`, a cooked PhysX blob starting `dkcP`). The table: `u32 size, u32 count, u32 1, u32 0`, then `count` x `[0xa0000000|typeId][u32 count][u32 offset]` giving the offset of each typed object in the block. A mesh with groups repeats types 3,4,6,5.

Type 6 object at `o` (one per draw group, e.g. LOD or body part):
- `u32[o-16]` vertex count V
- `u32[o-8]` submesh count N
- `u32[o .. o+N]` index count per submesh (sums to the group's index count; all groups' counts sum to the whole index buffer)

Material names are the `*.mat` strings in the block, in submesh order of group 0.

## Vertex buffer

Groups are stored back to back; each group's block is `V*stride` bytes padded up to a multiple of **160 bytes**. The stride is not stored anywhere found so far; it is recovered by finding stride assignments whose padded sizes fill the buffer exactly and picking the smoothest result (`tools/mesh.py`). Seen: 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64.

Verified layouts (positions render correctly):
- stride 20: `half3 pos, half 1.0, snorm8x4 normal(?), half2 uv, snorm8x4 tangent(?)`
- stride 32: `float3 pos, 4B, half2 uv (@16), half2 uv2, 4B, 4B`
- stride 40: `float3 pos, 4B weights(?), 4B bone ids(?), 4B, half2 uv (@24), half2 uv2, 8B` (skinned/animated)

Indices are local to their group. Not yet decoded: normals/tangents semantics, skin weights, other strides, bind poses.

`tools/mesh.py <pack> <outdir> [filter]` writes OBJ (positions, uv, one `g` per submesh, `usemtl` from the .mat names).
