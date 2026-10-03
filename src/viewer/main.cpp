// oc_viewer <DW dir> [map] [--shot out.ppm] [--cam x y z yaw pitch] [--radius R]
// Free-camera viewer for a map's static objects (SDL2 + Vulkan 1.3, flat shaded, no textures yet).
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/exp.hpp"
#include "core/items.hpp"
#include "core/mesh.hpp"
#include "core/mp.hpp"
#include "core/texture.hpp"
#include "core/rpack.hpp"
#include "core/sobj.hpp"
#include "core/zip.hpp"

namespace fs = std::filesystem;

#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (%s:%d)\n", #x, (int)r_, __FILE__, __LINE__); exit(2); } } while (0)

// ---------------------------------------------------------------- world data

struct Sub { uint32_t first, count, mat; };

struct TypeGeom {
    bool valid = false;
    float emissive = 0.0f;  // night glow strength, from the surface name ("emissive_no_shadow" window plugs, "*_ems")
    int32_t vertexOffset[2] = {0, 0};
    std::vector<Sub> subs[2];  // per LOD (0 = near, 1 = far)
};

struct GpuInstance { float pos[3], p0, scale[3], p1, quat[4]; };  // 48 bytes

struct MatInfo { std::string name, tex, dyeMask, dyePal, nrm, spc; int slot = 0; int state = 0; int nrmSlot = 0, spcSlot = 0; };  // state: 0 pending, 1 resolved

struct ItemSpawn { std::string id; float pos[3]; float yaw; };  // --item: inventory item mesh by id

struct Spawn { std::string mesh, clip; float pos[3]; float yaw; std::vector<int> parts; std::map<int, std::string> partMat; };  // --spawn: skinned character in a static pose

struct World {
    bool rich = false;   // materials created while true also get normal + specular maps (characters)
    bool nomap = false;  // --nomap: only --spawn characters (fast test scene)
    std::vector<Spawn> spawns;
    std::vector<ItemSpawn> itemSpawns;
    std::vector<float> vertices;     // x y z u v + packed snorm8 normal (uint32 bits), 6 floats per vertex
    std::vector<uint32_t> indices;
    std::vector<TypeGeom> geom;      // per object type
    std::vector<MatInfo> mats;       // material 0 is "no material"
    std::map<std::string, uint32_t> matIndex;
    oc::StaticObjects objects;
    std::vector<oc::Light> lights;
    std::map<std::string, std::pair<oc::Pack*, const oc::Resource*>> textures;
    std::unique_ptr<oc::MaterialDb> db;
};

static bool skipType(const std::string& mesh) {
    static const char* skip[] = {"blood", "decal", "dummy", "dead_body", "trigger", "collision", "physics", "occluder", "light_shaft"};
    std::string l = mesh;
    for (auto& c : l) c = (char)tolower((unsigned char)c);
    for (auto s : skip) if (l.find(s) != std::string::npos) return true;
    return false;
}

static uint32_t matId(World& w, const std::string& name) {
    auto it = w.matIndex.find(name);
    if (it != w.matIndex.end()) return it->second;
    uint32_t id = (uint32_t)w.mats.size();
    MatInfo mi{name, w.db ? w.db->diffuse(name, [&](const std::string& n) { return w.textures.count(n) > 0; }) : std::string()};
    if (w.db) {
        mi.dyeMask = w.db->sampler(name, "s_idx"); mi.dyePal = w.db->sampler(name, "s_grd");
        if (!w.textures.count(mi.dyeMask) || !w.textures.count(mi.dyePal)) mi.dyeMask = mi.dyePal = "";
    }
    if (w.db && w.rich) {
        mi.nrm = w.db->sampler(name, "s_nrm_0"); mi.spc = w.db->sampler(name, "s_spc_0");
        if (!w.textures.count(mi.nrm)) mi.nrm.clear();
        if (!w.textures.count(mi.spc)) mi.spc.clear();
    }
    w.mats.push_back(mi);
    w.matIndex[name] = id;
    return id;
}

static void appendGroup(World& w, const oc::MeshGroup& g, const std::vector<std::string>& mats, int32_t& vOff, std::vector<Sub>& subs) {
    vOff = (int32_t)(w.vertices.size() / 6);
    uint32_t base = (uint32_t)w.indices.size();
    size_t nv = g.pos.size() / 3;
    auto clean = [](float f) { return std::isfinite(f) && std::fabs(f) < 1e6f ? f : 0.0f; };
    for (size_t v = 0; v < nv; v++) {
        for (int k = 0; k < 3; k++) w.vertices.push_back(clean(g.pos[3 * v + k]));
        w.vertices.push_back(g.uv.empty() ? 0.0f : clean(g.uv[2 * v]));
        w.vertices.push_back(g.uv.empty() ? 0.0f : clean(g.uv[2 * v + 1]));
        uint32_t nb = g.normal.empty() ? 0u : g.normal[v];
        float nf;
        memcpy(&nf, &nb, 4);
        w.vertices.push_back(nf);
    }
    w.indices.insert(w.indices.end(), g.index.begin(), g.index.end());
    uint32_t acc = 0;
    for (size_t k = 0; k < g.counts.size(); k++) {
        size_t mi = k < g.material.size() && g.material[k] < mats.size() ? g.material[k] : std::min(k, mats.size() - 1);
        uint32_t m = mats.empty() ? 0 : matId(w, mats[mi]);
        subs.push_back({base + acc, g.counts[k], m});
        acc += g.counts[k];
    }
}

// CPU-skins group 0 of a skinned mesh into `pose` (bind pose when null) and returns it as a plain static group.
static oc::MeshGroup skinGroup(const oc::Mesh& m, const oc::Pose* pose, size_t gi = 0) {
    oc::MeshGroup g = m.groups[gi];
    if (g.boneIdx.empty()) return g;
    auto sk = oc::skinMatrices(m.skeleton, pose);
    auto unpack = [](uint32_t p, int c) { int8_t b = (int8_t)((p >> (8 * c)) & 255); return std::max(-1.0f, b / 127.0f); };
    for (size_t v = 0; v < g.pos.size() / 3; v++) {
        float p[3] = {g.pos[3 * v], g.pos[3 * v + 1], g.pos[3 * v + 2]}, np[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
        float n[3] = {unpack(g.normal[v], 0), unpack(g.normal[v], 1), unpack(g.normal[v], 2)};
        float wsum = 0;
        for (int c = 0; c < 4; c++) {
            float w = g.boneW[4 * v + c] / 255.0f;
            if (w <= 0) continue;
            const oc::Mat34& M = sk[g.boneIdx[4 * v + c]];
            wsum += w;
            for (int r = 0; r < 3; r++) {
                np[r] += w * (M[4 * r] * p[0] + M[4 * r + 1] * p[1] + M[4 * r + 2] * p[2] + M[4 * r + 3]);
                nn[r] += w * (M[4 * r] * n[0] + M[4 * r + 1] * n[1] + M[4 * r + 2] * n[2]);
            }
        }
        if (wsum <= 0) continue;
        float len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        uint32_t packed = 0;
        for (int r = 0; r < 3; r++) {
            g.pos[3 * v + r] = np[r] / wsum;
            float f = len > 1e-6f ? nn[r] / len : 0.0f;
            packed |= (uint32_t)(uint8_t)(int8_t)std::lround(f * 127.0f) << (8 * r);
        }
        g.normal[v] = packed;
    }
    return g;
}

// Character meshes are kits: every group is one part (head, torso, legs, hair, LODs...). Merges the chosen groups
// (skinned into `pose`) into one group and returns the materials of their submeshes.
static oc::MeshGroup mergeParts(const oc::Mesh& m, const oc::Pose* pose, const std::vector<int>& parts, const std::map<int, std::string>& override, std::vector<std::string>& mats) {
    oc::MeshGroup out;
    std::vector<size_t> firstSub(m.groups.size() + 1, 0);
    for (size_t i = 0; i < m.groups.size(); i++) firstSub[i + 1] = firstSub[i] + m.groups[i].counts.size();
    for (int gi : parts) {
        if (gi < 0 || (size_t)gi >= m.groups.size()) continue;
        oc::MeshGroup g = skinGroup(m, pose, (size_t)gi);
        uint32_t base = (uint32_t)(out.pos.size() / 3);
        out.pos.insert(out.pos.end(), g.pos.begin(), g.pos.end());
        out.uv.resize(out.pos.size() / 3 * 2, 0.0f);
        if (!g.uv.empty()) std::copy(g.uv.begin(), g.uv.end(), out.uv.end() - g.uv.size());
        out.normal.resize(out.pos.size() / 3, 0);
        if (!g.normal.empty()) std::copy(g.normal.begin(), g.normal.end(), out.normal.end() - g.normal.size());
        for (uint32_t i : g.index) out.index.push_back(base + i);
        out.counts.insert(out.counts.end(), g.counts.begin(), g.counts.end());
        auto ov = override.find(gi);
        for (size_t k = 0; k < g.counts.size(); k++) mats.push_back(ov != override.end() ? ov->second : k < g.material.size() && g.material[k] < m.materials.size() ? m.materials[g.material[k]] : std::string());
    }
    return out;
}

// Mesh name of an inventory item (own, or inherited through the chain of base items); empty if unknown.
static std::string itemMesh(const fs::path& dw, const std::vector<std::string>& ids, std::map<std::string, std::string>& out) {
    std::map<std::string, oc::ItemDef> defs;
    for (auto& e : fs::directory_iterator(dw)) {
        std::string p = e.path().string();
        if (e.path().extension() != ".pak" || e.path().filename().string().rfind("Data", 0) != 0) continue;
        for (auto& name : oc::listZip(p)) {
            if (name.rfind("data/scripts/", 0) != 0 || name.size() < 4 || name.substr(name.size() - 4) != ".scr") continue;
            std::vector<uint8_t> b;
            if (!oc::readZipEntry(p, name, b) || b.empty() || b[0] == 0xff) continue;
            std::vector<oc::ItemDef> items;
            oc::collectItems(oc::parseScript(std::string(b.begin(), b.end())), items);
            for (auto& it : items) {
                auto f = defs.find(it.id);
                if (f == defs.end() || (!f->second.first("Mesh") && it.first("Mesh"))) defs[it.id] = it;
            }
        }
    }
    for (auto& id : ids) {
        std::string cur = id;
        for (int hop = 0; hop < 8 && defs.count(cur); hop++) {
            const oc::ItemDef& d = defs[cur];
            if (const oc::ScrValue* m = d.first("Mesh")) { out[id] = m->text; break; }
            if (d.base.empty()) break;
            cur = d.base;
        }
    }
    return {};
}

static void loadWorld(const fs::path& dw, const std::string& map, World& w) {
    std::vector<uint8_t> blob;
    if (!oc::readZipEntry((dw / "Data2.pak").string(), "data/maps/" + map + "/" + map + ".sobj", blob)) {
        fprintf(stderr, "map %s not found\n", map.c_str());
        exit(1);
    }
    w.objects = oc::parseSobj(blob);
    if (w.nomap) w.objects.instances.clear();
    {
        std::vector<uint8_t> exp;
        if (oc::readZipEntry((dw / "Data2.pak").string(), "data/maps/" + map + "/" + map + ".exp", exp)) w.lights = oc::parseLights(exp);
        printf("%zu lights from .exp\n", w.lights.size());
    }
    static std::vector<std::unique_ptr<oc::Pack>> packs;
    std::map<std::string, std::pair<oc::Pack*, const oc::Resource*>> index, clips;
    for (auto& e : fs::directory_iterator(dw / "Data")) {
        if (e.path().extension() != ".rpack") continue;
        try { packs.push_back(std::make_unique<oc::Pack>(e.path().string())); } catch (std::exception&) { continue; }
        for (auto& r : packs.back()->resources()) {
            if (r.flags == 0x01400001) clips.emplace(r.name, std::make_pair(packs.back().get(), &r));
            else if (r.flags == oc::TYPE_MESH) index.emplace(r.name, std::make_pair(packs.back().get(), &r));
            else if (r.flags == oc::TYPE_TEXTURE_2D || r.flags == oc::TYPE_TEXTURE_CUBE) w.textures.emplace(r.name, std::make_pair(packs.back().get(), &r));
        }
    }
    try { w.db = std::make_unique<oc::MaterialDb>((dw / "Data" / "optimized_dx11.mp").string()); } catch (std::exception& ex) { fprintf(stderr, "materials: %s\n", ex.what()); }
    w.mats.push_back({"", "", "", "", "", "", 0, 1});
    // far terrain ("terrain_horizon") meshes are baked in world space and not listed in the .sobj: add them at the origin
    if (!w.nomap) {
        std::string pre1 = map + "_terrain_horizon", pre2 = map == "old_town" ? "ot_terrain_horizon" : std::string("?");
        for (auto& kv : index) {
            if (kv.first.rfind(pre1, 0) != 0 && kv.first.rfind(pre2, 0) != 0) continue;
            oc::Instance in{};
            in.scale[0] = in.scale[1] = in.scale[2] = 1.0f; in.quat[3] = 32767; in.tag = 0xffff;
            in.type = (uint16_t)w.objects.types.size();
            w.objects.types.push_back({kv.first + ".msh", "Default", "", 0});
            w.objects.instances.push_back(in);
        }
    }
    if (!w.itemSpawns.empty()) {
        std::vector<std::string> ids;
        for (auto& s : w.itemSpawns) ids.push_back(s.id);
        std::map<std::string, std::string> meshOf;
        itemMesh(dw, ids, meshOf);
        for (auto& s : w.itemSpawns) {
            auto m = meshOf.find(s.id);
            if (m == meshOf.end()) { fprintf(stderr, "item %s: no mesh found\n", s.id.c_str()); continue; }
            printf("item %s: mesh %s\n", s.id.c_str(), m->second.c_str());
            oc::Instance in{};
            in.scale[0] = in.scale[1] = in.scale[2] = 1.0f;
            float h = s.yaw * 3.14159265f / 360.0f;
            in.quat[1] = (int16_t)std::lround(std::sin(h) * 32767); in.quat[3] = (int16_t)std::lround(std::cos(h) * 32767);
            memcpy(in.pos, s.pos, 12);
            in.tag = 0xffff;
            in.type = (uint16_t)w.objects.types.size();
            w.objects.types.push_back({m->second, "Default", "", 0});
            w.objects.instances.push_back(in);
        }
    }
    for (size_t k = 0; k < w.spawns.size(); k++) {
        const Spawn& s = w.spawns[k];
        oc::Instance in{};
        in.scale[0] = in.scale[1] = in.scale[2] = 1.0f;
        float h = s.yaw * 3.14159265f / 360.0f;
        in.quat[1] = (int16_t)std::lround(std::sin(h) * 32767); in.quat[3] = (int16_t)std::lround(std::cos(h) * 32767);
        memcpy(in.pos, s.pos, 12);
        in.tag = 0xffff;
        in.type = (uint16_t)w.objects.types.size();
        w.objects.types.push_back({"spawn#" + std::to_string(k), "Default", "", 0});
        w.objects.instances.push_back(in);
    }
    w.geom.resize(w.objects.types.size());
    std::vector<char> used(w.objects.types.size(), 0);
    for (auto& i : w.objects.instances) used[i.type] = 1;
    size_t ok = 0;
    for (size_t t = 0; t < w.objects.types.size(); t++) {
        if (!used[t] || skipType(w.objects.types[t].mesh)) continue;
        std::string name = w.objects.types[t].mesh;
        if (name.rfind("spawn#", 0) == 0) {
            const Spawn& s = w.spawns[std::stoul(name.substr(6))];
            auto mi = index.find(s.mesh);
            if (mi == index.end()) { fprintf(stderr, "spawn: mesh %s not found\n", s.mesh.c_str()); continue; }
            oc::Mesh m;
            if (!oc::loadMesh(*mi->second.first, *mi->second.second, m) || m.groups[0].index.empty()) { fprintf(stderr, "spawn: %s does not decode\n", s.mesh.c_str()); continue; }
            oc::Pose pose;
            bool havePose = false;
            auto ci = clips.find(s.clip);
            if (ci != clips.end()) havePose = oc::loadStaticPose(*ci->second.first, *ci->second.second, pose);
            printf("spawn %s: %zu bones, skinned=%d, clip %s %s\n", s.mesh.c_str(), m.skeleton.names.size(), (int)!m.groups[0].boneIdx.empty(), s.clip.c_str(), havePose ? "static pose" : "bind pose");
            TypeGeom& g = w.geom[t];
            g.valid = true;
            if (s.parts.empty()) {
                size_t sub = 0;
                for (size_t gi = 0; gi < m.groups.size(); gi++) {
                    const auto& pg = m.groups[gi];
                    float lo = 1e9f, hi = -1e9f;
                    for (size_t v = 0; v < pg.pos.size() / 3; v++) { lo = std::min(lo, pg.pos[3 * v + 1]); hi = std::max(hi, pg.pos[3 * v + 1]); }
                    printf("  part %zu: %zu verts y %.2f..%.2f %s\n", gi, pg.pos.size() / 3, lo, hi, !pg.material.empty() && pg.material[0] < m.materials.size() ? m.materials[pg.material[0]].c_str() : "?");
                    sub += pg.counts.size();
                }
            }
            std::vector<std::string> partMats;
            oc::MeshGroup mg = mergeParts(m, havePose ? &pose : nullptr, s.parts.empty() ? std::vector<int>{0} : s.parts, s.partMat, partMats);
            w.rich = true;
            appendGroup(w, mg, partMats, g.vertexOffset[0], g.subs[0]);
            w.rich = false;
            g.vertexOffset[1] = g.vertexOffset[0];
            g.subs[1] = g.subs[0];
            ok++;
            continue;
        }
        if (name.size() > 4 && name.substr(name.size() - 4) == ".msh") name.resize(name.size() - 4);
        auto it = index.find(name);
        if (it == index.end()) continue;
        oc::Mesh m;
        try { if (!oc::loadMesh(*it->second.first, *it->second.second, m)) continue; } catch (std::exception&) { continue; }
        if (m.groups[0].index.empty()) continue;
        TypeGeom& g = w.geom[t];
        g.valid = true;
        {
            std::string sfc = w.objects.types[t].surface;
            for (auto& c : sfc) c = (char)tolower((unsigned char)c);
            if (sfc.find("emissive") != std::string::npos) g.emissive = 1.0f;
            else if (sfc.find("ems_strong") != std::string::npos) g.emissive = 1.5f;
            else if (sfc.find("ems") != std::string::npos) g.emissive = 0.5f;
        }
        appendGroup(w, m.groups[0], m.materials, g.vertexOffset[0], g.subs[0]);
        // far LOD: last group, but only when the groups really are a LOD chain (vertex counts shrink)
        size_t last = m.groups.size() - 1;
        bool chain = last > 0;
        for (size_t i = 1; i <= last && chain; i++) chain = m.groups[i].pos.size() <= m.groups[i - 1].pos.size() && !m.groups[i].index.empty();
        if (chain) appendGroup(w, m.groups[last], m.materials, g.vertexOffset[1], g.subs[1]);
        else { g.vertexOffset[1] = g.vertexOffset[0]; g.subs[1] = g.subs[0]; }
        ok++;
    }
    printf("map %s: %zu instances, %zu/%zu types decoded, %zu vertices, %zu indices, %zu materials\n", map.c_str(), w.objects.instances.size(), ok,
           w.objects.types.size(), w.vertices.size() / 6, w.indices.size(), w.mats.size());
}

// ---------------------------------------------------------------- vulkan helpers

struct Gfx {
    SDL_Window* window = nullptr;
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice phys{};
    VkDevice dev{};
    VkQueue queue{};
    uint32_t family = 0;
    VkSwapchainKHR swap{};
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    VkImage depth{};
    VkDeviceMemory depthMem{};
    VkImageView depthView{};
    VkCommandPool pool{};
    VkPhysicalDeviceMemoryProperties memProps{};
};

static uint32_t findMem(Gfx& g, uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g.memProps.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g.memProps.memoryTypes[i].propertyFlags & want) == want) return i;
    fprintf(stderr, "no memory type\n");
    exit(2);
}

struct Buf { VkBuffer buf{}; VkDeviceMemory mem{}; void* map = nullptr; };

static Buf makeBuffer(Gfx& g, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool mapIt = false) {
    Buf b;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK(vkCreateBuffer(g.dev, &bi, nullptr, &b.buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g.dev, b.buf, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = findMem(g, mr.memoryTypeBits, props);
    VK(vkAllocateMemory(g.dev, &ai, nullptr, &b.mem));
    VK(vkBindBufferMemory(g.dev, b.buf, b.mem, 0));
    if (mapIt) VK(vkMapMemory(g.dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
    return b;
}

static VkCommandBuffer beginOnce(Gfx& g) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = g.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(g.dev, &ai, &cb));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK(vkBeginCommandBuffer(cb, &bi));
    return cb;
}

static void endOnce(Gfx& g, VkCommandBuffer cb) {
    VK(vkEndCommandBuffer(cb));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    VK(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(g.queue));
    vkFreeCommandBuffers(g.dev, g.pool, 1, &cb);
}

// Uploads `size` bytes to a device-local buffer through a staging buffer (in 256 MB pieces).
static Buf uploadBuffer(Gfx& g, const void* data, size_t size, VkBufferUsageFlags usage) {
    if (size == 0) size = 4;
    Buf dst = makeBuffer(g, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const size_t piece = 256u << 20;
    Buf st = makeBuffer(g, std::min(size, piece), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    for (size_t off = 0; off < size; off += piece) {
        size_t n = std::min(piece, size - off);
        if (data) memcpy(st.map, (const uint8_t*)data + off, n);
        VkCommandBuffer cb = beginOnce(g);
        VkBufferCopy c{0, off, n};
        vkCmdCopyBuffer(cb, st.buf, dst.buf, 1, &c);
        endOnce(g, cb);
    }
    vkDestroyBuffer(g.dev, st.buf, nullptr);
    vkFreeMemory(g.dev, st.mem, nullptr);
    return dst;
}

static void destroySwapchain(Gfx& g) {
    vkDeviceWaitIdle(g.dev);
    for (auto v : g.views) vkDestroyImageView(g.dev, v, nullptr);
    g.views.clear();
    if (g.depthView) { vkDestroyImageView(g.dev, g.depthView, nullptr); vkDestroyImage(g.dev, g.depth, nullptr); vkFreeMemory(g.dev, g.depthMem, nullptr); g.depthView = nullptr; }
    if (g.swap) { vkDestroySwapchainKHR(g.dev, g.swap, nullptr); g.swap = nullptr; }
}

static void createSwapchain(Gfx& g) {
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps));
    int w, h;
    SDL_Vulkan_GetDrawableSize(g.window, &w, &h);
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) { SDL_Delay(50); VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps)); }
    g.extent = caps.currentExtent.width != 0xffffffff ? caps.currentExtent
             : VkExtent2D{std::clamp<uint32_t>(w, caps.minImageExtent.width, caps.maxImageExtent.width),
                          std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height)};
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fm(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &n, fm.data());
    VkSurfaceFormatKHR chosen = fm[0];
    for (auto& f : fm) if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;
    g.format = chosen.format;
    VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    si.surface = g.surface;
    si.minImageCount = std::min(caps.minImageCount + 1, caps.maxImageCount ? caps.maxImageCount : 8u);
    si.imageFormat = chosen.format; si.imageColorSpace = chosen.colorSpace; si.imageExtent = g.extent; si.imageArrayLayers = 1;
    si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; si.preTransform = caps.currentTransform;
    si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; si.presentMode = VK_PRESENT_MODE_FIFO_KHR; si.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(g.dev, &si, nullptr, &g.swap));
    vkGetSwapchainImagesKHR(g.dev, g.swap, &n, nullptr);
    g.images.resize(n);
    vkGetSwapchainImagesKHR(g.dev, g.swap, &n, g.images.data());
    g.views.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = g.images[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = g.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(g.dev, &vi, nullptr, &g.views[i]));
    }
    VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    di.imageType = VK_IMAGE_TYPE_2D; di.format = VK_FORMAT_D32_SFLOAT; di.extent = {g.extent.width, g.extent.height, 1};
    di.mipLevels = 1; di.arrayLayers = 1; di.samples = VK_SAMPLE_COUNT_1_BIT; di.tiling = VK_IMAGE_TILING_OPTIMAL;
    di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(g.dev, &di, nullptr, &g.depth));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(g.dev, g.depth, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = findMem(g, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(g.dev, &ai, nullptr, &g.depthMem));
    VK(vkBindImageMemory(g.dev, g.depth, g.depthMem, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g.depth; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(g.dev, &vi, nullptr, &g.depthView));
}

static VkShaderModule loadShader(Gfx& g, const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "missing shader %s\n", path.c_str()); exit(2); }
    std::vector<char> code((size_t)f.tellg());
    f.seekg(0);
    f.read(code.data(), code.size());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size(); ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    VK(vkCreateShaderModule(g.dev, &ci, nullptr, &m));
    return m;
}

static void imageBarrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                         VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, uint32_t levels = 1) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage; b.srcAccessMask = srcAccess; b.dstStageMask = dstStage; b.dstAccessMask = dstAccess;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = {aspect, 0, levels, 0, 1};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1; d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &d);
}

// ---------------------------------------------------------------- main

// ---------------------------------------------------------------- textures

struct GpuTex { VkImage image{}; VkDeviceMemory mem{}; VkImageView view{}; };

static GpuTex createTexture(Gfx& g, const oc::Texture& tex) {
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    switch (tex.format) {
        case oc::TexFormat::RGBA8: fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
        case oc::TexFormat::R8: fmt = VK_FORMAT_R8_UNORM; break;
        case oc::TexFormat::BC1: fmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
        case oc::TexFormat::BC2: fmt = VK_FORMAT_BC2_UNORM_BLOCK; break;
        case oc::TexFormat::BC3: fmt = VK_FORMAT_BC3_UNORM_BLOCK; break;
    }
    GpuTex out;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = {tex.mips[0].w, tex.mips[0].h, 1};
    ii.mipLevels = (uint32_t)tex.mips.size(); ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(g.dev, &ii, nullptr, &out.image));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(g.dev, out.image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = findMem(g, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(g.dev, &ai, nullptr, &out.mem));
    VK(vkBindImageMemory(g.dev, out.image, out.mem, 0));
    Buf st = makeBuffer(g, tex.data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    memcpy(st.map, tex.data.data(), tex.data.size());
    VkCommandBuffer cb = beginOnce(g);
    uint32_t levels = (uint32_t)tex.mips.size();
    imageBarrier(cb, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, levels);
    std::vector<VkBufferImageCopy> regions(levels);
    for (uint32_t i = 0; i < levels; i++) {
        regions[i] = {};
        regions[i].bufferOffset = tex.mips[i].offset;
        regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        regions[i].imageExtent = {tex.mips[i].w, tex.mips[i].h, 1};
    }
    vkCmdCopyBufferToImage(cb, st.buf, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, regions.data());
    imageBarrier(cb, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, levels);
    endOnce(g, cb);
    vkUnmapMemory(g.dev, st.mem);
    vkDestroyBuffer(g.dev, st.buf, nullptr);
    vkFreeMemory(g.dev, st.mem, nullptr);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    if (tex.format == oc::TexFormat::R8) vi.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE};
    VK(vkCreateImageView(g.dev, &vi, nullptr, &out.view));
    return out;
}

constexpr uint32_t MAX_TEX = 4096;
constexpr uint32_t SHADOW_RES = 4096;
struct FrameUbo {  // 176 bytes, one 256-byte slot per frame in flight (mirrors `Frame` in the shaders)
    glm::mat4 lightVP;
    glm::vec4 sunDir, sunColor /* w = lamps on (0..1) */, skyColor, groundColor, fogColor;
    glm::vec4 params;   // x = shadow texel, yz = light grid origin xz, w = cell size
    glm::vec4 params2;  // x = grid w, y = grid h, z = light count, w = exposure
};
struct GpuLight { glm::vec4 posRadius, colorIntensity; };

struct Draw { uint32_t indexCount, instanceCount, firstIndex; int32_t vertexOffset; uint32_t firstInstance, mat; };

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_viewer <DW dir> [map] [--shot out.ppm] [--cam x y z yaw pitch] [--radius R]\n"); return 1; }
    fs::path dw = argv[1];
    World world;
    std::string map = "old_town", shot;
    glm::vec3 camPos(300, 70, 100);
    float yaw = 0.0f, pitch = -0.25f, radius = 450.0f, hour = 15.0f;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--radius" && i + 1 < argc) radius = (float)atof(argv[++i]);
        else if (a == "--spawn" && i + 6 < argc) { world.spawns.push_back({argv[i + 1], argv[i + 2], {(float)atof(argv[i + 3]), (float)atof(argv[i + 4]), (float)atof(argv[i + 5])}, (float)atof(argv[i + 6])}); i += 6; }
        else if (a == "--item" && i + 5 < argc) { world.itemSpawns.push_back({argv[i + 1], {(float)atof(argv[i + 2]), (float)atof(argv[i + 3]), (float)atof(argv[i + 4])}, (float)atof(argv[i + 5])}); i += 5; }
        else if (a == "--nomap") world.nomap = true;
        else if (a == "--parts" && i + 1 < argc && !world.spawns.empty()) {
            // list of part[=material.mat]
            for (char* tok = strtok(argv[++i], ","); tok; tok = strtok(nullptr, ",")) {
                int part = atoi(tok);
                world.spawns.back().parts.push_back(part);
                if (const char* eq = strchr(tok, '=')) world.spawns.back().partMat[part] = eq + 1;
            }
        }
        else if (a == "--time" && i + 1 < argc) hour = (float)atof(argv[++i]);
        else if (a == "--cam" && i + 5 < argc) { camPos = {(float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3])}; yaw = (float)atof(argv[i + 4]); pitch = (float)atof(argv[i + 5]); i += 5; }
        else if (a[0] != '-') map = a;
    }

    loadWorld(dw, map, world);

    SDL_Init(SDL_INIT_VIDEO);
    Gfx g;
    g.window = SDL_CreateWindow("openchrome", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!g.window) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 2; }

    uint32_t nExt = 0;
    SDL_Vulkan_GetInstanceExtensions(g.window, &nExt, nullptr);
    std::vector<const char*> exts(nExt);
    SDL_Vulkan_GetInstanceExtensions(g.window, &nExt, exts.data());
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "openchrome"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app; ici.enabledExtensionCount = nExt; ici.ppEnabledExtensionNames = exts.data();
    VK(vkCreateInstance(&ici, nullptr, &g.instance));
    if (!SDL_Vulkan_CreateSurface(g.window, g.instance, &g.surface)) { fprintf(stderr, "surface: %s\n", SDL_GetError()); return 2; }

    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(g.instance, &nd, nullptr);
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(g.instance, &nd, devs.data());
    int bestScore = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qf.data());
        for (uint32_t i = 0; i < nq; i++) {
            VkBool32 pres = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, g.surface, &pres);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
                int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; g.phys = d; g.family = i; }
                break;
            }
        }
    }
    if (!g.phys) { fprintf(stderr, "no Vulkan 1.3 device\n"); return 2; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g.phys, &props);
    printf("GPU: %s\n", props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(g.phys, &g.memProps);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = g.family; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.descriptorIndexing = VK_TRUE; f12.descriptorBindingPartiallyBound = VK_TRUE; f12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.pNext = &f12; f13.dynamicRendering = VK_TRUE; f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures feats{};
    feats.samplerAnisotropy = VK_TRUE; feats.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f13; dci.pEnabledFeatures = &feats; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qi; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExt;
    VK(vkCreateDevice(g.phys, &dci, nullptr, &g.dev));
    vkGetDeviceQueue(g.dev, g.family, 0, &g.queue);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = g.family;
    VK(vkCreateCommandPool(g.dev, &pci, nullptr, &g.pool));
    createSwapchain(g);

    // geometry
    Buf vbuf = uploadBuffer(g, world.vertices.data(), world.vertices.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buf ibuf = uploadBuffer(g, world.indices.data(), world.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    const size_t maxInst = world.objects.instances.size();
    constexpr int FRAMES = 2;
    Buf inst[FRAMES];
    for (auto& b : inst)
        b = makeBuffer(g, std::max<size_t>(maxInst, 1) * sizeof(GpuInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

    Buf instShadow[FRAMES];
    for (auto& b : instShadow)
        b = makeBuffer(g, std::max<size_t>(maxInst, 1) * sizeof(GpuInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

    // texture descriptors: one big array, slot 0 = default gray
    VkDescriptorSetLayoutBinding dlb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEX, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorBindingFlags dbf = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo dbfi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    dbfi.bindingCount = 1; dbfi.pBindingFlags = &dbf;
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.pNext = &dbfi; dlci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT; dlci.bindingCount = 1; dlci.pBindings = &dlb;
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(g.dev, &dlci, nullptr, &dsl));
    VkDescriptorPoolSize dps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEX};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT; dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &dps;
    VkDescriptorPool dpool;
    VK(vkCreateDescriptorPool(g.dev, &dpci, nullptr, &dpool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = dpool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
    VkDescriptorSet dset;
    VK(vkAllocateDescriptorSets(g.dev, &dsai, &dset));
    VkSamplerCreateInfo sci2{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci2.magFilter = sci2.minFilter = VK_FILTER_LINEAR; sci2.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci2.addressModeU = sci2.addressModeV = sci2.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci2.anisotropyEnable = VK_TRUE; sci2.maxAnisotropy = 8.0f; sci2.maxLod = 16.0f;
    VkSampler sampler;
    VK(vkCreateSampler(g.dev, &sci2, nullptr, &sampler));
    auto setSlot = [&](uint32_t slot, VkImageView view) {
        VkDescriptorImageInfo di{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wds.dstSet = dset; wds.dstBinding = 0; wds.dstArrayElement = slot; wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wds.pImageInfo = &di;
        vkUpdateDescriptorSets(g.dev, 1, &wds, 0, nullptr);
    };
    {
        oc::Texture gray;
        gray.width = gray.height = 1; gray.format = oc::TexFormat::RGBA8; gray.mips = {{1, 1, 0, 4}}; gray.data = {150, 145, 135, 255};
        setSlot(0, createTexture(g, gray).view);
    }
    uint32_t nextSlot = 1;

    // point lights from the .exp, bucketed into a 16 m grid so the fragment shader only loops over nearby lights
    std::vector<GpuLight> gpuLights;
    for (const oc::Light& l : world.lights) {
        float r = std::clamp(std::max({l.scale[0], l.scale[1], l.scale[2]}) * 0.55f, 3.0f, 40.0f);
        gpuLights.push_back({glm::vec4(l.pos[0], l.pos[1], l.pos[2], r), glm::vec4(l.color[0], l.color[1], l.color[2], l.intensity)});
    }
    const float cellSize = 16.0f;
    float gMinX = 0, gMinZ = 0;
    int gridW = 1, gridH = 1;
    std::vector<uint32_t> cellStart{0, 0}, cellIdx{0};
    if (!gpuLights.empty()) {
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (auto& l : gpuLights) { x0 = std::min(x0, l.posRadius.x - l.posRadius.w); z0 = std::min(z0, l.posRadius.z - l.posRadius.w); x1 = std::max(x1, l.posRadius.x + l.posRadius.w); z1 = std::max(z1, l.posRadius.z + l.posRadius.w); }
        gMinX = x0; gMinZ = z0;
        gridW = (int)std::ceil((x1 - x0) / cellSize) + 1; gridH = (int)std::ceil((z1 - z0) / cellSize) + 1;
        std::vector<std::vector<uint32_t>> cells((size_t)gridW * gridH);
        for (uint32_t i = 0; i < gpuLights.size(); i++) {
            const glm::vec4& p = gpuLights[i].posRadius;
            int cx0 = (int)std::floor((p.x - p.w - gMinX) / cellSize), cx1 = (int)std::floor((p.x + p.w - gMinX) / cellSize);
            int cz0 = (int)std::floor((p.z - p.w - gMinZ) / cellSize), cz1 = (int)std::floor((p.z + p.w - gMinZ) / cellSize);
            for (int cz = cz0; cz <= cz1; cz++)
                for (int cx = cx0; cx <= cx1; cx++) {
                    auto& c = cells[(size_t)cz * gridW + cx];
                    if (c.size() < 48) c.push_back(i);
                }
        }
        cellStart.assign(cells.size() + 1, 0);
        cellIdx.clear();
        for (size_t c = 0; c < cells.size(); c++) { cellStart[c] = (uint32_t)cellIdx.size(); cellIdx.insert(cellIdx.end(), cells[c].begin(), cells[c].end()); }
        cellStart[cells.size()] = (uint32_t)cellIdx.size();
        if (cellIdx.empty()) cellIdx.push_back(0);
    } else {
        gpuLights.push_back({glm::vec4(0), glm::vec4(0)});
    }
    Buf lightBuf = uploadBuffer(g, gpuLights.data(), gpuLights.size() * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buf cellBuf = uploadBuffer(g, cellStart.data(), cellStart.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buf idxBuf = uploadBuffer(g, cellIdx.data(), cellIdx.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const size_t numLights = world.lights.size();

    // shadow map + per-frame uniforms (descriptor set 1)
    VkImage shadowImg;
    VkDeviceMemory shadowMem;
    VkImageView shadowView;
    {
        VkImageCreateInfo si2{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        si2.imageType = VK_IMAGE_TYPE_2D; si2.format = VK_FORMAT_D32_SFLOAT; si2.extent = {SHADOW_RES, SHADOW_RES, 1};
        si2.mipLevels = 1; si2.arrayLayers = 1; si2.samples = VK_SAMPLE_COUNT_1_BIT; si2.tiling = VK_IMAGE_TILING_OPTIMAL;
        si2.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; si2.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK(vkCreateImage(g.dev, &si2, nullptr, &shadowImg));
        VkMemoryRequirements smr;
        vkGetImageMemoryRequirements(g.dev, shadowImg, &smr);
        VkMemoryAllocateInfo sai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        sai.allocationSize = smr.size; sai.memoryTypeIndex = findMem(g, smr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkAllocateMemory(g.dev, &sai, nullptr, &shadowMem));
        VK(vkBindImageMemory(g.dev, shadowImg, shadowMem, 0));
        VkImageViewCreateInfo svi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        svi.image = shadowImg; svi.viewType = VK_IMAGE_VIEW_TYPE_2D; svi.format = VK_FORMAT_D32_SFLOAT;
        svi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(g.dev, &svi, nullptr, &shadowView));
    }
    VkSamplerCreateInfo shs{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    shs.magFilter = shs.minFilter = VK_FILTER_LINEAR; shs.addressModeU = shs.addressModeV = shs.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shs.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; shs.compareEnable = VK_TRUE; shs.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkSampler shadowSampler;
    VK(vkCreateSampler(g.dev, &shs, nullptr, &shadowSampler));
    Buf frameUbo = makeBuffer(g, 256 * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    VkDescriptorSetLayoutBinding sb[5] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = 5; sl.pBindings = sb;
    VkDescriptorSetLayout dsl1;
    VK(vkCreateDescriptorSetLayout(g.dev, &sl, nullptr, &dsl1));
    VkDescriptorPoolSize dps1[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}};
    VkDescriptorPoolCreateInfo dpci1{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci1.maxSets = 1; dpci1.poolSizeCount = 3; dpci1.pPoolSizes = dps1;
    VkDescriptorPool dpool1;
    VK(vkCreateDescriptorPool(g.dev, &dpci1, nullptr, &dpool1));
    VkDescriptorSetAllocateInfo dsai1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai1.descriptorPool = dpool1; dsai1.descriptorSetCount = 1; dsai1.pSetLayouts = &dsl1;
    VkDescriptorSet dset1;
    VK(vkAllocateDescriptorSets(g.dev, &dsai1, &dset1));
    {
        VkDescriptorBufferInfo bi1{frameUbo.buf, 0, sizeof(FrameUbo)};
        VkDescriptorImageInfo ii1{shadowSampler, shadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo sbi[3] = {{lightBuf.buf, 0, VK_WHOLE_SIZE}, {cellBuf.buf, 0, VK_WHOLE_SIZE}, {idxBuf.buf, 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet w1[5]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                   {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (int k = 0; k < 3; k++) {
            w1[2 + k].dstSet = dset1; w1[2 + k].dstBinding = 2 + k; w1[2 + k].descriptorCount = 1;
            w1[2 + k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w1[2 + k].pBufferInfo = &sbi[k];
        }
        w1[0].dstSet = dset1; w1[0].dstBinding = 0; w1[0].descriptorCount = 1; w1[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w1[0].pBufferInfo = &bi1;
        w1[1].dstSet = dset1; w1[1].dstBinding = 1; w1[1].descriptorCount = 1; w1[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w1[1].pImageInfo = &ii1;
        vkUpdateDescriptorSets(g.dev, 5, w1, 0, nullptr);
    }

    // pipeline
    std::string exeDir = SDL_GetBasePath();
    VkShaderModule vs = loadShader(g, exeDir + "mesh.vert.spv"), fsm = loadShader(g, exeDir + "mesh.frag.spv");
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4) + sizeof(glm::vec4) + 16};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayout setLayouts[2] = {dsl, dsl1};
    plci.setLayoutCount = 2; plci.pSetLayouts = setLayouts; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    VkPipelineLayout layout;
    VK(vkCreatePipelineLayout(g.dev, &plci, nullptr, &layout));
    VkPipelineShaderStageCreateInfo stages[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fsm; stages[1].pName = "main";
    VkVertexInputBindingDescription binds[2] = {{0, 24, VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(GpuInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription attrs[7] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {2, 0, VK_FORMAT_R8G8B8A8_SNORM, 20},
        {3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {4, 1, VK_FORMAT_R32G32B32_SFLOAT, 16}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32}, {6, 1, VK_FORMAT_R32_SFLOAT, 12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds; vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &g.format; rci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.pNext = &rci; gpi.stageCount = 2; gpi.pStages = stages; gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp; gpi.pRasterizationState = &rs; gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb; gpi.pDynamicState = &dsi; gpi.layout = layout;
    VkPipeline pipeline;
    VK(vkCreateGraphicsPipelines(g.dev, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline));
    VkPipeline shadowPipeline;
    {
        VkShaderModule svs = loadShader(g, exeDir + "shadow.vert.spv");
        VkPipelineShaderStageCreateInfo ss{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ss.stage = VK_SHADER_STAGE_VERTEX_BIT; ss.module = svs; ss.pName = "main";
        VkPipelineRasterizationStateCreateInfo srs = rs;
        srs.depthBiasEnable = VK_TRUE; srs.depthBiasConstantFactor = 2.0f; srs.depthBiasSlopeFactor = 2.5f;
        VkPipelineColorBlendStateCreateInfo scb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkPipelineRenderingCreateInfo srci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        srci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo sg = gpi;
        sg.pNext = &srci; sg.stageCount = 1; sg.pStages = &ss; sg.pRasterizationState = &srs; sg.pColorBlendState = &scb;
        VK(vkCreateGraphicsPipelines(g.dev, VK_NULL_HANDLE, 1, &sg, nullptr, &shadowPipeline));
    }

    // light glow: one additive point sprite per light, positions read from the light buffer
    VkPipeline glowPipeline;
    {
        VkShaderModule gvs = loadShader(g, exeDir + "glow.vert.spv"), gfs = loadShader(g, exeDir + "glow.frag.spv");
        VkPipelineShaderStageCreateInfo gs[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        gs[0].stage = VK_SHADER_STAGE_VERTEX_BIT; gs[0].module = gvs; gs[0].pName = "main";
        gs[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; gs[1].module = gfs; gs[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo gvi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo gia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        gia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        VkPipelineDepthStencilStateCreateInfo gds = ds;
        gds.depthWriteEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState gba{};
        gba.blendEnable = VK_TRUE; gba.colorWriteMask = 0xF;
        gba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; gba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; gba.colorBlendOp = VK_BLEND_OP_ADD;
        gba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; gba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; gba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo gcb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        gcb.attachmentCount = 1; gcb.pAttachments = &gba;
        VkGraphicsPipelineCreateInfo gg = gpi;
        gg.pStages = gs; gg.pVertexInputState = &gvi; gg.pInputAssemblyState = &gia; gg.pDepthStencilState = &gds; gg.pColorBlendState = &gcb;
        VK(vkCreateGraphicsPipelines(g.dev, VK_NULL_HANDLE, 1, &gg, nullptr, &glowPipeline));
    }

    // sky: full-screen triangle drawn first, no depth
    VkPipeline skyPipeline;
    {
        VkShaderModule kvs = loadShader(g, exeDir + "sky.vert.spv"), kfs = loadShader(g, exeDir + "sky.frag.spv");
        VkPipelineShaderStageCreateInfo ks[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        ks[0].stage = VK_SHADER_STAGE_VERTEX_BIT; ks[0].module = kvs; ks[0].pName = "main";
        ks[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; ks[1].module = kfs; ks[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo kvi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo kia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        kia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineDepthStencilStateCreateInfo kds = ds;
        kds.depthTestEnable = VK_FALSE; kds.depthWriteEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState kba{};
        kba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo kcb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        kcb.attachmentCount = 1; kcb.pAttachments = &kba;
        VkPipelineRasterizationStateCreateInfo krs = rs;
        krs.cullMode = VK_CULL_MODE_NONE;
        VkGraphicsPipelineCreateInfo kg = gpi;
        kg.pStages = ks; kg.pVertexInputState = &kvi; kg.pInputAssemblyState = &kia; kg.pDepthStencilState = &kds; kg.pColorBlendState = &kcb; kg.pRasterizationState = &krs;
        VK(vkCreateGraphicsPipelines(g.dev, VK_NULL_HANDLE, 1, &kg, nullptr, &skyPipeline));
    }

    // frame resources
    VkCommandBuffer cmds[FRAMES];
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = g.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = FRAMES;
    VK(vkAllocateCommandBuffers(g.dev, &cai, cmds));
    VkSemaphore imageAvail[FRAMES];
    VkFence fences[FRAMES];
    std::vector<VkSemaphore> renderDone;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < FRAMES; i++) { VK(vkCreateSemaphore(g.dev, &sci, nullptr, &imageAvail[i])); VK(vkCreateFence(g.dev, &fci, nullptr, &fences[i])); }
    auto makeRenderDone = [&]() {
        for (auto s : renderDone) vkDestroySemaphore(g.dev, s, nullptr);
        renderDone.resize(g.images.size());
        for (auto& s : renderDone) VK(vkCreateSemaphore(g.dev, &sci, nullptr, &s));
    };
    makeRenderDone();
    Buf shotBuf;
    if (!shot.empty()) shotBuf = makeBuffer(g, 1u << 26, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

    // per-frame culling scratch
    const size_t nb = world.geom.size() * 2;
    std::vector<uint32_t> counts(nb), starts(nb), fill(nb), vis, visBucket;
    std::vector<Draw> draws, shadowDraws;
    std::vector<uint32_t> counts2(nb), starts2(nb), fill2(nb), vis2, visBucket2;
    SDL_SetRelativeMouseMode(shot.empty() ? SDL_TRUE : SDL_FALSE);
    float speed = 40.0f;
    uint64_t last = SDL_GetPerformanceCounter();
    int frame = 0;
    bool running = true, resized = false;
    double acc = 0; int accN = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RIGHTBRACKET) hour = std::fmod(hour + 0.5f, 24.0f);
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_LEFTBRACKET) hour = std::fmod(hour + 23.5f, 24.0f);
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p) printf("--cam %.2f %.2f %.2f %.3f %.3f\n", camPos.x, camPos.y, camPos.z, yaw, pitch), fflush(stdout);
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_n) hour = (hour > 7.0f && hour < 18.0f) ? 22.0f : 14.0f;
            else if (e.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode()) { yaw += e.motion.xrel * 0.0025f; pitch = std::clamp(pitch - e.motion.yrel * 0.0025f, -1.55f, 1.55f); }
            else if (e.type == SDL_MOUSEWHEEL) speed = std::clamp(speed * (e.wheel.y > 0 ? 1.25f : 0.8f), 2.0f, 2000.0f);
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) resized = true;
        }
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        glm::vec3 fwd(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        float sp = speed * (k[SDL_SCANCODE_LSHIFT] ? 5.0f : 1.0f) * (k[SDL_SCANCODE_LCTRL] ? 0.2f : 1.0f);
        if (k[SDL_SCANCODE_W]) camPos += fwd * sp * dt;
        if (k[SDL_SCANCODE_S]) camPos -= fwd * sp * dt;
        if (k[SDL_SCANCODE_D]) camPos += right * sp * dt;
        if (k[SDL_SCANCODE_A]) camPos -= right * sp * dt;
        if (k[SDL_SCANCODE_E]) camPos.y += sp * dt;
        if (k[SDL_SCANCODE_Q]) camPos.y -= sp * dt;

        {
            int dw_, dh_;
            SDL_Vulkan_GetDrawableSize(g.window, &dw_, &dh_);
            if (dw_ == 0 || dh_ == 0) { SDL_Delay(20); continue; }  // minimized: nothing to draw
        }
        int fi = frame % FRAMES;
        VK(vkWaitForFences(g.dev, 1, &fences[fi], VK_TRUE, UINT64_MAX));
        uint32_t imgIdx;
        VkResult ar = vkAcquireNextImageKHR(g.dev, g.swap, UINT64_MAX, imageAvail[fi], VK_NULL_HANDLE, &imgIdx);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR || resized) {
            resized = false;
            destroySwapchain(g); createSwapchain(g); makeRenderDone();
            continue;
        }
        VK(vkResetFences(g.dev, 1, &fences[fi]));

        // cull + bucket instances by (type, lod)
        std::fill(counts.begin(), counts.end(), 0u);
        vis.clear(); visBucket.clear();
        const float r2 = radius * radius, lodDist2 = 90.0f * 90.0f;
        for (size_t i = 0; i < maxInst; i++) {
            const oc::Instance& in = world.objects.instances[i];
            const TypeGeom& tg = world.geom[in.type];
            if (!tg.valid) continue;
            glm::vec3 d(in.pos[0] - camPos.x, in.pos[1] - camPos.y, in.pos[2] - camPos.z);
            float d2 = glm::dot(d, d);
            if (in.tag != 0xffff && (d2 > r2 || glm::dot(d, fwd) < -40.0f)) continue;
            uint32_t b = in.type * 2 + (d2 > lodDist2 ? 1 : 0);
            vis.push_back((uint32_t)i); visBucket.push_back(b); counts[b]++;
        }
        uint32_t run = 0;
        for (size_t b = 0; b < nb; b++) { starts[b] = run; fill[b] = 0; run += counts[b]; }
        GpuInstance* dst = (GpuInstance*)inst[fi].map;
        for (size_t v = 0; v < vis.size(); v++) {
            const oc::Instance& in = world.objects.instances[vis[v]];
            GpuInstance& o = dst[starts[visBucket[v]] + fill[visBucket[v]]++];
            memcpy(o.pos, in.pos, 12); memcpy(o.scale, in.scale, 12);
            for (int c = 0; c < 4; c++) o.quat[c] = in.quat[c] / 32767.0f;
            // lit windows: a stable pseudo-random ~60% of the emissive plugs are on (the real on/off state is not decoded)
            float lit = (((vis[v] * 2654435761u) >> 8) & 255) < 154 ? 1.0f : 0.0f;
            o.p0 = world.geom[in.type].emissive * (world.geom[in.type].emissive == 1.0f ? lit : 1.0f);
        }
        draws.clear();
        for (size_t b = 0; b < nb; b++) {
            if (!counts[b]) continue;
            const TypeGeom& tg = world.geom[b / 2];
            int lod = (int)(b & 1);
            for (const Sub& s : tg.subs[lod]) draws.push_back({s.count, counts[b], s.first, tg.vertexOffset[lod], starts[b], s.mat});
        }

        // sun light matrix: one orthographic cascade around the camera, snapped to shadow texels
        // time of day: sun path over 6..18 h, a fixed dim moon otherwise; lamps fade in around dusk
        const float el = (hour - 6.0f) / 12.0f * 3.14159265f;
        const float sunH = std::sin(el);
        const float day = glm::smoothstep(-0.08f, 0.2f, sunH);
        const float az = 3.14159265f * (hour - 6.0f) / 12.0f;
        const float ce = std::sqrt(std::max(0.0f, 1.0f - std::max(sunH, 0.12f) * std::max(sunH, 0.12f)));
        const glm::vec3 sunV = glm::normalize(glm::vec3(std::cos(az) * ce, std::max(sunH, 0.12f), std::sin(az) * ce));
        const glm::vec3 moonV = glm::normalize(glm::vec3(-0.4f, 0.6f, 0.5f));
        const glm::vec3 sunDir = day > 0.35f ? sunV : moonV;
        const glm::vec3 warm = glm::mix(glm::vec3(1.0f, 0.5f, 0.28f), glm::vec3(1.0f, 0.93f, 0.80f), glm::smoothstep(0.0f, 0.45f, sunH));
        const glm::vec3 sunCol = glm::mix(glm::vec3(0.16f, 0.22f, 0.40f) * 0.5f, warm * 2.0f, day);
        const glm::vec3 skyCol = glm::mix(glm::vec3(0.02f, 0.035f, 0.09f), glm::vec3(0.42f, 0.55f, 0.78f), day);
        const glm::vec3 groundCol = glm::mix(glm::vec3(0.01f, 0.012f, 0.02f), glm::vec3(0.28f, 0.24f, 0.20f), day);
        const glm::vec3 fogCol = glm::mix(glm::vec3(0.012f, 0.02f, 0.05f), glm::vec3(0.55f, 0.65f, 0.78f), day);
        const float exposure = glm::mix(2.4f, 0.85f, day);
        const float S = 150.0f;
        glm::vec3 C = camPos + glm::vec3(fwd.x, 0.0f, fwd.z) * 70.0f;
        glm::mat4 lv = glm::lookAt(C + sunDir * 500.0f, C, glm::vec3(0, 1, 0));
        {
            float texel = 2.0f * S / (float)SHADOW_RES;
            glm::vec4 lc = lv * glm::vec4(C, 1.0f);
            glm::vec2 snapped(std::floor(lc.x / texel) * texel, std::floor(lc.y / texel) * texel);
            lv = glm::translate(glm::mat4(1.0f), glm::vec3(snapped.x - lc.x, snapped.y - lc.y, 0.0f)) * lv;
        }
        glm::mat4 lightVP = glm::ortho(-S, S, -S, S, 0.0f, 1000.0f) * lv;
        std::fill(counts2.begin(), counts2.end(), 0u);
        vis2.clear(); visBucket2.clear();
        const float sr2 = S * 1.7f * S * 1.7f;
        for (size_t i = 0; i < maxInst; i++) {
            const oc::Instance& in = world.objects.instances[i];
            if (!world.geom[in.type].valid) continue;
            glm::vec3 d(in.pos[0] - C.x, in.pos[1] - C.y, in.pos[2] - C.z);
            if (in.tag == 0xffff || glm::dot(d, d) > sr2) continue;  // horizon terrain casts no shadow
            glm::vec3 dc(in.pos[0] - camPos.x, in.pos[1] - camPos.y, in.pos[2] - camPos.z);
            uint32_t b = in.type * 2 + (glm::dot(dc, dc) > lodDist2 ? 1 : 0);
            vis2.push_back((uint32_t)i); visBucket2.push_back(b); counts2[b]++;
        }
        run = 0;
        for (size_t b = 0; b < nb; b++) { starts2[b] = run; fill2[b] = 0; run += counts2[b]; }
        GpuInstance* dst2 = (GpuInstance*)instShadow[fi].map;
        for (size_t v = 0; v < vis2.size(); v++) {
            const oc::Instance& in = world.objects.instances[vis2[v]];
            GpuInstance& o = dst2[starts2[visBucket2[v]] + fill2[visBucket2[v]]++];
            memcpy(o.pos, in.pos, 12); memcpy(o.scale, in.scale, 12);
            for (int c = 0; c < 4; c++) o.quat[c] = in.quat[c] / 32767.0f;
        }
        shadowDraws.clear();
        for (size_t b = 0; b < nb; b++) {
            if (!counts2[b]) continue;
            const TypeGeom& tg = world.geom[b / 2];
            const auto& subs = tg.subs[b & 1];
            if (subs.empty()) continue;
            shadowDraws.push_back({subs.back().first + subs.back().count - subs.front().first, counts2[b], subs.front().first, tg.vertexOffset[b & 1], starts2[b], 0});
        }
        {
            FrameUbo u;
            u.lightVP = lightVP; u.sunDir = glm::vec4(sunDir, 1.0f); u.sunColor = glm::vec4(sunCol, 1.0f - day);
            u.skyColor = glm::vec4(skyCol, 0); u.groundColor = glm::vec4(groundCol, 0); u.fogColor = glm::vec4(fogCol, 0);
            u.params = glm::vec4(1.0f / SHADOW_RES, gMinX, gMinZ, cellSize);
            u.params2 = glm::vec4((float)gridW, (float)gridH, (float)numLights, exposure);
            memcpy((uint8_t*)frameUbo.map + 256 * fi, &u, sizeof u);
        }

        // stream in a few diffuse textures per frame for materials that are on screen
        int budget = shot.empty() ? 6 : 400;
        for (const Draw& d : draws) {
            MatInfo& m = world.mats[d.mat];
            if (m.state || budget <= 0 || nextSlot >= MAX_TEX) continue;
            m.state = 1;
            auto it = world.textures.find(m.tex);
            if (m.tex.empty() || it == world.textures.end()) continue;
            oc::Texture tex;
            try { if (!oc::loadTexture(*it->second.first, *it->second.second, 1024, tex)) continue; } catch (std::exception&) { continue; }
            budget--;
            if (!m.dyeMask.empty()) {
                oc::Texture mask, pal, dyed;
                auto im = world.textures.find(m.dyeMask), ip = world.textures.find(m.dyePal);
                try {
                    if (oc::loadTexture(*im->second.first, *im->second.second, 1024, mask) && oc::loadTexture(*ip->second.first, *ip->second.second, 1024, pal) &&
                        oc::tintTexture(tex, mask, pal, 0, dyed)) tex = std::move(dyed);
                } catch (std::exception&) {}
            }
            setSlot(nextSlot, createTexture(g, tex).view);
            m.slot = (int)nextSlot++;
            auto extra = [&](const std::string& n, int& slot) {
                auto e = world.textures.find(n);
                oc::Texture x;
                if (n.empty() || e == world.textures.end() || nextSlot >= MAX_TEX) return;
                try { if (!oc::loadTexture(*e->second.first, *e->second.second, 1024, x)) return; } catch (std::exception&) { return; }
                setSlot(nextSlot, createTexture(g, x).view);
                slot = (int)nextSlot++;
            };
            extra(m.nrm, m.nrmSlot);
            extra(m.spc, m.spcSlot);
        }

        VkCommandBuffer cmd = cmds[fi];
        VK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK(vkBeginCommandBuffer(cmd, &bi));
        // shadow pass
        imageBarrier(cmd, shadowImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        {
            VkRenderingAttachmentInfo sda{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            sda.imageView = shadowView; sda.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL; sda.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            sda.storeOp = VK_ATTACHMENT_STORE_OP_STORE; sda.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo sri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            sri.renderArea = {{0, 0}, {SHADOW_RES, SHADOW_RES}}; sri.layerCount = 1; sri.pDepthAttachment = &sda;
            vkCmdBeginRendering(cmd, &sri);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
            VkViewport svp{0, 0, (float)SHADOW_RES, (float)SHADOW_RES, 0, 1};
            VkRect2D ssc{{0, 0}, {SHADOW_RES, SHADOW_RES}};
            vkCmdSetViewport(cmd, 0, 1, &svp);
            vkCmdSetScissor(cmd, 0, 1, &ssc);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof lightVP, &lightVP);
            VkBuffer svbs[2] = {vbuf.buf, instShadow[fi].buf};
            VkDeviceSize soffs[2] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, svbs, soffs);
            vkCmdBindIndexBuffer(cmd, ibuf.buf, 0, VK_INDEX_TYPE_UINT32);
            for (const Draw& d : shadowDraws) vkCmdDrawIndexed(cmd, d.indexCount, d.instanceCount, d.firstIndex, d.vertexOffset, d.firstInstance);
            vkCmdEndRendering(cmd);
        }
        imageBarrier(cmd, shadowImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        imageBarrier(cmd, g.images[imgIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        imageBarrier(cmd, g.depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageView = g.views[imgIdx]; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE; ca.clearValue.color = {{glm::mix(0.04f, 0.72f, day), glm::mix(0.06f, 0.80f, day), glm::mix(0.10f, 0.88f, day), 1.0f}};
        da.imageView = g.depthView; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL; da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; da.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, g.extent}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca; ri.pDepthAttachment = &da;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport viewport{0, 0, (float)g.extent.width, (float)g.extent.height, 0, 1};
        VkRect2D sc{{0, 0}, g.extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        struct { glm::mat4 vp; glm::vec4 cam; uint32_t tex; uint32_t nrmSpc; uint32_t pad[2]; } pc{};
        glm::mat4 proj = glm::perspective(glm::radians(65.0f), (float)g.extent.width / (float)g.extent.height, 0.3f, std::max(radius * 2.0f, 20000.0f));
        proj[1][1] *= -1.0f;
        pc.vp = proj * glm::lookAt(camPos, camPos + fwd, glm::vec3(0, 1, 0));
        pc.cam = glm::vec4(camPos, 1.2f / radius);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        VkBuffer vbs[2] = {vbuf.buf, inst[fi].buf};
        VkDeviceSize offs[2] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs);
        vkCmdBindIndexBuffer(cmd, ibuf.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &dset, 0, nullptr);
        uint32_t uboOff = 256u * (uint32_t)fi;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &dset1, 1, &uboOff);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        for (const Draw& d : draws) {
            pc.tex = (uint32_t)world.mats[d.mat].slot;
            pc.nrmSpc = (uint32_t)world.mats[d.mat].nrmSlot | (uint32_t)world.mats[d.mat].spcSlot << 16;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            vkCmdDrawIndexed(cmd, d.indexCount, d.instanceCount, d.firstIndex, d.vertexOffset, d.firstInstance);
        }
        if (numLights && day < 0.98f) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glowPipeline);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            vkCmdDraw(cmd, (uint32_t)numLights, 1, 0, 0);
        }
        vkCmdEndRendering(cmd);
        bool takeShot = !shot.empty() && frame == 8;
        if (takeShot) {
            imageBarrier(cmd, g.images[imgIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy bc{};
            bc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; bc.imageExtent = {g.extent.width, g.extent.height, 1};
            vkCmdCopyImageToBuffer(cmd, g.images[imgIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shotBuf.buf, 1, &bc);
            imageBarrier(cmd, g.images[imgIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        } else {
            imageBarrier(cmd, g.images[imgIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        }
        VK(vkEndCommandBuffer(cmd));
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &imageAvail[fi]; si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderDone[imgIdx];
        VK(vkQueueSubmit(g.queue, 1, &si, fences[fi]));
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone[imgIdx]; pi.swapchainCount = 1; pi.pSwapchains = &g.swap; pi.pImageIndices = &imgIdx;
        VkResult pr = vkQueuePresentKHR(g.queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) resized = true;
        if (takeShot) {
            vkQueueWaitIdle(g.queue);
            std::ofstream f(shot, std::ios::binary);
            f << "P6\n" << g.extent.width << " " << g.extent.height << "\n255\n";
            const uint8_t* px = (const uint8_t*)shotBuf.map;
            bool bgr = g.format == VK_FORMAT_B8G8R8A8_UNORM || g.format == VK_FORMAT_B8G8R8A8_SRGB;
            std::vector<char> row(g.extent.width * 3);
            for (uint32_t y = 0; y < g.extent.height; y++) {
                for (uint32_t x = 0; x < g.extent.width; x++) {
                    const uint8_t* p = px + ((size_t)y * g.extent.width + x) * 4;
                    row[3 * x] = (char)(bgr ? p[2] : p[0]); row[3 * x + 1] = (char)p[1]; row[3 * x + 2] = (char)(bgr ? p[0] : p[2]);
                }
                f.write(row.data(), row.size());
            }
            printf("screenshot %s: %zu instances, %zu draws\n", shot.c_str(), vis.size(), draws.size());
            running = false;
        }
        acc += dt; accN++;
        if (accN == 120 && shot.empty()) {
            char title[160];
            snprintf(title, sizeof title, "openchrome  %.1f fps  %zu instances  %zu draws", accN / acc, vis.size(), draws.size());
            SDL_SetWindowTitle(g.window, title);
            acc = 0; accN = 0;
        }
        frame++;
    }
    vkDeviceWaitIdle(g.dev);
    SDL_Quit();
    return 0;
}
