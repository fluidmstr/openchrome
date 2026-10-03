// Loads every quest XML from DW/Data*.pak. `oc_queststat <DW dir>` prints totals and phase type counts;
// `oc_queststat <DW dir> <level>` lists the quests of one level with their phases.
#include <cstdio>
#include <filesystem>
#include <map>

#include "core/quest.hpp"
#include "core/texts.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_queststat <DW dir> [level]\n"); return 1; }
    std::string level = argc > 2 ? argv[2] : "";
    // English display names: quest "X" -> text key "X_Name"
    std::map<std::string, std::string> names;
    {
        std::string pak = std::string(argv[1]) + "/DataEn.pak";
        for (auto& n : oc::listZip(pak)) {
            if (n.find("_texts_") == std::string::npos || n.substr(n.size() - 4) != ".bin") continue;
            std::vector<uint8_t> b;
            std::vector<oc::TextEntry> t;
            if (oc::readZipEntry(pak, n, b) && oc::parseTexts(b, t))
                for (auto& e : t) if (e.key.size() > 5 && e.key.compare(e.key.size() - 5, 5, "_Name") == 0) names[e.key.substr(0, e.key.size() - 5)] = oc::toUtf8(e.text);
        }
    }
    // a title may itself be a "&Other_Name&" reference to another entry
    auto title = [&](const std::string& q) {
        auto it = names.find(q);
        for (int hop = 0; it != names.end() && hop < 4; hop++) {
            const std::string& s = it->second;
            if (s.size() < 8 || s.front() != '&' || s.compare(s.size() - 6, 6, "_Name&") != 0) return s;
            it = names.find(s.substr(1, s.size() - 7));
        }
        return it == names.end() ? std::string() : it->second;
    };
    int titled = 0, total = 0;
    int files = 0, bad = 0, quests = 0, phases = 0;
    std::map<std::string, int> types;
    for (auto& e : fs::directory_iterator(argv[1])) {
        std::string p = e.path().string();
        if (e.path().extension() != ".pak" || e.path().filename().string().rfind("Data", 0) != 0) continue;
        for (auto& name : oc::listZip(p)) {
            if (name.rfind("data/quests/", 0) != 0 || name.size() < 4 || name.substr(name.size() - 4) != ".xml" || name.find("_underlay") != std::string::npos) continue;
            std::vector<uint8_t> b;
            if (!oc::readZipEntry(p, name, b)) continue;
            oc::QuestFile qf;
            if (!oc::loadQuests(std::string(b.begin(), b.end()), qf)) { bad++; continue; }
            files++;
            for (auto& q : qf.quests) {
                quests++;
                total++;
                titled += title(q.name).empty() ? 0 : 1;
                phases += (int)q.phases.size();
                for (auto& ph : q.phases) types[ph.type]++;
                if (qf.level == level) {
                    printf("%s \"%s\" (parent %s)\n", q.name.c_str(), title(q.name).c_str(), q.parent.c_str());
                    for (auto& ph : q.phases) printf("  [%s] %s\n", ph.type.c_str(), ph.name.c_str());
                }
            }
        }
    }
    if (level.empty()) {
        printf("quests with an English display name: %d of %d\n", titled, total);
        printf("%d quest files (%d not quest documents), %d quests, %d phases\n", files, bad, quests, phases);
        for (auto& t : types) printf("  %-28s %d\n", t.first.c_str(), t.second);
    }
    return 0;
}
