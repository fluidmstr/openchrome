// `oc_textstat <DW dir>`: parses every *_texts_*.bin of every Data<Lang>.pak and reports whether each parses to its end.
// `oc_textstat <DW dir> <key>`: prints the text of one key in every language.
#include <cstdio>
#include <filesystem>

#include "core/texts.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_textstat <DW dir> [key]\n"); return 1; }
    std::string want = argc > 2 ? argv[2] : "";
    int files = 0, bad = 0;
    size_t entries = 0;
    for (auto& e : fs::directory_iterator(argv[1])) {
        std::string p = e.path().string(), fn = e.path().filename().string();
        if (e.path().extension() != ".pak" || fn.rfind("Data", 0) != 0) continue;
        for (auto& name : oc::listZip(p)) {
            if (name.find("_texts_") == std::string::npos || name.substr(name.size() - 4) != ".bin") continue;
            std::vector<uint8_t> b;
            std::vector<oc::TextEntry> t;
            if (!oc::readZipEntry(p, name, b)) continue;
            files++;
            if (!oc::parseTexts(b, t)) { bad++; printf("%s %s: does not parse\n", fn.c_str(), name.c_str()); continue; }
            entries += t.size();
            for (auto& x : t)
                if (x.key == want) printf("%s: %s\n", fn.c_str(), oc::toUtf8(x.text).c_str());
        }
    }
    if (want.empty()) printf("%d text tables, %d failed, %zu entries\n", files, bad, entries);
    return 0;
}
