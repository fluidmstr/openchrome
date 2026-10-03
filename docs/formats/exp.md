# `.exp` entity database

Per-map file in `Data2.pak`. A flat blob of class-tagged records; only `LightObject` is decoded so far.

## LightObject

Records are found by scanning for the ASCII string `LightObject`; everything up to the next occurrence belongs to the record.
Inside are tagged chunks `[4-char tag][u32 1][u32 payload bytes][payload]`:

| tag    | payload |
|--------|---------|
| `LDat` | u32 words: `0, kind0, kind1, kind2, 0, r, g, b (f32), intensity (f32)` |
| `Lght` | 3x4 row-major transform, translation in column 3 (`m[3], m[7], m[11]`); row lengths of the 3x3 part give the light volume size |

Open: meaning of the `kind` words (point / spot / area variants), cones, falloff, flicker.

## Used by the viewer

Light radius = clamp(max(scale) * 0.55, 3, 40) m, colour * intensity, binned into a 16 m grid (<= 48 lights/cell).
Lamps and window plugs (`emissive` surfaces) turn on at night; the real per-lamp on/off state is not decoded.

## Other classes seen (not decoded)

`ModelObject`, `SpawnPoint`, `Encounter`, ...
