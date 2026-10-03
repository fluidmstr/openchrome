// optimized_dx11.mp (ABDM) material database, see docs/formats/mp.md
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace oc {

class MaterialDb {
public:
    explicit MaterialDb(const std::string& path);  // throws std::runtime_error
    // Name of the texture resource used as diffuse map by a "*.mat" material, empty if unknown.
    std::string diffuse(const std::string& mat) const;
    size_t materialCount() const { return blobs_.size(); }

private:
    std::vector<uint8_t> file_;
    std::unordered_map<uint32_t, std::string> dds_;                  // string key -> texture resource name
    std::unordered_map<std::string, uint32_t> matKey_;                // .mat name -> key
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> blobs_;  // material key -> offset,size
};

}  // namespace oc
