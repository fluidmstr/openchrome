// Map static objects, see docs/formats/sobj.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

struct ObjType { std::string mesh, surface, tmpl; uint16_t flags; };

struct Instance {
    float pos[3], scale[3];
    int16_t quat[4];  // x y z w, divide by 32767
    uint16_t tag, type;
};

struct StaticObjects {
    std::vector<ObjType> types;
    std::vector<Instance> instances;
};

// Parses the .sobj blob; throws std::runtime_error on mismatch.
StaticObjects parseSobj(const std::vector<uint8_t>& data);

}  // namespace oc
