// Skeleton, static clip poses and CPU skinning, see docs/formats/anim.md
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/rpack.hpp"

namespace oc {

using Mat34 = std::array<float, 12>;  // row-major 3x4 [R|t], column vectors

struct Skeleton {
    std::vector<std::string> names;
    std::vector<uint32_t> hashes;  // boneHash(name)
    std::vector<int> parent;       // -1 for roots
    std::vector<Mat34> local, invBind;
};

// ANM2 bone name hash: h = h * 41 + c
uint32_t boneHash(const std::string& name);

// Skeleton stored in the meta chunk of a skinned mesh; false if the mesh has none.
bool loadSkeleton(View meta, View rel, Skeleton& out);

// Local transform of every bone of a fully static clip (stand poses etc.), keyed by bone hash.
struct Pose {
    std::vector<uint32_t> hashes;
    std::vector<float> trs;  // 9 per bone: quaternion xyz, position xyz, scale xyz
};
// false for clips with animated channels (bit-packed stream not decoded) or other formats
bool loadStaticPose(Pack& pack, const Resource& r, Pose& out);

// Skin matrices (world pose * inverse bind) for every bone; bones missing in the pose keep their bind pose.
std::vector<Mat34> skinMatrices(const Skeleton& s, const Pose* pose);

}  // namespace oc
