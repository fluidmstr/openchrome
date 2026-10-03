#include "core/skin.hpp"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

Mat34 mul(const Mat34& a, const Mat34& b) {
    Mat34 r{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) r[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j];
        r[4 * i + 3] = a[4 * i] * b[3] + a[4 * i + 1] * b[7] + a[4 * i + 2] * b[11] + a[4 * i + 3];
    }
    return r;
}

Mat34 inverse(const Mat34& m) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    float id = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
    Mat34 r;
    r[0] = (e * i - f * h) * id; r[1] = (c * h - b * i) * id; r[2] = (b * f - c * e) * id;
    r[4] = (f * g - d * i) * id; r[5] = (a * i - c * g) * id; r[6] = (c * d - a * f) * id;
    r[8] = (d * h - e * g) * id; r[9] = (b * g - a * h) * id; r[10] = (a * e - b * d) * id;
    for (int k = 0; k < 3; k++) r[4 * k + 3] = -(r[4 * k] * m[3] + r[4 * k + 1] * m[7] + r[4 * k + 2] * m[11]);
    return r;
}

Mat34 fromTrs(const float* v) {
    float x = v[0], y = v[1], z = v[2], w = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y - z * z));
    Mat34 m{};
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[4] = 2 * (x * y + z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z - x * w);
    m[8] = 2 * (x * z - y * w);     m[9] = 2 * (y * z + x * w);     m[10] = 1 - 2 * (x * x + y * y);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) m[4 * r + c] *= v[6 + c];
        m[4 * r + 3] = v[3 + r];
    }
    return m;
}
}  // namespace

uint32_t boneHash(const std::string& name) {
    uint32_t h = 0;
    for (unsigned char c : name) h = h * 41 + c;
    return h;
}

// Layout: bone nodes are 208-byte records from meta offset 0x90: name pointer (string offset + 1) at +136,
// local 3x4 at +16, inverse bind 3x4 at +64. The bone count is the `a` field of the type-2 relocation entry.
// The parent is the earlier bone p with world[p] * local[k] == world[k].
bool loadSkeleton(View meta, View rel, Skeleton& out) {
    if (rel.size < 16) return false;
    uint32_t n = rd<uint32_t>(rel.data + 4), count = 0;
    for (uint32_t i = 0; i < n && 16 + 12ull * (i + 1) <= rel.size; i++) {
        if ((rd<uint32_t>(rel.data + 16 + 12 * i) & 0xffff) == 2) count = rd<uint32_t>(rel.data + 16 + 12 * i + 4);
    }
    if (count == 0 || 144 + 208ull * count > meta.size) return false;
    for (uint32_t k = 0; k < count; k++) {
        const uint8_t* b = meta.data + 144 + 208ull * k;
        uint32_t ptr = rd<uint32_t>(b + 136);
        if (ptr == 0 || ptr > meta.size) return false;
        const char* s = reinterpret_cast<const char*>(meta.data + ptr - 1);
        size_t len = strnlen(s, meta.size - (ptr - 1));
        out.names.emplace_back(s, len);
        out.hashes.push_back(boneHash(out.names.back()));
        Mat34 l, iv;
        std::memcpy(l.data(), b + 16, 48);
        std::memcpy(iv.data(), b + 64, 48);
        out.local.push_back(l);
        out.invBind.push_back(iv);
    }
    std::vector<Mat34> world(count);
    for (uint32_t k = 0; k < count; k++) world[k] = inverse(out.invBind[k]);
    out.parent.assign(count, -1);
    for (uint32_t k = 1; k < count; k++) {
        float best = 1e-3f;
        for (uint32_t p = 0; p < k; p++) {
            Mat34 w = mul(world[p], out.local[k]);
            float e = 0;
            for (int i = 0; i < 12; i++) e = std::max(e, std::fabs(w[i] - world[k][i]));
            if (e < best) { best = e; out.parent[k] = (int)p; }
        }
    }
    return true;
}

// Clip payload at o = 32 + 4*bones: two (frames-1)|1<<16 words, a tag, a u16 table that contains
// [static, animated, bones*9, X]. Static clips (animated == 0) store 9 floats per bone at o + 48.
bool loadStaticPose(Pack& pack, const Resource& r, Pose& out) {
    for (auto& c : r.chunks) {
        View v = pack.chunkData(c);
        if (v.size < 40 || rd<uint32_t>(v.data) != 0x324d4e41) continue;
        uint32_t bones = rd<uint32_t>(v.data + 8) >> 16;
        size_t o = 32 + 4ull * bones;
        uint32_t total = bones * 9;
        if (!bones || o + 12 + 80 > v.size) return false;
        bool isStatic = false;
        for (size_t i = 0; i + 4 <= 40; i++) {
            const uint8_t* u = v.data + o + 12 + 2 * i;
            if (o + 12 + 2 * i + 8 > v.size) break;
            if (rd<uint16_t>(u + 2) == 0 && rd<uint16_t>(u + 4) == total && rd<uint16_t>(u) == total) { isStatic = true; break; }
        }
        if (!isStatic || o + 48 + 4ull * total > v.size) return false;
        out.hashes.assign(reinterpret_cast<const uint32_t*>(v.data + 32), reinterpret_cast<const uint32_t*>(v.data + 32) + bones);
        out.trs.resize(total);
        std::memcpy(out.trs.data(), v.data + o + 48, 4ull * total);
        return true;
    }
    return false;
}

std::vector<Mat34> skinMatrices(const Skeleton& s, const Pose* pose) {
    std::unordered_map<uint32_t, size_t> at;
    if (pose) for (size_t i = 0; i < pose->hashes.size(); i++) at[pose->hashes[i]] = i;
    std::vector<Mat34> world(s.names.size()), out(s.names.size());
    for (size_t k = 0; k < s.names.size(); k++) {
        Mat34 l = s.local[k];
        auto it = at.find(s.hashes[k]);
        if (it != at.end()) l = fromTrs(&pose->trs[9 * it->second]);
        world[k] = s.parent[k] >= 0 ? mul(world[s.parent[k]], l) : l;
        out[k] = mul(world[k], s.invBind[k]);
    }
    return out;
}

}  // namespace oc
