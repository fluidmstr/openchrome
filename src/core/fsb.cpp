#include "core/fsb.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#define _fseeki64 fseeko
#define _ftelli64 ftello
#endif

namespace oc {
namespace {

const int kStep[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kIdx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kRates[11] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};

template <class T> T rd(const uint8_t* p) { T v; memcpy(&v, p, sizeof v); return v; }

struct File {
    FILE* f = nullptr;
    explicit File(const std::string& p) { f = fopen(p.c_str(), "rb"); }
    ~File() { if (f) fclose(f); }
    bool read(uint64_t off, void* dst, size_t n) {
        return f && _fseeki64(f, (long long)off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
    }
};

}  // namespace

std::vector<SoundEntry> listSounds(const std::string& path) {
    std::vector<SoundEntry> out;
    File f(path);
    if (!f.f) return out;
    // the table of 88-byte records sits at the start of the file: u32 offset, u32 size, u32 2, u32 samples, u32 ms, u32 0, name
    _fseeki64(f.f, 0, SEEK_END);
    std::vector<uint8_t> tab((size_t)std::min<long long>(_ftelli64(f.f), 1 << 20));  // the table is smaller than the first blob
    if (!f.read(0, tab.data(), tab.size())) return out;
    for (size_t i = 64; i + 88 <= tab.size(); i += 4) {
        uint32_t off = rd<uint32_t>(&tab[i]), size = rd<uint32_t>(&tab[i + 4]);
        if (rd<uint32_t>(&tab[i + 8]) - 1 > 1 || off < 64 || size < 100) continue;
        uint8_t h[64];
        if (!f.read(off, h, sizeof h) || memcmp(h, "FSB5", 4) != 0) continue;
        SoundEntry e;
        e.name = std::string(reinterpret_cast<char*>(&tab[i + 24]), strnlen(reinterpret_cast<char*>(&tab[i + 24]), 36));
        e.offset = off;
        e.size = size;
        e.codec = (int)rd<uint32_t>(h + 24);
        out.push_back(e);
        i += 84;
    }
    for (SoundEntry& e : out) {
        uint8_t h[72];
        if (!f.read(e.offset, h, sizeof h)) continue;
        uint64_t x = rd<uint64_t>(h + 60);
        e.rate = kRates[std::min<uint32_t>((x >> 1) & 15, 10)];
        e.channels = (int)((x >> 5) & 3) + 1;
        e.samples = x >> 34;
    }
    return out;
}

bool decodeSound(const std::string& path, const SoundEntry& e, std::vector<int16_t>& pcm) {
    if (e.codec != 7) return false;
    File f(path);
    uint8_t h[64];
    if (!f.read(e.offset, h, sizeof h)) return false;
    uint32_t shdr = rd<uint32_t>(h + 12), names = rd<uint32_t>(h + 16), dsz = rd<uint32_t>(h + 20);
    std::vector<uint8_t> d(dsz);
    uint64_t x;
    if (!f.read(e.offset + 60, &x, 8) || !f.read(e.offset + 60 + shdr + names + ((x >> 7) & 0x7ffffff) * 16, d.data(), dsz)) return false;
    int ch = e.channels;
    size_t frames = d.size() / (36 * (size_t)ch);
    pcm.assign(frames * 64 * ch, 0);
    for (size_t fr = 0; fr < frames; fr++) {
        const uint8_t* p = &d[fr * 36 * ch];
        for (int c = 0; c < ch; c++) {
            int pred = rd<int16_t>(p + 4 * c), idx = std::min<int>(p[4 * c + 2], 88);
            for (int i = 0; i < 64; i++) {
                int u = i / 8, k = i % 8;
                int nib = (p[4 * ch + (u * ch + c) * 4 + k / 2] >> (4 * (k & 1))) & 15;
                int step = kStep[idx], diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                pred = std::clamp(pred + (nib & 8 ? -diff : diff), -32768, 32767);
                idx = std::clamp(idx + kIdx[nib & 7], 0, 88);
                pcm[(fr * 64 + i) * ch + c] = (int16_t)pred;
            }
        }
    }
    pcm.resize(std::min<size_t>(pcm.size(), e.samples * ch));
    return true;
}

}  // namespace oc
