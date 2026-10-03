#include "core/rpack.hpp"

#include <zlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
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

namespace {
#ifdef _WIN32
struct Mapping {
    HANDLE file = INVALID_HANDLE_VALUE, map = nullptr; const uint8_t* base = nullptr; uint64_t size = 0;
    ~Mapping() { if (base) UnmapViewOfFile(base); if (map) CloseHandle(map); if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
std::shared_ptr<Mapping> mapFile(const std::string& path) {
    auto m = std::make_shared<Mapping>();
    m->file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER sz;
    if (m->file == INVALID_HANDLE_VALUE || !GetFileSizeEx(m->file, &sz) || !sz.QuadPart) return nullptr;
    m->size = (uint64_t)sz.QuadPart;
    m->map = CreateFileMappingA(m->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m->map) return nullptr;
    m->base = (const uint8_t*)MapViewOfFile(m->map, FILE_MAP_READ, 0, 0, 0);
    return m->base ? m : nullptr;
}
#else
struct Mapping {
    const uint8_t* base = nullptr; uint64_t size = 0;
    ~Mapping() { if (base) munmap((void*)base, size); }
};
std::shared_ptr<Mapping> mapFile(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    auto m = std::make_shared<Mapping>();
    if (fstat(fd, &st) == 0 && st.st_size) {
        void* p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) { m->base = (const uint8_t*)p; m->size = st.st_size; }
    }
    close(fd);
    return m->base ? m : nullptr;
}
#endif

// inflates stream bytes [s.offset, s.offset+csize) of `src` straight into `dst` (no whole-stream buffer in RAM)
bool inflateToFile(FILE* src, uint32_t offset, uint32_t csize, FILE* dst, uint32_t usize) {
    _fseeki64(src, offset, SEEK_SET);
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    std::vector<uint8_t> in(1 << 20), out(1 << 22);
    uint32_t left = csize;
    uint64_t total = 0;
    int rc = Z_OK;
    while (rc == Z_OK) {
        if (!z.avail_in) {
            size_t n = left ? fread(in.data(), 1, std::min<size_t>(left, in.size()), src) : 0;
            if (!n) break;
            left -= (uint32_t)n; z.next_in = in.data(); z.avail_in = (uInt)n;
        }
        z.next_out = out.data(); z.avail_out = (uInt)out.size();
        rc = inflate(&z, Z_NO_FLUSH);
        size_t got = out.size() - z.avail_out;
        if (got && fwrite(out.data(), 1, got, dst) != got) { inflateEnd(&z); return false; }
        total += got;
    }
    inflateEnd(&z);
    return rc == Z_STREAM_END && total == usize;
}
}  // namespace

const uint8_t* Pack::stream(size_t i) {
    Stream& s = streams_.at(i);
    if (s.data) return s.data;
    if (!s.csize) {
        auto m = mapFile(path_);
        if (!m || (uint64_t)s.offset + s.usize > m->size) throw std::runtime_error("cannot map " + path_);
        s.data = m->base + s.offset; s.map = m;
        return s.data;
    }
    std::string cache = cachePath(path_, i);
    std::error_code ec;
    if (!(std::filesystem::exists(cache, ec) && std::filesystem::file_size(cache, ec) == s.usize)) {
        std::filesystem::create_directories(std::filesystem::path(cache).parent_path(), ec);
        std::string tmp = cache + ".tmp";
        FILE* f = fopen(path_.c_str(), "rb");
        FILE* cf = fopen(tmp.c_str(), "wb");
        bool ok = f && cf && inflateToFile(f, s.offset, s.csize, cf, s.usize);
        if (f) fclose(f);
        if (cf) fclose(cf);
        if (!ok) { std::filesystem::remove(tmp, ec); throw std::runtime_error("inflate failed in " + path_); }
        std::filesystem::rename(tmp, cache, ec);
    }
    auto m = mapFile(cache);
    if (!m || m->size != s.usize) throw std::runtime_error("cannot map " + cache);
    s.data = m->base; s.map = m;
    return s.data;
}

View Pack::chunkData(const Chunk& c) {
    size_t si = c.part & 0xff;
    if (si >= streams_.size()) return {};
    const uint8_t* d = stream(si);
    if ((size_t)c.offset + c.size > streams_[si].usize) return {};
    return {d + c.offset, c.size};
}

View Pack::chunk(const Resource& r, uint8_t role) {
    for (const Chunk& c : r.chunks) {
        size_t si = c.part & 0xff;
        if (si < streams_.size() && (streams_[si].flags & 0xff) == role) {
            const uint8_t* d = stream(si);
            if ((size_t)c.offset + c.size > streams_[si].usize) return {};
            return {d + c.offset, c.size};
        }
    }
    return {};
}

}  // namespace oc
