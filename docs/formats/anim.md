# Animations (rpack types `0x01400001` clips, `0x01420002` clip sets)

Seen in `common_anims_PC.rpack` (6846 clips, 10 sets); `old_town_PC` has clips for cinematics. Clips live in the stream with role `0x40`; sets use roles `0x42` (records) and `0x43` (event table).

## Clip (`ANM2`)

Header (u32 words): `'ANM2'`, `u16 version=0x2a, u16 flag`, `u16 frames, u16 bones`, `u32 f3` (low u16 = 1 or other, high u16 = offset of the next section in some clips), `u32 size` (= chunk size), `1`, `0`, `0`; then `bones` x u32 hashes of bone names (common bones such as `0xccc3cddf`, `0xe204eae7`, `0x32c99f30`, `0x10f2dc54` recur in every clip).

After the hashes: `u32 (frames-1)|1<<16` twice, three zero words, a u16 table (increasing counts/offsets, meaning unknown), then per-bone static data as floats (a fully static idle clip has 9 floats per bone: 0,0,0, 0,0,0, 1,1,1, probably position, quaternion xyz, scale) and range/quantization floats (`0x37a7c5ac` filler, groups of min/extent floats). Animated tracks are bit-packed; format not decoded. Some clips use a different header arrangement (`version` without the flag, e.g. `_upset_p_s_hidegun`).

## Clip set

Chunk 1: u32 0, then 56-byte records (14 u32): `[588, 0, 0, loop, blend(f32), fps(f32), start(f32), end(f32), 0, 0, 0, eventCount, 0x7ff9, eventOffset]`, then a short tail with the set name. Record count differs from the clip count (e.g. `spitter` 594 records vs 72 clips), so records are probably segments of clips; the link to a clip is not found. Chunk 2: event table (`playsound3d`, `playfx`, `playaisound` with .wav/.fx names and bone names); `eventOffset` is cumulative.

Bone hash (verified): `h = h * 41 + c` over the bone name bytes (u32, start 0, as written), e.g. `bip01` = `0x10f2dc54`, `hspine` = `0xe204eae7`. The hash used for `strings` keys in `optimized_dx11.mp` is a different one (not this with any multiplier).

## ANM2 payload (partly understood)

Payload starts after the bone hashes (`o = 32 + 4*bones`): `u32 (frames-1)|1<<16` twice, a u32 tag, then a u16 table. Channels are 9 per bone in bone order: quaternion xyz, position xyz, scale xyz (w = sqrt(1 - x^2 - y^2 - z^2)); scale is stored as `0.99999994`-style floats.

The table contains `[static channels, animated channels, bones*9, X]` with `X = 64 * ceil(animated / 8)`; before it are increasing numbers (block table of the bit-packed stream, ~14 frames per block, meaning unknown) and the value `~7/3 * bones`.

Verified for fully static clips (stand poses, `animated = 0`): the payload is 48 bytes of header followed by `bones*9` raw floats (bone-major), then 0xff padding. Example `fpp_chainsaw_a_standpose`, `m_npc_reset_anim`.

Animated clips (not decoded): after the header come `X` bytes of per-animated-channel parameters in groups of 8 channels: 8 float bases then 8 floats scale (value `0x37a7c5ac` = 2.0e-5 appears as scale/unused marker), then the raw floats of the *static* channels only, then padding and a sparse bit-packed stream (16-byte rows mostly zero, so probably entropy/delta coded). Only the animated channels are in the stream, 2-147 of them per clip.

## Skeleton (verified, `tools/skel.py`)

Skinned mesh resources (`player_*`, 89-216 bones) carry the skeleton in the meta chunk (role 0x10): the type-2 object (`a` = bone count) is the NUL-separated bone-name string table; bone nodes are 208-byte records from offset 0x90: name pointer at +136 (string offset + 1), local 3x4 matrix at +16, inverse-bind 3x4 at +64 (column vectors `[R|t]`, floats). The parent is not stored: it is the earlier bone p with `inverse(invBind[p]) * local[k] == inverse(invBind[k])` (error < 1e-6 for all tested bones). Name hashes match the clip bone hashes.

`tools/anim.py <pack> [filter]` prints clip headers and set record counts.

## Animated tracks: findings so far (not decoded)

Worked through `m_fpp_lowstamina` (51 frames, 80 bones, 3 animated channels).

Verified:
- Payload layout: 48-byte header+u16 table, then `X = 64*ceil(animated/8)` bytes of per-channel parameters (8 float bases, then 8 float scales per group of 8 channels; unused lanes 0; the scale lane is `2.0e-5` = `0x37a7c5ac` for used lanes here), then `static` floats, then the bit-packed stream (`0xff` padding, then blocks).
- Table at u16 offset 14..: `[1?, block offsets..., 0, 0, static, animated, bones*9, X]`; the block offsets (191, 196, 202, 208, 212) are in 16-byte units, one per block of ~10 frames (5 blocks for 51 frames), and their differences equal the block sizes in 16-byte units.
- The static floats are bone-major with the animated channels left out: in this clip bone 26 stores 6 floats (position + scale), so the 3 animated channels are its quaternion xyz; the bases (0.319, -0.319, -0.340) are close to the neighbouring bone's quaternion (0.333, -0.333, -0.333). Channel positions can be found by looking for bones whose static group is short.
- Stream block = 16-byte header (8 u16) + N rows of 16 bytes; a row holds one u16 per animated channel (lane = channel within its group of 8, unused lanes zero). N varies per block (4, 5, 5, 3).

Rejected: bit-plane decoding of the rows (rows as bit planes of 16 frames) gives noisy values; rows as raw u16 samples scaled by `2e-5` leave the quaternion range. Open: meaning of the block header words (`f4fe 8301 33ff 3480 4380 4380 3380 3380`), and whether rows are spline control points or delta/entropy coded samples. A second clip with a single animated channel is the next best probe.
