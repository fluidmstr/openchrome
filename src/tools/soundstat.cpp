// `oc_soundstat <file.csb>` lists the sounds of a bank (name, codec, rate, channels, length);
// `oc_soundstat <file.csb> <name> <out.wav>` decodes one IMA ADPCM entry to a WAV file.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>

#include "core/fsb.hpp"

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oc_soundstat <file.csb> [name out.wav]\n"); return 1; }
    auto list = oc::listSounds(argv[1]);
    if (argc < 4) {
        std::map<int, int> codecs;
        for (auto& e : list) {
            codecs[e.codec]++;
            printf("%-44s codec %2d %5d Hz %dch %7.1f s\n", e.name.c_str(), e.codec, e.rate, e.channels, e.rate ? (double)e.samples / e.rate : 0.0);
        }
        printf("%zu sounds;", list.size());
        for (auto& c : codecs) printf(" codec %d: %d", c.first, c.second);
        printf("\n");
        return 0;
    }
    for (auto& e : list) {
        if (e.name != argv[2]) continue;
        std::vector<int16_t> pcm;
        if (!oc::decodeSound(argv[1], e, pcm)) { fprintf(stderr, "codec %d is not decoded\n", e.codec); return 1; }
        FILE* f = fopen(argv[3], "wb");
        uint32_t bytes = (uint32_t)(pcm.size() * 2), riff = 36 + bytes, rate = (uint32_t)e.rate, brate = rate * e.channels * 2, fmtLen = 16;
        uint16_t tag = 1, ch = (uint16_t)e.channels, align = (uint16_t)(e.channels * 2), bits = 16;
        fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
        fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f);
        fclose(f);
        printf("%s: %zu samples written\n", e.name.c_str(), pcm.size() / e.channels);
        return 0;
    }
    fprintf(stderr, "no sound named %s\n", argv[2]);
    return 1;
}
