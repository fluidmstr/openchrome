#include "core/rpack.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <stdexcept>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
}  // namespace

Pack::Pack(const std::string& path) : path_(path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    uint8_t h[32];
    if (fread(h, 1, 32, f) != 32 || rd<uint32_t>(h) != 0x4c365052) { fclose(f); throw std::runtime_error("not RP6L: " + path); }
    uint32_t nrec = rd<uint32_t>(h + 12), nstr = rd<uint32_t>(h + 16), nres = rd<uint32_t>(h + 20), strsz = rd<uint32_t>(h + 24);
    size_t bytes = 20ull * nstr + 16ull * nrec + 12ull * nres + 4ull * (nres + 1) + strsz;
    std::vector<uint8_t> raw(bytes);
    if (fread(raw.data(), 1, bytes, f) != bytes) { fclose(f); throw std::runtime_error("truncated: " + path); }
    fclose(f);
    const uint8_t* p = raw.data();
    for (uint32_t i = 0; i < nstr; i++, p += 20) {
        Stream s; s.flags = rd<uint32_t>(p + 4); s.offset = rd<uint32_t>(p + 8); s.usize = rd<uint32_t>(p + 12); s.csize = rd<uint32_t>(p + 16);
        streams_.push_back(std::move(s));
    }
    const uint8_t* rows = p; p += 16ull * nrec;
    const uint8_t* dirs = p; p += 12ull * nres;
    const uint8_t* offs = p; p += 4ull * (nres + 1);
    const char* strs = reinterpret_cast<const char*>(p);
    res_.resize(nres);
    for (uint32_t i = 0; i < nres; i++) {
        res_[i].index = i;
        res_[i].flags = rd<uint32_t>(dirs + 12 * i + 4);
        res_[i].name = strs + rd<uint32_t>(offs + 4 * (i + 1));  // offs[0] is a pack-level name
    }
    for (uint32_t i = 0; i < nrec; i++) {
        uint32_t id = rd<uint32_t>(rows + 16 * i + 4);
        if ((id >> 16) < nres)
            res_[id >> 16].chunks.push_back({id & 0xffff, rd<uint32_t>(rows + 16 * i + 8), rd<uint32_t>(rows + 16 * i + 12)});
    }
}

// Inflated streams are cached on disk (shared with tools/rpack.py): $RPACK_CACHE or ./out/cache,
// file name <pack file>.<pack size>.<stream index>.
static std::string cachePath(const std::string& pack, size_t i) {
    namespace fs = std::filesystem;
    const char* env = getenv("RPACK_CACHE");
    fs::path dir = env ? fs::path(env) : fs::path("out") / "cache";
    std::error_code ec;
    uintmax_t size = fs::file_size(pack, ec);
    return (dir / (fs::path(pack).filename().string() + "." + std::to_string(size) + "." + std::to_string(i))).string();
}

const std::vector<uint8_t>& Pack::stream(size_t i) {
    Stream& s = streams_.at(i);
    if (s.loaded) return s.data;
    std::string cache = cachePath(path_, i);
    if (FILE* cf = fopen(cache.c_str(), "rb")) {
        s.data.resize(s.usize);
        size_t n = s.usize ? fread(s.data.data(), 1, s.usize, cf) : 0;
        fclose(cf);
        if (n == s.usize) { s.loaded = true; return s.data; }
    }
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path_);
    _fseeki64(f, s.offset, SEEK_SET);
    s.data.resize(s.usize);
    if (!s.csize) {
        if (fread(s.data.data(), 1, s.usize, f) != s.usize) { fclose(f); throw std::runtime_error("short read"); }
    } else {
        z_stream z{}; inflateInit(&z);
        std::vector<uint8_t> in(1 << 20);
        uint32_t left = s.csize; z.next_out = s.data.data(); z.avail_out = s.usize;
        int rc = Z_OK;
        while (left && rc == Z_OK) {
            size_t n = fread(in.data(), 1, left < in.size() ? left : in.size(), f);
            if (!n) break;
            left -= (uint32_t)n; z.next_in = in.data(); z.avail_in = (uInt)n;
            rc = inflate(&z, Z_NO_FLUSH);
        }
        inflateEnd(&z);
        if (rc != Z_STREAM_END) { fclose(f); throw std::runtime_error("inflate failed in " + path_); }
    }
    fclose(f);
    s.loaded = true;
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(cache).parent_path(), ec);
        std::string tmp = cache + ".tmp";
        if (FILE* cf = fopen(tmp.c_str(), "wb")) {
            bool ok = fwrite(s.data.data(), 1, s.data.size(), cf) == s.data.size();
            fclose(cf);
            if (ok) std::filesystem::rename(tmp, cache, ec); else std::filesystem::remove(tmp, ec);
        }
    }
    return s.data;
}

View Pack::chunkData(const Chunk& c) {
    size_t si = c.part & 0xff;
    if (si >= streams_.size()) return {};
    const auto& d = stream(si);
    if ((size_t)c.offset + c.size > d.size()) return {};
    return {d.data() + c.offset, c.size};
}

View Pack::chunk(const Resource& r, uint8_t role) {
    for (const Chunk& c : r.chunks) {
        size_t si = c.part & 0xff;
        if (si < streams_.size() && (streams_[si].flags & 0xff) == role) {
            const auto& d = stream(si);
            if ((size_t)c.offset + c.size > d.size()) return {};
            return {d.data() + c.offset, c.size};
        }
    }
    return {};
}

}  // namespace oc
