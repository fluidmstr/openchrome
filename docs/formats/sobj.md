# Map static objects (`data/maps/<map>/<map>.sobj`, inside `DW/Data2.pak`)

`DW/Data*.pak` are ordinary ZIP archives (Data0: scripts/xml/defs, Data2: maps: `.sobj`, `.navmesh`, `.road`, `.radargeom`, `.exp`, `.to` foliage, `.startprop`, scripts). Little-endian.

## Header (22 u32 words)

| word | meaning |
|----|----|
| 0 | `'SO18'` |
| 1 | 0x58, offset of the type table (= header size) |
| 2 | offset of the instance table (end of type table) |
| 3..6 | offsets of later sections (not decoded) |
| 7 | instance count (old_town: 211740) |
| 9 | type count (old_town: 4707) |
| 8,10..14 | other counts (unknown) |
| 15..21 | world bounds / floats (unverified) |

## Type table (from word 1 to word 2)

Per entry: `u16 len + mesh name` (e.g. `water_box_a.msh`), `u16 len + surface/material name` (`Blood_a`, `Default`, `Brick`), 28 bytes (zeros except `u32 1`, `i32 -1` at +20, sometimes extra ints), `u16 len + template name`, `u16 flags`. Parses exactly to word 2 for old_town.

## Instances (48 bytes each, `count` rows starting at word 2)

| off | type |
|----|----|
| 0 | f32 x3 position (y is up) |
| 12 | f32 x3 scale |
| 24 | i16 x4 quaternion x,y,z,w, divide by 32767 (unit norm) |
| 32 | u16 tag (meaning unknown) |
| 34 | u16 type index into the type table |
| 36 | i32 -1 (link? unknown) |
| 40 | 8 zero bytes |

Verified: types referenced span exactly 0..4706; positions give the Old Town layout; 3573 instances within 45 m of (300,100) assembled with their meshes (group 0 = LOD0) form recognisable buildings, trees and terrain.

Mesh name -> geometry: strip `.msh`, look up the rpack mesh resource across `DW/Data/*.rpack`.

`tools/sobj.py <map>` prints counts; `tools/scene.py <map> <cx> <cz> <radius> <out.png> [out.obj]` assembles a region and renders a shaded preview.
