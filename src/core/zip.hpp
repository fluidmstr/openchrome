// Minimal ZIP reader for DW/Data*.pak (stored/deflate, no ZIP64)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oc {
// Returns true and fills `out` with the uncompressed file `name`; false if absent.
bool readZipEntry(const std::string& pak, const std::string& name, std::vector<uint8_t>& out);
std::vector<std::string> listZip(const std::string& pak);
}  // namespace oc
