# .rpack (RP6L) container

Observed on `DW/Data/*.rpack` of the Steam build; all fields little-endian. Derived from the files themselves (offset chains, sizes that tile the file exactly, zlib round-trips).

## Header (32 bytes)

| off | type | meaning |
|----|----|----|
| 0 | u32 | magic `RP6L` (0x4c365052) |
| 4 | u32 | version = 1 |
| 8 | u32 | 1 (unknown) |
| 12 | u32 | total resource-record count (unverified) |
| 16 | u32 | number of streams |
| 20.. | u32 x3 | counts / sizes, not yet decoded |

## Stream table (at 32, `streams` x 20 bytes)

`[x][flags][offset][usize][csize]`

- `csize != 0`: payload at `offset` is one zlib stream (78 xx header) of `usize` bytes.
- `csize == 0`: stored raw, `usize` bytes.
- Streams tile the tail of the file: `offset + csize` of one equals `offset` of the next, last ends at EOF.
- Verified: `common_anims_PC` stream 0 (213 MB compressed) inflates to exactly `usize` = 0x102097c0 as a single zlib stream; `engine_PC` has 9 streams, all inflate to `usize`.
- `flags` low byte = stream kind (e.g. 0x10-0x22 resource data, 0xf0/0xf1, 0xff = text name list, raw). Meaning of each kind: TODO.

## Record table (after the stream table)

16-byte rows `[0][id][offset][size]`, `id = index<<16 | part`. `offset/size` address the inflated stream selected by `part` (0, 1, 2 seen); within a part the rows are contiguous (`offset+size` = next offset). Exact start/count and the tail index tables are still to be decoded.

## Name list

Last stream (raw, `\n`-separated, prefix `+`), e.g. `+add_blinn&0000` (shaders in engine_PC), `+_calm_r_s_boxjumpdown2` (anims). Mapping name -> record not yet established.

## Tool

`tools/rpack.py <file> [outdir]` dumps every inflated stream.
