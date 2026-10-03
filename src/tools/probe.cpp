// oc_probe <DW dir> <map> [max types]: parse a map and decode the meshes it uses (smoke test for oc_core)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

#include "core/mesh.hpp"
#include "core/rpack.hpp"
#include "core/sobj.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: oc_probe <DW dir> <map> [max types]\n");
        return 1;
    }
    fs::path dw = argv[1];
    std::string map = argv[2];
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> blob;
    if (!oc::readZipEntry((dw / "Data2.pak").string(), "data/maps/" + map + "/" + map + ".sobj", blob)) {
        fprintf(stderr, "no sobj\n");
        return 1;
    }
    oc::StaticObjects so = oc::parseSobj(blob);
    printf("%zu types, %zu instances\n", so.types.size(), so.instances.size());

    // mesh name -> (pack, resource) over all packs
    std::vector<std::unique_ptr<oc::Pack>> packs;
    std::map<std::string, std::pair<oc::Pack*, const oc::Resource*>> index;
    for (auto& e : fs::directory_iterator(dw / "Data")) {
        if (e.path().extension() != ".rpack") continue;
        try {
            packs.push_back(std::make_unique<oc::Pack>(e.path().string()));
        } catch (std::exception& ex) {
            fprintf(stderr, "%s\n", ex.what());
            continue;
        }
        for (auto& r : packs.back()->resources())
            if (r.flags == oc::TYPE_MESH) index.emplace(r.name, std::make_pair(packs.back().get(), &r));
    }
    printf("%zu meshes indexed in %zu packs\n", index.size(), packs.size());
    size_t ok = 0, bad = 0, missing = 0, tris = 0;
    std::set<uint16_t> used;
    for (auto& i : so.instances) used.insert(i.type);
    int limit = argc > 3 ? atoi(argv[3]) : 200;
    for (uint16_t t : used) {
        if (limit-- <= 0) break;
        std::string name = so.types[t].mesh;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".msh") name.resize(name.size() - 4);
        auto it = index.find(name);
        if (it == index.end()) { missing++; continue; }
        oc::Mesh m;
        try {
            if (oc::loadMesh(*it->second.first, *it->second.second, m)) {
                ok++;
                tris += m.groups[0].index.size() / 3;
            } else {
                bad++;
            }
        } catch (std::exception&) {
            bad++;
        }
    }
    printf("decoded %zu meshes (%zu failed, %zu not in packs), %zu LOD0 triangles, %.1fs\n", ok, bad, missing, tris,
           std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}
