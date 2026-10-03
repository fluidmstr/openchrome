#include "core/texts.hpp"

#include <cstring>

namespace oc {

bool parseTexts(const std::vector<uint8_t>& b, std::vector<TextEntry>& out) {
    if (b.size() < 8) return false;
    uint32_t version, count;
    std::memcpy(&version, &b[0], 4);
    std::memcpy(&count, &b[4], 4);
    if (version != 1) return false;
    size_t p = 8;
    auto u16 = [&](uint16_t& v) { if (p + 2 > b.size()) return false; std::memcpy(&v, &b[p], 2); p += 2; return true; };
    for (uint32_t i = 0; i < count; i++) {
        uint16_t klen, tlen;
        if (!u16(klen) || p + klen > b.size()) return false;
        TextEntry e;
        e.key.assign(reinterpret_cast<const char*>(&b[p]), klen);
        p += klen;
        if (!u16(tlen) || p + 2ull * tlen > b.size()) return false;
        e.text.resize(tlen);
        std::memcpy(e.text.data(), &b[p], 2ull * tlen);
        p += 2ull * tlen;
        out.push_back(std::move(e));
    }
    return p == b.size();
}

std::string toUtf8(const std::u16string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xc0 | c >> 6); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xe0 | c >> 12); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xf0 | c >> 18); o += (char)(0x80 | (c >> 12 & 63)); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
    }
    return o;
}

}  // namespace oc
