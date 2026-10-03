# optimized_dx11.mp (ABDM) - templates, shaders, materials

`DW/Data/optimized_dx11.mp`, little-endian.

## Container

```
u32 'ABDM' (0x4d444241), u32 16, u32 16, u32 0
16 directory entries x 0x30:  char name[32]; u32 count; u32 count; u32 indexOffset
```

Each section `name` is an index of `count` rows `[u32 key][u32 absOffset][u32 size][u32 size]` at `indexOffset`; the blobs sit at absolute file offsets, back to back, directly before the index. Verified: offsets are contiguous in every section and end exactly where the index starts.

Sections: `templates_0000` 7633, `strings` 42295, `shaders_t` 5669, `hl_shaders` 6370, `expressions` 9321, `exp_fixups` 9321, `materials` 25900, `input_attributes` 65, `blend_states` 102, `pass_states` 37, `templates_0008` 10027, `templates_0201` 377, `templates_0001` 2424, `templates_0005` 326, `templates_0408` 60, `templates_0205` 2. Only `strings` and `materials` are decoded.

## strings

Blob = NUL-terminated name (`*.mat`, `*.dds`, shader parameter names...). `key` is a 32-bit hash of the string, the hash function is not identified (keys are used as-is for lookups).

## materials

`key` = hash of the `.mat` name. Blob (u32 words):

| word | meaning |
|----|----|
| 0 | name hash (= key) |
| 1 | 0 |
| 2 | template hash (matches keys in `templates_*`, not decoded) |
| 3 | 1 |
| 4 | `nTex << 16 \| 2` (record count is unreliable, ~40% of blobs differ) |
| 5..9 | `0x00120004, 0x10000000, 0, 0x10000000, 0` |
| 10.. | slot records `[flags][texture-name hash][arg]` |

Texture refs are found by looking every word up in `strings` (names ending `.dds`; the rpack texture resource name is the same without `.dds`). `flags` byte 2 (0x83..0x86) is a slot id whose meaning depends on the template (e.g. 0x84 is mostly a normal map, 0x85 spec/diffuse), so diffuse is picked by name (`_nrm/_spc/_msk...` excluded). 87% of the 130k texture references resolve to a texture in `DW/Data/*.rpack`.

Tools: `tools/mp.py` (reader), `tools/materials.py` (materials, texture index, classification), `tools/export_model.py <pack> <mesh> <out>` (OBJ + MTL + PNG).

## Albedo selection (heuristic, C++ `MaterialDb::diffuse`)

Material templates (`templates_*`, hashes of parameter names) are not decoded, so the albedo is chosen by name:
1. among the listed textures, `*_dif/_diff/_clr/_color/_d` score highest; names with normal/spec/mask/height/`dye` tokens or `blood/wind/weave/noise/overlay/dirt/env` score low;
2. if nothing scores >= 0, the albedo is derived from the base name of a spec, then normal, then mask map (`plaster_ot_a_spc` -> `plaster_ot_a`, `ot_atlas_nrm_a` -> `ot_atlas_a`) when such a texture exists. This is what colours the Old Town facades: their materials only list `*_dye` (a per-mesh grayscale mask, not a colour map) plus normal/spec maps of shared tiling textures;
3. otherwise the material has no albedo (default gray).

Open: the real blend of the tiling layers and the `dye` tint, car paint (green fringes on cars).

