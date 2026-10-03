#include "core/mp.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace oc {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

bool endsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Name-based guess of how likely a texture is the albedo map: *_dif/_clr/... high, masks, normals and overlays low.
int diffuseScore(const std::string& name) {
    std::string l = name;
    for (auto& c : l) c = (char)tolower((unsigned char)c);
    std::vector<std::string> tok;
    for (size_t b = 0; b <= l.size();) {
        size_t e = l.find('_', b);
        if (e == std::string::npos) e = l.size();
        tok.push_back(l.substr(b, e - b));
        b = e + 1;
    }
    auto has = [&](std::initializer_list<const char*> set) {
        for (auto& t : tok) for (auto s : set) if (t == s) return true;
        return false;
    };
    int score = 0;
    const std::string& last = tok.back();
    for (auto s : {"dif", "diff", "diffuse", "clr", "color", "col", "albedo", "d"}) if (last == s) score += 10;
    if (has({"nrm", "normal", "nm", "n", "spc", "spec", "shn", "gloss", "ems", "msk", "mask", "grd", "colors", "det", "detail", "dye", "hgt"})) score -= 20;
    for (auto s : {"blood", "wind", "weave", "noise", "overlay", "dirt", "env"}) if (l.find(s) != std::string::npos) score -= 8;
    return score;
}
}  // namespace

MaterialDb::MaterialDb(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    _fseeki64(f, 0, SEEK_END);
    int64_t size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    file_.resize((size_t)size);
    if (fread(file_.data(), 1, file_.size(), f) != file_.size()) { fclose(f); throw std::runtime_error("read " + path); }
    fclose(f);
    if (file_.size() < 16 || rd<uint32_t>(file_.data()) != 0x4d444241) throw std::runtime_error("not ABDM: " + path);
    struct Sec { uint32_t count, index; };
    std::unordered_map<std::string, Sec> secs;
    for (size_t o = 16; o + 0x30 <= file_.size() && file_[o]; o += 0x30) {
        std::string name((const char*)&file_[o], strnlen((const char*)&file_[o], 32));
        secs[name] = {rd<uint32_t>(&file_[o + 0x20]), rd<uint32_t>(&file_[o + 0x28])};
    }
    if (!secs.count("strings") || !secs.count("materials")) throw std::runtime_error("mp: missing sections");
    auto rows = [&](const Sec& s, auto fn) {
        for (uint32_t i = 0; i < s.count; i++) {
            const uint8_t* r = &file_[s.index + 16ull * i];
            fn(rd<uint32_t>(r), rd<uint32_t>(r + 4), rd<uint32_t>(r + 8));
        }
    };
    rows(secs["strings"], [&](uint32_t key, uint32_t off, uint32_t sz) {
        if (off + sz > file_.size() || sz == 0) return;
        std::string s((const char*)&file_[off], strnlen((const char*)&file_[off], sz));
        if (endsWith(s, ".dds")) dds_[key] = s.substr(0, s.size() - 4);
        else if (endsWith(s, ".mat")) matKey_[s] = key;
    });
    rows(secs["materials"], [&](uint32_t key, uint32_t off, uint32_t sz) { blobs_[key] = {off, sz}; });
}

namespace {
// "ot_brick_e_nrm" -> "ot_brick_e", "ot_atlas_nrm_a" -> "ot_atlas_a": the albedo shares the base name of its normal/spec map.
bool baseName(const std::string& name, const char* tok, std::string& out) {
    std::string l = name;
    for (auto& c : l) c = (char)tolower((unsigned char)c);
    std::vector<std::string> parts;
    for (size_t b = 0; b <= l.size();) {
        size_t e = l.find('_', b);
        if (e == std::string::npos) e = l.size();
        parts.push_back(l.substr(b, e - b));
        b = e + 1;
    }
    for (size_t i = 1; i < parts.size(); i++) {
        if (parts[i] != tok) continue;
        out.clear();
        for (size_t k = 0; k < parts.size(); k++) if (k != i) out += (out.empty() ? "" : "_") + parts[k];
        return true;
    }
    return false;
}
}  // namespace

std::string MaterialDb::diffuse(const std::string& mat, const std::function<bool(const std::string&)>& exists) const {
    auto k = matKey_.find(mat);
    if (k == matKey_.end()) return {};
    auto b = blobs_.find(k->second);
    if (b == blobs_.end()) return {};
    std::vector<std::string> names;
    for (uint32_t o = b->second.first + 40; o + 4 <= b->second.first + b->second.second; o += 4) {
        auto t = dds_.find(rd<uint32_t>(&file_[o]));
        if (t != dds_.end()) names.push_back(t->second);
    }
    std::string best;
    int bestScore = -1000;
    for (auto& n : names) {
        int s = diffuseScore(n);
        if (s > bestScore) { bestScore = s; best = n; }
    }
    if (bestScore >= 0 && exists(best)) return best;
    // no albedo listed: derive it from spec, then normal, then mask names
    for (const char* tok : {"spc", "shn", "nrm", "msk"}) {
        for (auto& n : names) {
            std::string base;
            if (baseName(n, tok, base) && exists(base)) return base;
        }
    }
    return bestScore > -20 && exists(best) ? best : std::string();
}

}  // namespace oc
