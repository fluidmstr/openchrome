# Sound banks (`Data/*.csb`, FMOD FSB5)

A `.csb` is a table followed by FSB5 blobs. Verified on `menu.csb`, `music_1.csb`, `all_in_maps_persistant.csb`.

Table (from offset 64 up to the first blob): 88-byte records `u32 offset, u32 size, u32 1|2 (channels?), u32 samples, u32 milliseconds, u32 0, char name[64]`; `offset` points at an `FSB5` magic. The table starts with a 12-byte header (`0x4c, record count, 2`) and the name that follows it; whether a name belongs to the record it sits in or to its neighbour is not settled (the last record of `menu.csb` has an empty name while the first header carries one).

FSB5 blob (version 1): `'FSB5', version, numSamples (1 here), sampleHeaderSize (36), nameTableSize (0), dataSize, mode`, 8 zero bytes, 16-byte hash, 8 bytes, then the 8-byte sample header: bit 0 extra params, bits 1-4 frequency (`0:4000 1:8000 2:11000 3:11025 4:16000 5:22050 6:24000 7:32000 8:44100 9:48000 10:96000`), bits 5-6 channels-1, bits 7-33 data offset / 16, bits 34-63 sample count. Data follows the header block.

Codecs seen: mode 7 = IMA ADPCM (music, `music_*.csb`, 48 kHz stereo), mode 15 = Vorbis (effects, `all_in_maps_*.csb`, `menu.csb`).

IMA ADPCM (verified: bit-exact in two implementations, spectrum decays like music): frames of `36 * channels` bytes = per channel a 4-byte header (s16 predictor, u8 step index, pad), then 32 bytes per channel interleaved in 4-byte units (unit u of channel c at `u * channels + c`), 8 samples per unit, low nibble first; 64 samples per channel and frame. Standard IMA step table and index deltas.

Not decoded: FMOD Vorbis. The setup header is not in the file (only its CRC in an extra chunk), so a codebook table would be needed; effects stay silent for now.

Tools: `oc_soundstat <csb> [name out.wav]`, `tools/fsb.py <csb> [filter] [-x outdir]`, `oc_viewer ... --music <name>`.
