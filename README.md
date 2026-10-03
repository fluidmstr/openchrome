# openchrome

Independent, from-scratch runtime for **Dying Light (1)** (Chrome Engine 6).

**Target:** load and play the original game using your own legitimately owned copy of the assets. The engine is written from scratch (C++, Vulkan/SDL2). No game code or assets are included or distributed here.

**Status:** early reverse engineering of file formats.

- [x] `.rpack` container ([spec](docs/formats/rpack.md), `tools/rpack.py`)
- [x] resource names, per-resource extraction
- [x] textures -> DDS ([spec](docs/formats/texture.md), `tools/textures.py`)
- [x] static meshes -> OBJ ([spec](docs/formats/mesh.md), `tools/mesh.py`)
- [x] materials + texture binding ([spec](docs/formats/mp.md), `tools/export_model.py`)
- [~] animations: clip header + clip sets ([spec](docs/formats/anim.md)); track decoding and skeletons open
- [x] map static objects ([spec](docs/formats/sobj.md), `tools/scene.py`): placements parsed, regions assemble correctly
- [x] free-camera Vulkan viewer: textures, vertex normals, sun/sky lighting, shadow map, terrain chunks + horizon
- [ ] material templates (real colours), skeletal animation, sky, vegetation (`.to`), gameplay entities (`.exp`)
- [ ] gameplay

Unofficial fan project, not affiliated with Techland. Dying Light is a trademark of its owners.

## Build and run (Windows, MSVC + Vulkan SDK)

```
build.bat                     # CMake + NMake, output in buildbuild\oc_viewer.exe "F:\SteamLibrary\steamapps\common\Dying Light\DW" old_town
```

WASD fly, mouse look, Q/E down/up, Shift/Ctrl speed, wheel changes speed, Esc quits. `--shot out.ppm --cam x y z yaw pitch` writes a screenshot. `oc_probe` decodes all meshes of a map as a smoke test.

