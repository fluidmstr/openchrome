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

void bc1Color(const uint8_t* b, uint8_t c[4][3], bool opaque4) {
    uint16_t c0 = rd<uint16_t>(b), c1 = rd<uint16_t>(b + 2);
    auto ex = [](uint16_t v, uint8_t* o) { o[0] = uint8_t((v >> 11 & 31) * 255 / 31); o[1] = uint8_t((v >> 5 & 63) * 255 / 63); o[2] = uint8_t((v & 31) * 255 / 31); };
    ex(c0, c[0]); ex(c1, c[1]);
    for (int k = 0; k < 3; k++) {
        if (opaque4 || c0 > c1) { c[2][k] = uint8_t((2 * c[0][k] + c[1][k]) / 3); c[3][k] = uint8_t((c[0][k] + 2 * c[1][k]) / 3); }
        else { c[2][k] = uint8_t((c[0][k] + c[1][k]) / 2); c[3][k] = 0; }
    }
}

// top mip to RGBA8
bool decodeTop(const Texture& t, std::vector<uint8_t>& px) {
    if (t.mips.empty()) return false;
    const auto& m = t.mips[0];
    const uint8_t* d = t.data.data() + m.offset;
    px.assign((size_t)m.w * m.h * 4, 255);
    if (t.format == TexFormat::RGBA8) { std::memcpy(px.data(), d, px.size()); return true; }
    if (t.format == TexFormat::R8) { for (size_t i = 0; i < (size_t)m.w * m.h; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = d[i]; return true; }
    bool bc1 = t.format == TexFormat::BC1;
    if (!bc1 && t.format != TexFormat::BC3) return false;
    size_t bs = bc1 ? 8 : 16, bw = (m.w + 3) / 4, bh = (m.h + 3) / 4;
    for (size_t by = 0; by < bh; by++)
        for (size_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = d + (by * bw + bx) * bs;
            uint8_t col[4][3];
            bc1Color(bc1 ? b : b + 8, col, !bc1);
            uint32_t bits = rd<uint32_t>((bc1 ? b : b + 8) + 4);
            for (int i = 0; i < 16; i++) {
                size_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= m.w || y >= m.h) continue;
                std::memcpy(&px[(y * m.w + x) * 4], col[bits >> (2 * i) & 3], 3);
            }
        }
    return true;
}
}  // namespace

bool tintTexture(const Texture& albedo, const Texture& mask, const Texture& palette, uint32_t row, Texture& out) {
    std::vector<uint8_t> a, m, p;
    if (!decodeTop(albedo, a) || !decodeTop(mask, m) || !decodeTop(palette, p)) return false;
    uint32_t w = albedo.mips[0].w, h = albedo.mips[0].h, mw = mask.mips[0].w, mh = mask.mips[0].h, pw = palette.mips[0].w;
    if (row >= palette.mips[0].h || pw < 16) return false;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t* q = &a[((size_t)y * w + x) * 4];
            const uint8_t* mk = &m[((size_t)(y * mh / h) * mw + x * mw / w) * 4];
            const uint8_t* c = &p[((size_t)row * pw + (mk[0] * 15 + 127) / 255) * 4];
            for (int k = 0; k < 3; k++) q[k] = uint8_t(q[k] * c[k] / 255);
        }
    out = Texture{};
    out.format = TexFormat::RGBA8; out.width = w; out.height = h;
    std::vector<uint8_t> cur = a;
    for (uint32_t cw = w, ch = h;;) {
        out.mips.push_back({cw, ch, out.data.size(), cur.size()});
        out.data.insert(out.data.end(), cur.begin(), cur.end());
        if (cw == 1 && ch == 1) break;
        uint32_t nw = std::max(1u, cw / 2), nh = std::max(1u, ch / 2);
        std::vector<uint8_t> nx((size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; y++)
            for (uint32_t x = 0; x < nw; x++)
                for (int k = 0; k < 4; k++) {
                    uint32_t x1 = std::min(cw - 1, 2 * x + 1), y1 = std::min(ch - 1, 2 * y + 1), s = 0;
                    s += cur[((size_t)(2 * y) * cw + 2 * x) * 4 + k] + cur[((size_t)(2 * y) * cw + x1) * 4 + k];
                    s += cur[((size_t)y1 * cw + 2 * x) * 4 + k] + cur[((size_t)y1 * cw + x1) * 4 + k];
                    nx[((size_t)y * nw + x) * 4 + k] = uint8_t(s / 4);
                }
        cur.swap(nx); cw = nw; ch = nh;
    }
    return true;
}

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
