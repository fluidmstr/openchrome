#include "core/texture.hpp"

#include <algorithm>
#include <cstring>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

struct FormatInfo { TexFormat fmt; uint32_t blockW, blockBytes; };

bool formatInfo(uint32_t id, FormatInfo& f) {
    switch (id) {
        case 2: f = {TexFormat::RGBA8, 1, 4}; return true;
        case 14: f = {TexFormat::R8, 1, 1}; return true;
        case 17: f = {TexFormat::BC1, 4, 8}; return true;
        case 18: f = {TexFormat::BC2, 4, 16}; return true;  // assumed BC2, see docs
        case 19: f = {TexFormat::BC3, 4, 16}; return true;
        default: return false;
    }
}

size_t mipBytes(uint32_t w, uint32_t h, const FormatInfo& f) {
    size_t bw = std::max<uint32_t>(1, (w + f.blockW - 1) / f.blockW), bh = std::max<uint32_t>(1, (h + f.blockW - 1) / f.blockW);
    return bw * bh * f.blockBytes;
}
}  // namespace

bool loadTexture(Pack& pack, const Resource& r, uint32_t maxDim, Texture& out) {
    View hdr{};
    for (const Chunk& c : r.chunks) {
        if (c.size == 151) { hdr = pack.chunkData(c); break; }
    }
    if (!hdr.data) return false;
    uint32_t w = rd<uint16_t>(hdr.data), h = rd<uint16_t>(hdr.data + 2), faces = rd<uint16_t>(hdr.data + 6), mips = rd<uint16_t>(hdr.data + 8);
    FormatInfo fi;
    if (faces != 1 || !w || !h || !mips || !formatInfo(rd<uint32_t>(hdr.data + 12), fi)) return false;
    size_t want = 0;
    for (uint32_t i = 0, mw = w, mh = h; i < mips; i++, mw = std::max(1u, mw >> 1), mh = std::max(1u, mh >> 1)) want += mipBytes(mw, mh, fi);
    View px{};
    for (const Chunk& c : r.chunks) {
        if (c.size == want) { px = pack.chunkData(c); break; }
    }
    if (!px.data) return false;
    out.format = fi.fmt;
    out.width = w; out.height = h;
    size_t off = 0;
    for (uint32_t i = 0, mw = w, mh = h; i < mips; i++, mw = std::max(1u, mw >> 1), mh = std::max(1u, mh >> 1)) {
        size_t n = mipBytes(mw, mh, fi);
        if (std::max(mw, mh) <= maxDim || (out.mips.empty() && i + 1 == mips)) {
            out.mips.push_back({mw, mh, out.data.size(), n});
            out.data.insert(out.data.end(), px.data + off, px.data + off + n);
        }
        off += n;
    }
    return !out.mips.empty();
}

}  // namespace oc
