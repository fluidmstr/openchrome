# Animations (rpack types `0x01400001` clips, `0x01420002` clip sets)

Seen in `common_anims_PC.rpack` (6846 clips, 10 sets); `old_town_PC` has clips for cinematics. Clips live in the stream with role `0x40`; sets use roles `0x42` (records) and `0x43` (event table).

## Clip (`ANM2`)

Header (u32 words): `'ANM2'`, `u16 version=0x2a, u16 flag`, `u16 frames, u16 bones`, `u32 f3` (low u16 = 1 or other, high u16 = offset of the next section in some clips), `u32 size` (= chunk size), `1`, `0`, `0`; then `bones` x u32 hashes of bone names (common bones such as `0xccc3cddf`, `0xe204eae7`, `0x32c99f30`, `0x10f2dc54` recur in every clip).

After the hashes: `u32 (frames-1)|1<<16` twice, three zero words, a u16 table (increasing counts/offsets, meaning unknown), then per-bone static data as floats (a fully static idle clip has 9 floats per bone: 0,0,0, 0,0,0, 1,1,1, probably position, quaternion xyz, scale) and range/quantization floats (`0x37a7c5ac` filler, groups of min/extent floats). Animated tracks are bit-packed; format not decoded. Some clips use a different header arrangement (`version` without the flag, e.g. `_upset_p_s_hidegun`).

## Clip set

Chunk 1: u32 0, then 56-byte records (14 u32): `[588, 0, 0, loop, blend(f32), fps(f32), start(f32), end(f32), 0, 0, 0, eventCount, 0x7ff9, eventOffset]`, then a short tail with the set name. Record count differs from the clip count (e.g. `spitter` 594 records vs 72 clips), so records are probably segments of clips; the link to a clip is not found. Chunk 2: event table (`playsound3d`, `playfx`, `playaisound` with .wav/.fx names and bone names); `eventOffset` is cumulative.

String hash: bone/material/texture hashes are 32-bit but not CRC32 (any variant tried), FNV, murmur2/3, xxh32, lookup3, djb2, sdbm or one-at-a-time, on any case/extension variant. Still unidentified.

`tools/anim.py <pack> [filter]` prints clip headers and set record counts.
