// Map entity database (.exp): LightObject records, see docs/formats/exp.md
#pragma once
#include <cstdint>
#include <string>
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

// Any placed entity: class-tagged record with a name (property 0x16) and a 3x4 row-major transform (property 0xdf, translation in column 3).
struct Entity {
    std::string cls, name;
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    bool hasTransform = false;
};

// Scans for `[u16 len]Class=` record markers; records without a name keep an empty name. Never throws.
std::vector<Entity> parseEntities(const std::vector<uint8_t>& data);

}  // namespace oc
