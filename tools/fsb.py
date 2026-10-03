"""FSB5 banks inside *.csb (FMOD). Spec: docs/formats/audio.md. Usage: fsb.py <csb> [name filter] [-x outdir]"""
import mmap, re, struct, sys, wave
import numpy as np

RATES = {0: 4000, 1: 8000, 2: 11000, 3: 11025, 4: 16000, 5: 22050, 6: 24000, 7: 32000, 8: 44100, 9: 48000, 10: 96000}
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]

def banks(m):
    """-> [(name, fsb offset, fsb size)]; records are 88 bytes: offset, size, 2, samples, ms, 0, name."""
    first = m.find(b'FSB5')
    tab = m[:first]
    out = []
    for f in re.finditer(b'FSB5', m):
        p = f.start()
        for i in range(0, len(tab) - 4, 4):
            if struct.unpack_from('<I', tab, i)[0] == p and struct.unpack_from('<I', tab, i + 8)[0] == 2:
                out.append((tab[i + 24:i + 60].split(b'\0')[0].decode('latin1'), p, struct.unpack_from('<I', tab, i + 4)[0])); break
    return out

def header(m, off):
    ver, n, sh, nm, dsz, mode = struct.unpack_from('<6I', m, off + 4)
    x = struct.unpack_from('<Q', m, off + 60)[0]
    rate = RATES.get((x >> 1) & 15, 44100); ch = ((x >> 5) & 3) + 1
    return dict(n=n, mode=mode, rate=rate, ch=ch, samples=x >> 34, data=off + 60 + sh + nm + ((x >> 7) & 0x7ffffff) * 16, dsz=dsz)

def ima(data, ch, samples):
    """FMOD IMA (MS-IMA like): frames of 36*ch bytes = one 4-byte header per channel (s16 predictor, u8 index, pad),
    then 32 bytes per channel interleaved in 4-byte units (8 samples, low nibble first) = 64 samples per channel."""
    nfr = len(data) // (36 * ch)
    out = np.zeros((nfr * 64, ch), np.int16)
    for f in range(nfr):
        fr = data[f * 36 * ch:(f + 1) * 36 * ch]
        for c in range(ch):
            pred, idx = struct.unpack_from('<hB', fr, 4 * c)
            for i in range(64):
                u, k = i // 8, i % 8
                byte = fr[4 * ch + (u * ch + c) * 4 + k // 2]
                nib = (byte >> (4 * (k & 1))) & 15
                step = STEP[idx]
                d = step >> 3
                if nib & 1: d += step >> 2
                if nib & 2: d += step >> 1
                if nib & 4: d += step
                pred = max(-32768, min(32767, pred + (-d if nib & 8 else d)))
                idx = max(0, min(88, idx + INDEX[nib & 7]))
                out[f * 64 + i, c] = pred
    return out[:samples]

if __name__ == '__main__':
    f = open(sys.argv[1], 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    flt = sys.argv[2] if len(sys.argv) > 2 else ''
    for name, off, sz in banks(m):
        if flt not in name: continue
        h = header(m, off)
        print(name, off, sz, h)
        if '-x' in sys.argv and h['mode'] == 7:
            pcm = ima(m[h['data']:h['data'] + h['dsz']], h['ch'], h['samples'])
            w = wave.open(sys.argv[sys.argv.index('-x') + 1] + '/' + name + '.wav', 'wb'); w.setnchannels(h['ch']); w.setsampwidth(2); w.setframerate(h['rate']); w.writeframes(pcm.tobytes()); w.close()
