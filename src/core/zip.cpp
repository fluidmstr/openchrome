#include "core/zip.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

struct Entry { std::string name; uint32_t method, csize, usize, local; };

std::vector<Entry> directory(FILE* f) {
    _fseeki64(f, 0, SEEK_END);
    int64_t size = _ftelli64(f);
    int64_t tail = size < 70000 ? size : 70000;
    std::vector<uint8_t> t((size_t)tail);
    _fseeki64(f, size - tail, SEEK_SET);
    if (fread(t.data(), 1, t.size(), f) != t.size()) throw std::runtime_error("zip: read");
    int64_t e = -1;
    for (int64_t i = tail - 22; i >= 0; i--) {
        if (rd<uint32_t>(&t[i]) == 0x06054b50) { e = i; break; }
    }
    if (e < 0) throw std::runtime_error("zip: no end record");
    uint32_t n = rd<uint16_t>(&t[e + 10]), csz = rd<uint32_t>(&t[e + 12]), coff = rd<uint32_t>(&t[e + 16]);
    std::vector<uint8_t> cd(csz);
    _fseeki64(f, coff, SEEK_SET);
    if (fread(cd.data(), 1, csz, f) != csz) throw std::runtime_error("zip: central directory");
    std::vector<Entry> out;
    size_t p = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* h = &cd[p];
        if (rd<uint32_t>(h) != 0x02014b50) throw std::runtime_error("zip: bad central entry");
        uint32_t nl = rd<uint16_t>(h + 28), el = rd<uint16_t>(h + 30), cl = rd<uint16_t>(h + 32);
        out.push_back({std::string((const char*)h + 46, nl), rd<uint16_t>(h + 10), rd<uint32_t>(h + 20), rd<uint32_t>(h + 24), rd<uint32_t>(h + 42)});
        p += 46 + nl + el + cl;
    }
    return out;
}
}  // namespace

std::vector<std::string> listZip(const std::string& pak) {
    FILE* f = fopen(pak.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + pak);
    std::vector<std::string> r;
    for (auto& e : directory(f)) r.push_back(e.name);
    fclose(f);
    return r;
}

bool readZipEntry(const std::string& pak, const std::string& name, std::vector<uint8_t>& out) {
    FILE* f = fopen(pak.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + pak);
    bool ok = false;
    for (auto& e : directory(f)) {
        if (e.name != name) continue;
        uint8_t lh[30];
        _fseeki64(f, e.local, SEEK_SET);
        if (fread(lh, 1, 30, f) != 30) break;
        _fseeki64(f, e.local + 30 + rd<uint16_t>(lh + 26) + rd<uint16_t>(lh + 28), SEEK_SET);
        std::vector<uint8_t> in(e.csize);
        if (fread(in.data(), 1, in.size(), f) != in.size()) break;
        out.resize(e.usize);
        if (e.method == 0) {
            out = in;
            ok = true;
        } else if (e.method == 8) {
            z_stream z{};
            inflateInit2(&z, -15);
            z.next_in = in.data();
            z.avail_in = (uInt)in.size();
            z.next_out = out.data();
            z.avail_out = (uInt)out.size();
            ok = inflate(&z, Z_FINISH) == Z_STREAM_END;
            inflateEnd(&z);
        }
        break;
    }
    fclose(f);
    return ok;
}

}  // namespace oc
