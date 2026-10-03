// optimized_dx11.mp (ABDM) material database, see docs/formats/mp.md
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oc {

class MaterialDb {
public:
    explicit MaterialDb(const std::string& path);  // throws std::runtime_error
    // Name of the texture resource used as albedo by a "*.mat" material, empty if unknown. `exists` tells whether
    // a texture resource of that name is available; names derived from normal/spec maps are only returned if it is.
    // texture bound to one sampler of material `mat`, empty if unknown
    std::string sampler(const std::string& mat, const char* name) const;
    std::string diffuse(const std::string& mat, const std::function<bool(const std::string&)>& exists) const;
    size_t materialCount() const { return blobs_.size(); }

private:
    std::vector<uint8_t> file_;
    std::unordered_map<uint32_t, std::string> dds_;                  // string key -> texture resource name
    std::unordered_map<std::string, uint32_t> matKey_;                // .mat name -> key
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> blobs_;  // material key -> offset,size
    std::unordered_map<uint32_t, std::string> samplerName_;           // string key -> "s_*" shader sampler name
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> templates_;  // template key -> offset,size
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> samplers_;  // hl shader key -> (sampler key, flags)
    // texture bound to the first existing sampler among `names` (see docs/formats/mp.md), empty if not found
    std::string bound(const std::vector<uint8_t>& blob, std::initializer_list<const char*> names) const;
};

}  // namespace oc
