// Localisation tables (data/maps/*_texts_*.bin in Data<Lang>.pak), see docs/formats/texts.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

struct TextEntry {
    std::string key;
    std::u16string text;
};

// False if the blob does not parse to exactly its end.
bool parseTexts(const std::vector<uint8_t>& blob, std::vector<TextEntry>& out);

std::string toUtf8(const std::u16string& s);

}  // namespace oc
