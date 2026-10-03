# Texture resources (rpack type `0x21200002` / `0x21200003`)

Header chunk is always 151 bytes (the chunk of that size among the resource's parts); fields little-endian:

| off | type | meaning |
|----|----|----|
| 0 | u16 | width |
| 2 | u16 | height |
| 4 | u16 | 1 (unknown) |
| 6 | u16 | faces (1, or 6 = cubemap) |
| 8 | u16 | mip count |
| 10 | u16 | 0 |
| 12 | u32 | format id |
| 16.. | | zero in samples (unknown) |

Pixel data: the other chunk whose size equals the full mip chain x faces (largest mip first, faces in DDS order). Some textures carry a second smaller chunk (streaming copy, unknown purpose); small textures only have the chunk with part-id bit 8 set.

Format ids (verified by byte counts and by viewing the output): `2` RGBA8 (4 B/texel), `14` 1 B/texel (R8), `17` BC1, `19` BC3, `33` RGBA32F. `18` is 1 B/texel block-compressed, assumed BC2 (unverified).

`tools/textures.py <pack> <outdir> [filter]` writes DDS (DX10 header). Verified visually: bokeh, clouds_top_a, logo_background.
