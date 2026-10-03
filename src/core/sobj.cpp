#include "core/sobj.hpp"

#include <cstring>
#include <stdexcept>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
}  // namespace

StaticObjects parseSobj(const std::vector<uint8_t>& d) {
    if (d.size() < 88 || std::memcmp(d.data(), "SO18", 4) != 0) throw std::runtime_error("not SO18");
    uint32_t typeOff = rd<uint32_t>(d.data() + 4), instOff = rd<uint32_t>(d.data() + 8);
    uint32_t n = rd<uint32_t>(d.data() + 28), nt = rd<uint32_t>(d.data() + 36);
    StaticObjects out;
    size_t o = typeOff;
    auto str = [&]() {
        if (o + 2 > d.size()) throw std::runtime_error("sobj: truncated");
        uint16_t k = rd<uint16_t>(&d[o]);
        o += 2;
        if (o + k > d.size()) throw std::runtime_error("sobj: truncated");
        std::string s((const char*)&d[o], k);
        o += k;
        return s;
    };
    for (uint32_t i = 0; i < nt; i++) {
        ObjType t;
        t.mesh = str();
        t.surface = str();
        o += 28;
        t.tmpl = str();
        t.flags = rd<uint16_t>(&d[o]);
        o += 2;
        out.types.push_back(std::move(t));
    }
    if (o != instOff) throw std::runtime_error("sobj: type table does not end at instance table");
    if (instOff + 48ull * n > d.size()) throw std::runtime_error("sobj: truncated instances");
    out.instances.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* p = &d[instOff + 48ull * i];
        Instance& s = out.instances[i];
        std::memcpy(s.pos, p, 12);
        std::memcpy(s.scale, p + 12, 12);
        std::memcpy(s.quat, p + 24, 8);
        s.tag = rd<uint16_t>(p + 32);
        s.type = rd<uint16_t>(p + 34);
    }
    return out;
}

}  // namespace oc
