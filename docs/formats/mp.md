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
| 2 | template hash (key in `templates_*`, see below) |
| 3 | 1 |
| 4 | `nTex << 16 \| 2` (record count is unreliable, ~40% of blobs differ) |
| 5..7 | `0xa0004`-style flags, `0x10000000`, 0 |
| 8.. | slot records `[flags][texture-name hash][1]` (3 words each; the flags byte 2 is a texture format class, not a role) |

Texture refs are found by looking every word up in `strings` (names ending `.dds`; the rpack texture resource name is the same without `.dds`). `flags` byte 2 (0x83..0x86) is not a role. 87% of the 130k texture references resolve to a texture in `DW/Data/*.rpack`.

Tools: `tools/mp.py` (reader), `tools/materials.py` (materials, texture index, classification), `tools/export_model.py <pack> <mesh> <out>` (OBJ + MTL + PNG).

## Albedo selection (heuristic, C++ `MaterialDb::diffuse`)

Used when the sampler binding below does not resolve (count mismatch or missing texture); the albedo is then chosen by name:
1. among the listed textures, `*_dif/_diff/_clr/_color/_d` score highest; names with normal/spec/mask/height/`dye` tokens or `blood/wind/weave/noise/overlay/dirt/env` score low;
2. if nothing scores >= 0, the albedo is derived from the base name of a spec, then normal, then mask map (`plaster_ot_a_spc` -> `plaster_ot_a`, `ot_atlas_nrm_a` -> `ot_atlas_a`) when such a texture exists. This is what colours the Old Town facades: their materials only list `*_dye` (a per-mesh grayscale mask, not a colour map) plus normal/spec maps of shared tiling textures;
3. otherwise the material has no albedo (default gray).

Open: the real blend of the tiling layers and the `dye` tint, car paint (green fringes on cars).


## Templates and sampler binding (verified)

`words[2]` of a material is a key in the `templates_*` sections (the same key may appear in several: pass variants; the first one found is used). A template is a list of descriptor keys (`pass_states`, `blend_states`, `hl_shaders`, `expressions`). `hl_shaders` blobs of pixel shaders contain `[sampler name key][flags]` pairs (names are `s_dif_0`, `s_nrm_0`, `s_spc_0`, `s_dye`, `s_clr`, ... in `strings`; bytecode itself is DXBC in `shaders_t`).

Binding: the material's slot records are in ascending order of `flags & 0xfff` of the template's samplers (flags with bits 0xf000 set are global samplers, skipped). Record *i* is the texture of sampler *i*. In 90% of materials the counts match; the rest (extra global samplers) fall back to the name heuristic.
Albedo = texture bound to `s_dif_0`, else `s_clr`, `s_dif`, `s_det_clr_0` (`MaterialDb::bound`). Terrain blends use `s_dif_0..2` (layers) + `s_nrm_*`/`s_spc_*`; blend weights come from elsewhere (vertex colour / `_t2mat.scr`), not decoded.

## Character dye (guessed, looks right)

Clothing materials bind `s_idx` (mask, same UV as the albedo) and `s_grd` (16x32 RGBA8 palette). Rendered colour = albedo * `s_grd[row][round(mask.r * 15 / 255)]`; rows are colour variants (the viewer uses row 0). Verified only visually on `survivor_woman_torso_b` / `legs_b`: regions of the mask map to coherent garment parts. Which row the game picks per character is unknown.
