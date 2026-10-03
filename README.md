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
- [ ] map loading + free camera
- [ ] gameplay

Unofficial fan project, not affiliated with Techland. Dying Light is a trademark of its owners.
