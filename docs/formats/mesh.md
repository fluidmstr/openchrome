# Mesh resources (rpack type `0x01100005`)

Chunks are addressed by stream role (low byte of the stream flags): `0x10` object block, `0x11` object table, `0x12` shader slot names, `0xf0` vertex buffer, `0xf1` index buffer (u16 triangle lists).

## Object block (role 0x10) and table (role 0x11)

The block is a serialized object graph (pointers are u64 `offset|1`, strings like `foo.msh`, `foo.mat`, `foo.scr`, a cooked PhysX blob starting `dkcP`). The table: `u32 size, u32 count, u32 1, u32 0`, then `count` x `[0xa0000000|typeId][u32 count][u32 offset]` giving the offset of each typed object in the block. A mesh with groups repeats types 3,4,6,5.

Type 6 object at `o` (one per draw group, e.g. LOD or body part):
- `u32[o-16]` vertex count V
- `u32[o-8]` submesh count N
- `u32[o .. o+N]` index count per submesh (sums to the group's index count; all groups' counts sum to the whole index buffer)

After the N counts follows a u16 array with, per submesh, an index into the list of `*.mat` strings of the block (the material table; verified on `player_bill_fpp`). Character kits such as `survivor_woman_a` (126 groups = head/torso/legs/hair variants and their LODs) list every material of every variant there; which materials are used is decided at runtime, so for kits the index is not meaningful.

## Vertex buffer

Groups are stored back to back; each group's block is `V*stride` bytes padded up to a multiple of **160 bytes**. The stride is not stored anywhere found so far; it is recovered by finding stride assignments whose padded sizes fill the buffer exactly and picking the smoothest result (`tools/mesh.py`). Seen: 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64.

Verified layouts (positions render correctly):
- stride 20: `half3 pos, half 1.0, snorm8x4 normal(?), half2 uv, snorm8x4 tangent(?)`
- stride 32: `float3 pos, 4B, half2 uv (@16), half2 uv2, 4B, 4B`
- stride 40 (skinned): `float3 pos, 4x unorm8 weights (@12, sum 255), 4x u8 bone ids (@16), snorm8x4 normal (@20), half2 uv (@24), half2 uv2, snorm8x4 tangent(?), 4B`. Bone ids are local to the submesh palette.

## Skin palettes and skeleton

Type 5 object (`a` = submesh count of the group) is preceded by `a` 16-byte entries `[ptr+1, 0, count, 0]`; each points to `count` u16 skeleton bone indices (the palette of that submesh). Palettes are in group order. The skeleton itself (bone names, local and inverse-bind matrices) is described in `anim.md`.

Index blocks: each group's u16 triangle list starts at a 4-byte aligned position (one padding u16 after groups with an odd index count; verified: 126 groups, 42 odd, total = sum + 42 pads). Indices are local to their group.

Fast path for kits: when every group has the same stride and the padded sizes fill the buffer, that stride is used without the search. Not yet decoded: tangent semantics, skinned layouts of other strides.

`tools/mesh.py <pack> <outdir> [filter]` writes OBJ (positions, uv, one `g` per submesh, `usemtl` from the .mat names).
