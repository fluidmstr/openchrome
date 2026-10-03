// FMOD sound banks (*.csb): a table of named FSB5 blobs. Spec: docs/formats/audio.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

struct SoundEntry {
    std::string name;
    uint64_t offset = 0, size = 0;  // FSB5 blob inside the file
    int codec = 0;                  // FSB5 mode: 7 = IMA ADPCM (music), 15 = Vorbis (effects)
    int rate = 0, channels = 0;
    uint64_t samples = 0;
};

// Lists the entries of a .csb file; empty when the file does not look like one.
std::vector<SoundEntry> listSounds(const std::string& path);

// Decodes an IMA ADPCM entry to interleaved s16 PCM. Returns false for other codecs.
bool decodeSound(const std::string& path, const SoundEntry& e, std::vector<int16_t>& pcm);

}  // namespace oc
