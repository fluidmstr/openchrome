# .rpack (RP6L) container

Observed on `DW/Data/*.rpack` of the Steam build; all fields little-endian. Derived from the files themselves (offset chains, sizes that tile the file exactly, zlib round-trips). 38 of the smaller packs parse with one out-of-bounds chunk (`static_load_PC`, loading_prison, part id with bit 8 set).

## Layout

```
header            32 bytes
stream table      nStreams x 20
record table      nRecords x 16
resource dir      nRes x 12
name offsets      (nRes+1) x u32
string table      strSize bytes (NUL-terminated, latin1)
stream payloads   (offsets from the stream table; tile the rest of the file)
```

### Header

| off | type | meaning |
|----|----|----|
| 0 | u32 | magic `RP6L` (0x4c365052) |
| 4 | u32 | version = 1 |
| 8 | u32 | 1 (unknown) |
| 12 | u32 | nRecords |
| 16 | u32 | nStreams |
| 20 | u32 | nRes |
| 24 | u32 | strSize |
| 28 | u32 | nRes (again) |

### Stream table: `[x][flags][offset][usize][csize]`

- `csize != 0`: one zlib stream (78 xx) of `usize` bytes at `offset`; `csize == 0`: stored raw.
- `flags` low byte = stream role (0x10.. 0x22 resource data, 0xf0.. 0xf8, 0xff = name registry text). Per-role meaning: TODO.

### Record table: `[flags][id][offset][size]`

`id = resIndex<<16 | partId`; `partId & 0xff` is the stream index, the data is `stream[offset:offset+size]`. Bit 8 of `partId` appears on some chunks (meaning unknown). `flags` is nonzero only on the first row (unknown).

### Resource dir: `[firstRecordRow][typeFlags][resIndex]`

Seen type flags (count over 38 packs): `0x01400001` 10318, `0x01100005` 5254 (meshes: names listed in `_MESH_`), `0x01420002` 3069, `0x21200002` 836 and `0x21200003` 703 (textures), `0x81ff0001` 129 (name registries `_MATERIAL_`, `_TEXTURE_`, `_MESH_` with `+name` text lists), `0x01500001` 29, `0x01f80001` 18, `0x01f80003` 9, `0x01100003` 2.

### Names

`nameOffsets[0]` is a pack-level name; resource `i` is `string[nameOffsets[i+1]]`.

## Tool

`tools/rpack.py ls <pack>` lists resources and per-stream sizes; `tools/rpack.py x <pack> <dir>` extracts the parts.
