// `oc_itemstat <DW dir>`: loads every Item() from data/scripts/**/*.scr and prints totals per category and property.
// `oc_itemstat <DW dir> <item id>` dumps one item.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>

#include <set>

#include "core/items.hpp"
#include "core/rpack.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_itemstat <DW dir> [item id]\n"); return 1; }
    std::string want = argc > 2 ? argv[2] : "";
    std::map<std::string, int> cats, props;
    std::map<std::string, int> seen;
    std::vector<oc::ItemDef> all;
    int total = 0, dup = 0;
    for (auto& e : fs::directory_iterator(argv[1])) {
        std::string p = e.path().string();
        if (e.path().extension() != ".pak" || e.path().filename().string().rfind("Data", 0) != 0) continue;
        for (auto& name : oc::listZip(p)) {
            if (name.rfind("data/scripts/", 0) != 0 || name.size() < 4 || name.substr(name.size() - 4) != ".scr") continue;
            std::vector<uint8_t> b;
            if (!oc::readZipEntry(p, name, b) || b.empty() || b[0] == 0xff) continue;
            std::vector<oc::ItemDef> items;
            oc::collectItems(oc::parseScript(std::string(b.begin(), b.end())), items);
            for (auto& it : items) {
                total++;
                all.push_back(it);
                if (seen[it.id]++) dup++;
                cats[it.base.empty() ? it.category : "(derived from another item)"]++;
                for (auto& pr : it.props) props[pr.first]++;
                if (it.id == want) {
                    printf("%s (%s) from %s\n", it.id.c_str(), it.category.c_str(), name.c_str());
                    for (auto& pr : it.props) {
                        printf("  %s(", pr.first.c_str());
                        for (size_t k = 0; k < pr.second.size(); k++) printf("%s%s", k ? ", " : "", pr.second[k].text.c_str());
                        printf(")\n");
                    }
                }
            }
        }
    }
    if (want.empty()) {
        std::map<std::string, int> plain;
        for (auto& i : all) if (i.base.empty()) plain[i.id]++;
        int derived = 0, resolved = 0, ownMesh = 0;
        for (auto& i : all) if (!i.base.empty()) { derived++; resolved += plain.count(i.base) ? 1 : 0; ownMesh += i.first("Mesh") ? 1 : 0; }
        // mesh names (own or inherited from the base item) against the mesh resources of all packs
        std::set<std::string> meshes;
        for (auto& e : fs::directory_iterator(fs::path(argv[1]) / "Data")) {
            if (e.path().extension() != ".rpack") continue;
            try { oc::Pack pk(e.path().string()); for (auto& r : pk.resources()) if (r.flags == oc::TYPE_MESH) meshes.insert(r.name); } catch (std::exception&) {}
        }
        std::map<std::string, const oc::ItemDef*> byId;
        for (auto& i : all) if (i.base.empty() && i.first("Mesh")) byId[i.id] = &i;
        int withMesh = 0, found = 0;
        for (auto& i : all) {
            const oc::ScrValue* m = i.first("Mesh");
            if (!m && !i.base.empty() && byId.count(i.base)) m = byId[i.base]->first("Mesh");
            if (!m) continue;
            withMesh++;
            std::string n = m->text;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".msh") == 0) n.resize(n.size() - 4);
            found += meshes.count(n) ? 1 : 0;
        }
        printf("items with a mesh (own or inherited) %d, found as mesh resource %d\n", withMesh, found);
        printf("derived items %d, base defined as a plain item %d, with own Mesh %d\n", derived, resolved, ownMesh);
        printf("%d items (%zu distinct ids, %d redefinitions)\n", total, seen.size(), dup);
        for (auto& c : cats) printf("  %-34s %d\n", c.first.c_str(), c.second);
        printf("most common properties:\n");
        std::vector<std::pair<int, std::string>> v;
        for (auto& pr : props) v.push_back({pr.second, pr.first});
        std::sort(v.rbegin(), v.rend());
        for (size_t i = 0; i < v.size() && i < 30; i++) printf("  %-28s %d\n", v[i].second.c_str(), v[i].first);
    }
    return 0;
}
