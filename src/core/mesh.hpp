// Mesh resource decoding, see docs/formats/mesh.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/rpack.hpp"

namespace oc {

struct MeshGroup {
    std::vector<float> pos;        // xyz per vertex
    std::vector<float> uv;         // uv per vertex, empty if the vertex layout is unknown
    std::vector<uint32_t> normal;  // packed snorm8 xyz(+pad) per vertex, empty if the layout is unknown
    std::vector<uint32_t> index;   // triangle list, local to the group
    std::vector<uint32_t> counts;  // index count per submesh
    int stride = 0;
};

struct Mesh {
    std::vector<MeshGroup> groups;  // group 0 is LOD0 / first part
    std::vector<std::string> materials;
};

// Returns false if the resource does not decode (unknown layout).
bool loadMesh(Pack& pack, const Resource& r, Mesh& out);

}  // namespace oc
