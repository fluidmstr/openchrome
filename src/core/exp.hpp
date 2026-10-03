// Map entity database (.exp): LightObject records, see docs/formats/exp.md
#pragma once
#include <cstdint>
#include <vector>

namespace oc {

struct Light {
    float pos[3];
    float color[3];
    float intensity;
    float scale[3];   // size of the light volume (row lengths of the 3x3 part of the transform)
    uint32_t kind[3]; // LDat words 1..3, meaning not decoded (point/spot/area variants)
};

// Scans the blob for LightObject records. Never throws; malformed records are skipped.
std::vector<Light> parseLights(const std::vector<uint8_t>& data);

}  // namespace oc
