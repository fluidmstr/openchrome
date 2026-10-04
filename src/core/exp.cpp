#include "core/exp.hpp"

#include <cmath>
#include <cstring>
#include <string_view>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

size_t find(const std::vector<uint8_t>& d, std::string_view s, size_t from, size_t to) {
    if (to > d.size()) to = d.size();
    for (size_t i = from; i + s.size() <= to; i++) {
        if (std::memcmp(&d[i], s.data(), s.size()) == 0) return i;
    }
    return (size_t)-1;
}
}  // namespace

// Each record contains chunks "LDat" and "Lght": [4-char tag][u32 1][u32 payload bytes][payload].
// LDat payload (u32/f32 words): 0, kind x3, 0, r, g, b, intensity. Lght payload starts with a 3x4 row-major
// transform (translation in column 3).
std::vector<Light> parseLights(const std::vector<uint8_t>& d) {
    std::vector<Light> out;
    const std::string_view cls = "LightObject";
    size_t pos = 0;
    while ((pos = find(d, cls, pos, d.size())) != (size_t)-1) {
        size_t next = find(d, cls, pos + cls.size(), d.size());
        size_t end = next == (size_t)-1 ? d.size() : next;
        size_t i = find(d, "LDat", pos, end), j = find(d, "Lght", pos, end);
        pos += cls.size();
        if (i == (size_t)-1 || j == (size_t)-1 || i + 12 + 36 > end || j + 12 + 48 > end) continue;
        if (rd<uint32_t>(&d[i + 8]) < 36 || rd<uint32_t>(&d[j + 8]) < 48) continue;
        const uint8_t* ld = &d[i + 12];
        const uint8_t* lg = &d[j + 12];
        Light l{};
        for (int k = 0; k < 3; k++) l.kind[k] = rd<uint32_t>(ld + 4 * (k + 1));
        for (int k = 0; k < 3; k++) l.color[k] = rd<float>(ld + 4 * (5 + k));
        l.intensity = rd<float>(ld + 32);
        float m[12];
        std::memcpy(m, lg, 48);
        l.pos[0] = m[3]; l.pos[1] = m[7]; l.pos[2] = m[11];
        for (int r = 0; r < 3; r++) l.scale[r] = std::sqrt(m[4 * r] * m[4 * r] + m[4 * r + 1] * m[4 * r + 1] + m[4 * r + 2] * m[4 * r + 2]);
        bool ok = true;
        for (float f : {l.pos[0], l.pos[1], l.pos[2], l.color[0], l.color[1], l.color[2], l.intensity}) ok = ok && std::isfinite(f) && std::fabs(f) < 1e6f;
        if (ok) out.push_back(l);
    }
    return out;
}

std::vector<Entity> parseEntities(const std::vector<uint8_t>& d) {
    // markers: u16 length, identifier (starts upper case), '=', 0
    std::vector<std::pair<size_t, std::string>> marks;
    for (size_t i = 0; i + 4 < d.size(); i++) {
        uint16_t len = rd<uint16_t>(&d[i]);
        if (len < 4 || len > 48 || i + 2 + len + 2 > d.size() || d[i + 2] < 'A' || d[i + 2] > 'Z' || d[i + 2 + len] != '=' || d[i + 3 + len] != 0) continue;
        bool ok = true;
        for (size_t k = 0; k < len && ok; k++) ok = isalnum(d[i + 2 + k]) || d[i + 2 + k] == '_';
        if (!ok) continue;
        marks.push_back({i, std::string(reinterpret_cast<const char*>(&d[i + 2]), len)});
        i += len + 2;
    }
    std::vector<Entity> out;
    for (size_t k = 0; k < marks.size(); k++) {
        size_t b = marks[k].first, e = k + 1 < marks.size() ? marks[k + 1].first : d.size();
        Entity en;
        en.cls = marks[k].second;
        // properties: [u16 id][u32 size][data]; 0xdf = transform (48 bytes), 0x16 = name (u16 length + chars)
        static const uint8_t tr[6] = {0xdf, 0x00, 0x30, 0x00, 0x00, 0x00};
        for (size_t i = b; i + 54 <= e; i++) {
            if (std::memcmp(&d[i], tr, 6) == 0) { std::memcpy(en.m, &d[i + 6], 48); en.hasTransform = true; break; }
        }
        for (size_t i = b; i + 8 <= e; i++) {
            if (d[i] != 0x16 || d[i + 1] != 0 || rd<uint32_t>(&d[i + 2]) < 3) continue;
            uint32_t size = rd<uint32_t>(&d[i + 2]);
            uint16_t len = rd<uint16_t>(&d[i + 6]);
            if (size != len + 2u || i + 8 + len > e) continue;
            en.name.assign(reinterpret_cast<const char*>(&d[i + 8]), len);
            break;
        }
        out.push_back(std::move(en));
    }
    return out;
}

}  // namespace oc
