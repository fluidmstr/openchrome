// Loads every quest XML from DW/Data*.pak. `oc_queststat <DW dir>` prints totals and phase type counts;
// `oc_queststat <DW dir> <level>` lists the quests of one level with their phases.
#include <cstdio>
#include <filesystem>
#include <map>

#include "core/quest.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_queststat <DW dir> [level]\n"); return 1; }
    std::string level = argc > 2 ? argv[2] : "";
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
                phases += (int)q.phases.size();
                for (auto& ph : q.phases) types[ph.type]++;
                if (qf.level == level) {
                    printf("%s (parent %s)\n", q.name.c_str(), q.parent.c_str());
                    for (auto& ph : q.phases) printf("  [%s] %s\n", ph.type.c_str(), ph.name.c_str());
                }
            }
        }
    }
    if (level.empty()) {
        printf("%d quest files (%d not quest documents), %d quests, %d phases\n", files, bad, quests, phases);
        for (auto& t : types) printf("  %-28s %d\n", t.first.c_str(), t.second);
    }
    return 0;
}
