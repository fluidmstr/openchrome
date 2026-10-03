// RP6L container reader, see docs/formats/rpack.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

struct Chunk { uint32_t part, offset, size; };  // part & 0xff = stream index

struct Resource {
    uint32_t index = 0;
    std::string name;
    uint32_t flags = 0;  // type
    std::vector<Chunk> chunks;
};

struct View { const uint8_t* data = nullptr; size_t size = 0; };

enum : uint8_t { ROLE_META = 0x10, ROLE_REL = 0x11, ROLE_VB = 0xf0, ROLE_IB = 0xf1 };
constexpr uint32_t TYPE_MESH = 0x01100005;
constexpr uint32_t TYPE_TEXTURE_2D = 0x21200002, TYPE_TEXTURE_CUBE = 0x21200003;

class Pack {
public:
    explicit Pack(const std::string& path);  // throws std::runtime_error
    const std::vector<Resource>& resources() const { return res_; }
    const std::string& path() const { return path_; }
    // chunk of `r` living in a stream with the given role (empty View if none)
    View chunk(const Resource& r, uint8_t role);
    // data of one specific chunk of a resource
    View chunkData(const Chunk& c);
private:
    struct Stream { uint32_t flags, offset, usize, csize; std::vector<uint8_t> data; bool loaded = false; };
    const std::vector<uint8_t>& stream(size_t i);
    std::string path_;
    std::vector<Stream> streams_;
    std::vector<Resource> res_;
};

}  // namespace oc
