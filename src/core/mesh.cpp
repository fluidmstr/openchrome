#include "core/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace oc {

namespace {

constexpr size_t kAlign = 160;  // each group's vertex block is padded to this many bytes
size_t up(size_t x) { return (x + kAlign - 1) / kAlign * kAlign; }

template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

float half(uint16_t h) {
    int s = h >> 15, e = (h >> 10) & 31, m = h & 1023;
    float f = e == 0 ? std::ldexp((float)m, -24)
            : e == 31 ? (m ? NAN : INFINITY)
                      : std::ldexp((float)(m + 1024), e - 25);
    return s ? -f : f;
}

void position(const uint8_t* v, int stride, float* o) {
    if (stride <= 24) { for (int i = 0; i < 3; i++) o[i] = half(rd<uint16_t>(v + 2 * i)); }
    else { for (int i = 0; i < 3; i++) o[i] = rd<float>(v + 4 * i); }
}

int uvOffset(int stride) {
    switch (stride) { case 20: case 24: return 12; case 32: return 16; case 40: return 24; default: return -1; }
}

// Vertex normal (snorm8 xyz) offset per stride; verified against face normals (20, 32), weaker for skinned 40.
int normalOffset(int stride) {
    switch (stride) { case 20: return 8; case 32: return 12; case 40: return 20; default: return -1; }
}

struct Group { uint32_t V; std::vector<uint32_t> counts; std::vector<uint16_t> material; };

bool readGroups(View meta, View rel, std::vector<Group>& out) {
    if (rel.size < 16) return false;
    uint32_t n = rd<uint32_t>(rel.data + 4);
    for (uint32_t i = 0; i < n && 16 + 12ull * (i + 1) <= rel.size; i++) {
        uint32_t t = rd<uint32_t>(rel.data + 16 + 12 * i), o = rd<uint32_t>(rel.data + 16 + 12 * i + 8);
        if ((t & 0xffff) != 6) continue;
        if (o < 16 || o + 4 > meta.size) return false;
        uint32_t V = rd<uint32_t>(meta.data + o - 16), N = rd<uint32_t>(meta.data + o - 8);
        if (N > 512 || o + 4ull * N > meta.size) return false;
        Group g{V, {}};
        for (uint32_t k = 0; k < N; k++) g.counts.push_back(rd<uint32_t>(meta.data + o + 4 * k));
        // the u16 array after the counts holds each submesh's index into the mesh's material name list
        if (o + 4ull * N + 2ull * N <= meta.size) for (uint32_t k = 0; k < N; k++) g.material.push_back(rd<uint16_t>(meta.data + o + 4 * N + 2 * k));
        out.push_back(std::move(g));
    }
    return !out.empty();
}

// Mean triangle edge length: a wrong stride scrambles vertices and inflates it.
double edgeScore(const uint8_t* base, int s, uint32_t V, const uint16_t* tri, size_t ntri) {
    double sum = 0;
    size_t used = 0;
    auto pos = [&](uint32_t i, float* p) {
        position(base + (size_t)i * s, s, p);
        for (int k = 0; k < 3; k++) if (!std::isfinite(p[k])) p[k] = 1e9f;
    };
    for (size_t t = 0; t < ntri && used < 2000; t++) {
        uint32_t a = tri[3 * t], b = tri[3 * t + 1], c = tri[3 * t + 2];
        if (a >= V || b >= V || c >= V) continue;
        float pa[3], pb[3];
        pos(a, pa);
        pos(b, pb);
        sum += std::fabs((double)pa[0] - pb[0]) + std::fabs((double)pa[1] - pb[1]) + std::fabs((double)pa[2] - pb[2]);
        used++;
    }
    return used ? std::log1p(sum / used) : 0.0;
}

// Per-group stride: groups sit back to back, each padded to kAlign. Search all assignments that
// fill the vertex buffer exactly, pick the smoothest one (DP over byte offset).
bool solveStrides(const std::vector<Group>& g, View vb, View ib, std::vector<int>& out) {
    // fast path: one stride for every group that fills the vertex buffer exactly (character kits have 100+ groups,
    // which blows up the search below)
    if (g.size() > 4) {
        for (int s = 20; s <= 68; s += 4) {
            size_t total = 0;
            for (const Group& gr : g) total += up((size_t)gr.V * s);
            if (total == vb.size) { out.assign(g.size(), s); return true; }
        }
    }
    struct St { double score; std::vector<int> path; };
    std::map<size_t, St> best{{0, {0.0, {}}}};
    size_t io = 0;
    for (const Group& gr : g) {
        size_t n = 0;
        for (uint32_t c : gr.counts) n += c;
        if ((io + n) * 2 > ib.size) return false;
        const uint16_t* tri = reinterpret_cast<const uint16_t*>(ib.data) + io;
        io += n; io += io & 1;  // each group's index block starts 4-byte aligned
        std::map<size_t, St> next;
        for (auto& [off, st] : best) {
            for (int s = 20; s <= 68; s += 4) {
                size_t end = off + up((size_t)gr.V * s);
                if (end > vb.size || off + (size_t)gr.V * s > vb.size) continue;
                double c = st.score + edgeScore(vb.data + off, s, gr.V, tri, n / 3);
                auto it = next.find(end);
                if (it == next.end() || c < it->second.score) {
                    St ns{c, st.path};
                    ns.path.push_back(s);
                    next[end] = std::move(ns);
                }
            }
        }
        best = std::move(next);
        if (best.empty()) return false;
    }
    auto it = best.find(vb.size);
    if (it == best.end()) return false;
    out = it->second.path;
    return true;
}

}  // namespace

bool loadMesh(Pack& pack, const Resource& r, Mesh& out) {
    View meta = pack.chunk(r, ROLE_META), rel = pack.chunk(r, ROLE_REL), vb = pack.chunk(r, ROLE_VB), ib = pack.chunk(r, ROLE_IB);
    if (!meta.data || !rel.data || !vb.data || !ib.data) return false;
    std::vector<Group> groups;
    if (!readGroups(meta, rel, groups)) return false;
    std::vector<int> strides;
    if (!solveStrides(groups, vb, ib, strides)) return false;
    loadSkeleton(meta, rel, out.skeleton);
    // per-submesh bone palettes: type-5 object with a = submesh count, preceded by a table of [ptr+1, 0, count, 0]
    // entries; u16 skeleton indices at ptr. Palettes are in group order.
    std::vector<std::vector<std::vector<uint16_t>>> palettes;
    {
        std::vector<std::pair<uint32_t, uint32_t>> t5;  // offset, count
        uint32_t n = rd<uint32_t>(rel.data + 4);
        for (uint32_t i = 0; i < n && 16 + 12ull * (i + 1) <= rel.size; i++) {
            if ((rd<uint32_t>(rel.data + 16 + 12 * i) & 0xffff) == 5) t5.push_back({rd<uint32_t>(rel.data + 16 + 12 * i + 8), rd<uint32_t>(rel.data + 16 + 12 * i + 4)});
        }
        std::sort(t5.begin(), t5.end());
        for (auto [o, a] : t5) {
            std::vector<std::vector<uint16_t>> pal;
            for (uint32_t k = 0; k < a && o >= 16 * a; k++) {
                size_t e = o - 16ull * a + 16ull * k;
                if (e + 16 > meta.size) break;
                uint32_t ptr = rd<uint32_t>(meta.data + e), cnt = rd<uint32_t>(meta.data + e + 8);
                if (ptr == 0 || (size_t)ptr - 1 + 2ull * cnt > meta.size) break;
                const uint8_t* q = meta.data + ptr - 1;
                pal.emplace_back(reinterpret_cast<const uint16_t*>(q), reinterpret_cast<const uint16_t*>(q) + cnt);
            }
            palettes.push_back(std::move(pal));
        }
    }
    size_t io = 0, off = 0;
    for (size_t gi = 0; gi < groups.size(); gi++) {
        const Group& g = groups[gi];
        int s = strides[gi];
        MeshGroup mg;
        mg.stride = s;
        mg.counts = g.counts;
        mg.material = g.material;
        size_t n = 0;
        for (uint32_t c : g.counts) n += c;
        mg.pos.resize((size_t)g.V * 3);
        int uo = uvOffset(s);
        if (uo >= 0) mg.uv.resize((size_t)g.V * 2);
        int no = normalOffset(s);
        if (no >= 0) mg.normal.resize(g.V);
        for (uint32_t v = 0; v < g.V; v++) {
            const uint8_t* p = vb.data + off + (size_t)v * s;
            position(p, s, &mg.pos[3 * v]);
            if (no >= 0) mg.normal[v] = rd<uint32_t>(p + no) & 0x00ffffffu;
            if (uo >= 0) {
                mg.uv[2 * v] = half(rd<uint16_t>(p + uo));
                mg.uv[2 * v + 1] = half(rd<uint16_t>(p + uo + 2));
            }
        }
        const uint16_t* tri = reinterpret_cast<const uint16_t*>(ib.data) + io;
        mg.index.assign(tri, tri + n);
        if (s == 40 && !out.skeleton.names.empty() && gi < palettes.size() && palettes[gi].size() == g.counts.size()) {
            // vertex bone indices are local to the palette of the submesh that uses the vertex
            mg.boneIdx.assign((size_t)g.V * 4, 0);
            mg.boneW.assign((size_t)g.V * 4, 0);
            std::vector<char> done(g.V, 0);
            size_t at = 0;
            for (size_t k = 0; k < g.counts.size(); k++) {
                for (size_t i = at; i < at + g.counts[k]; i++) {
                    uint32_t v = mg.index[i];
                    if (v >= g.V || done[v]) continue;
                    done[v] = 1;
                    const uint8_t* p = vb.data + off + (size_t)v * s;
                    for (int c = 0; c < 4; c++) {
                        uint8_t li = p[16 + c];
                        mg.boneIdx[4 * v + c] = li < palettes[gi][k].size() ? palettes[gi][k][li] : 0;
                        mg.boneW[4 * v + c] = p[12 + c];
                    }
                }
                at += g.counts[k];
            }
        }
        io += n; io += io & 1;  // each group's index block starts 4-byte aligned
        off += up((size_t)g.V * s);
        out.groups.push_back(std::move(mg));
    }
    // material names: "*.mat" strings in the object block, in submesh order
    std::string m(reinterpret_cast<const char*>(meta.data), meta.size);
    for (size_t pos = 0; (pos = m.find(".mat", pos)) != std::string::npos; pos += 4) {
        size_t b = pos;
        while (b > 0 && (isalnum((unsigned char)m[b - 1]) || m[b - 1] == '_' || m[b - 1] == '#' || m[b - 1] == '-' || m[b - 1] == '.')) b--;
        out.materials.push_back(m.substr(b, pos + 4 - b));
    }
    return true;
}

}  // namespace oc
