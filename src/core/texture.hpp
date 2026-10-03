// Texture resources (type 0x2120000x), see docs/formats/texture.md
#pragma once
#include <cstdint>
#include <vector>

#include "core/rpack.hpp"

namespace oc {

enum class TexFormat { RGBA8, R8, BC1, BC2, BC3 };

struct Texture {
    uint32_t width = 0, height = 0;
    TexFormat format = TexFormat::RGBA8;
    struct Mip { uint32_t w, h; size_t offset, size; };
    std::vector<Mip> mips;        // largest first, only levels up to the requested max dimension
    std::vector<uint8_t> data;    // tightly packed mip data
};

// Decodes a 2D texture, dropping mip levels larger than maxDim. False for cubemaps / unknown formats.
bool loadTexture(Pack& pack, const Resource& r, uint32_t maxDim, Texture& out);

}  // namespace oc
