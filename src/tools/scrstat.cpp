// Parses every *.scr in DW/Data*.pak and prints how many files parse without skipped constructs.
#include <cstdio>
#include <filesystem>
#include <map>

#include "core/script.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

static void count(const std::vector<oc::ScrNode>& v, std::map<std::string, int>& calls, size_t& nodes) {
    for (auto& n : v) {
        nodes++;
        if (n.kind == oc::ScrNode::Call) calls[n.name]++;
        count(n.kids, calls, nodes);
    }
}

static void findSkipped(const std::vector<oc::ScrNode>& v, std::string& first) {
    for (auto& n : v) {
        if (!first.empty()) return;
        if (n.kind == oc::ScrNode::Skipped) { first = n.name + "@" + std::to_string(n.line); return; }
        findSkipped(n.kids, first);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_scrstat <DW dir> [-v]\n"); return 1; }
    bool verbose = argc > 2;
    int files = 0, bad = 0, binary = 0;
    size_t nodes = 0;
    std::map<std::string, int> calls;
    for (auto& e : fs::directory_iterator(argv[1])) {
        std::string p = e.path().string();
        if (e.path().extension() != ".pak" || e.path().filename().string().rfind("Data", 0) != 0) continue;
        for (auto& name : oc::listZip(p)) {
            if (name.size() < 4 || name.substr(name.size() - 4) != ".scr" || name.find("editor/") != std::string::npos) continue;
            std::vector<uint8_t> b;
            if (!oc::readZipEntry(p, name, b) || b.empty()) continue;
            if (b.size() >= 2 && b[0] == 0xff && b[1] == 0xfe) continue;  // UTF-16
            size_t ctl = 0;
            for (uint8_t c : b) ctl += c < 9;
            if (ctl) { binary++; continue; }
            auto f = oc::parseScript(std::string(b.begin(), b.end()));
            files++;
            count(f.nodes, calls, nodes);
            if (f.errors) {
                bad++;
                if (verbose) {
                    std::string first;
                    findSkipped(f.nodes, first);
                    printf("%s: %d skipped, first %s\n", name.c_str(), f.errors, first.c_str());
                }
            }
        }
    }
    printf("%d binary files ignored\n%d files, %zu nodes, %d with skipped constructs, %zu distinct calls\n", binary, files, nodes, bad, calls.size());
    return 0;
}
