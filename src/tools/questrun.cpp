// `oc_questrun <DW dir> <quest> [seconds]`: runs one quest (and, as they become startable, the quests chained to it) on simulated time
// and prints what its phases do. Waiting phases (go to, talk, use, ...) are satisfied 0.5 s after they start.
#include <cstdio>
#include <filesystem>
#include <map>

#include "core/exp.hpp"
#include "core/questrun.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

static std::map<std::string, oc::Entity> g_ents;  // by name; the entities of the quest's map

static std::string describe(const oc::QuestPhase& p) {
    std::string s = "[" + p.type + "]";
    if (!p.name.empty() && p.name != "_") s += " " + p.name;
    for (auto& a : p.attr) s += " " + a.first + "=" + a.second;
    for (auto& o : p.objects)
        for (auto& q : o.kids)
            if (q.tag == "QuestObject") {
                s += " <" + q.get("class") + " " + q.get("name");
                auto it = g_ents.find(q.get("name"));
                if (it != g_ents.end() && it->second.hasTransform) { char b[64]; snprintf(b, sizeof b, " @%.0f,%.0f,%.0f", it->second.m[3], it->second.m[7], it->second.m[11]); s += b; }
                s += ">";
            }
    return s;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: oc_questrun <DW dir> <quest> [seconds]\n"); return 1; }
    float limit = argc > 3 ? (float)atof(argv[3]) : 600.0f;
    oc::QuestManager mgr;
    mgr.loadAll(argv[1]);
    {
        std::vector<uint8_t> exp;
        std::string level = mgr.levelOf(argv[2]);
        if (oc::readZipEntry((fs::path(argv[1]) / "Data2.pak").string(), "data/maps/" + level + "/" + level + ".exp", exp))
            for (auto& e : oc::parseEntities(exp)) if (!e.name.empty()) g_ents.emplace(e.name, e);
        printf("level %s, %zu named entities\n", level.c_str(), g_ents.size());
    }
    float now = 0;
    std::map<const oc::QuestPhase*, float> seen;
    mgr.onRun = [&](const oc::Quest& q, const oc::QuestPhase& p) { printf("%7.2f %s: %s\n", now, q.name.c_str(), describe(p).c_str()); };
    mgr.onWait = [&](const oc::Quest& q, const oc::QuestPhase& p, float dt) {
        auto it = seen.find(&p);
        if (it == seen.end()) { seen[&p] = now; printf("%7.2f %s: wait %s\n", now, q.name.c_str(), describe(p).c_str()); return false; }
        return now - it->second >= 0.5f;
    };
    if (!mgr.start(argv[2])) { fprintf(stderr, "no quest %s\n", argv[2]); return 1; }
    size_t doneBefore = 0;
    for (; now < limit && mgr.active(); now += 0.25f) {
        mgr.update(0.25f);
        for (auto* q : mgr.startable()) if (mgr.finished(q->parent) ) (void)q;
        (void)doneBefore;
    }
    printf("finished: %s; active quests %zu after %.1f s\n", mgr.finished(argv[2]) ? "yes" : "no", mgr.active(), now);
    return 0;
}
