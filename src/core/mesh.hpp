// Mesh resource decoding, see docs/formats/mesh.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/rpack.hpp"
#include "core/skin.hpp"

namespace oc {

struct MeshGroup {
    std::vector<float> pos;        // xyz per vertex
    std::vector<float> uv;         // uv per vertex, empty if the vertex layout is unknown
    std::vector<uint32_t> normal;  // packed snorm8 xyz(+pad) per vertex, empty if the layout is unknown
    std::vector<uint32_t> index;   // triangle list, local to the group
    std::vector<uint32_t> counts;  // index count per submesh
    std::vector<uint16_t> material;  // per submesh: index into Mesh::materials
    std::vector<uint16_t> boneIdx;  // 4 skeleton bone indices per vertex (skinned stride-40 groups only)
    std::vector<uint8_t> boneW;     // 4 weights (unorm8) per vertex
    int stride = 0;
};

struct Mesh {
    std::vector<MeshGroup> groups;  // group 0 is LOD0 / first part
    std::vector<std::string> materials;
    Skeleton skeleton;  // empty for static meshes
};

// Returns false if the resource does not decode (unknown layout).
bool loadMesh(Pack& pack, const Resource& r, Mesh& out);

}  // namespace oc
