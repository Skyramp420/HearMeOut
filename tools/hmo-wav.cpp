// hmo-wav — spracuje WAV súbor rovnakým reťazcom ako APO. Na ladenie presetov bez reštartu audia.
//   hmo-wav vstup.wav vystup.wav [config.ini]
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "hmo/dsp.h"

struct Wav { int channels = 0, rate = 0; std::vector<float> data; };

static bool readWav(const char* path, Wav& w) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
    int fmt = 0, bits = 0; size_t p = 12;
    while (p + 8 <= b.size()) {
        uint32_t sz; std::memcpy(&sz, &b[p + 4], 4);
        const char* id = &b[p]; const char* d = &b[p + 8];
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t tag, ch, bps; uint32_t sr;
            std::memcpy(&tag, d, 2); std::memcpy(&ch, d + 2, 2); std::memcpy(&sr, d + 4, 4); std::memcpy(&bps, d + 14, 2);
            if (tag == 0xFFFE && sz >= 40) std::memcpy(&tag, d + 24, 2);   // WAVE_FORMAT_EXTENSIBLE
            fmt = tag; w.channels = ch; w.rate = (int)sr; bits = bps;
        } else if (!std::memcmp(id, "data", 4)) {
            size_t bytes = std::min<size_t>(sz, b.size() - (p + 8)), bpsB = bits / 8, n = bytes / bpsB;
            w.data.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const unsigned char* s = (const unsigned char*)d + i * bpsB;
                if (fmt == 3 && bits == 32) std::memcpy(&w.data[i], s, 4);
                else if (fmt == 1 && bits == 16) w.data[i] = (int16_t)(s[0] | s[1] << 8) / 32768.0f;
                else if (fmt == 1 && bits == 24) { int32_t v = (s[0] << 8 | s[1] << 16 | s[2] << 24) >> 8; w.data[i] = v / 8388608.0f; }
                else if (fmt == 1 && bits == 32) { int32_t v; std::memcpy(&v, s, 4); w.data[i] = v / 2147483648.0f; }
                else return false;
            }
            return w.channels > 0;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

static bool writeWav(const char* path, const Wav& w) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t dataBytes = (uint32_t)(w.data.size() * 4), riff = 36 + dataBytes, sr = w.rate, br = w.rate * w.channels * 4, fmtSz = 16;
    uint16_t tag = 3, ch = (uint16_t)w.channels, align = (uint16_t)(w.channels * 4), bits = 32;
    f.write("RIFF", 4); f.write((char*)&riff, 4); f.write("WAVEfmt ", 8); f.write((char*)&fmtSz, 4);
    f.write((char*)&tag, 2); f.write((char*)&ch, 2); f.write((char*)&sr, 4); f.write((char*)&br, 4);
    f.write((char*)&align, 2); f.write((char*)&bits, 2); f.write("data", 4); f.write((char*)&dataBytes, 4);
    f.write((const char*)w.data.data(), dataBytes);
    return (bool)f;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "pouzitie: hmo-wav vstup.wav vystup.wav [config.ini]\n"); return 2; }
    Wav in;
    if (!readWav(argv[1], in)) { std::fprintf(stderr, "neviem nacitat %s (podporovane: PCM 16/24/32, float 32)\n", argv[1]); return 1; }
    hmo::Params p;
    if (argc > 3) {
        std::ifstream c(argv[3]); std::stringstream ss; ss << c.rdbuf();
        p = hmo::parseConfig(ss.str());
    }
    hmo::Chain chain; chain.setParams(p); chain.prepare((float)in.rate); chain.setParams(p);
    const int C = in.channels; const size_t frames = in.data.size() / C;
    const int lat = chain.latencySamples();
    Wav out{C, in.rate, std::vector<float>(in.data.size())};
    // spracuj + dorovnaj latenciu, aby bol výstup časovo zarovnaný so vstupom
    for (size_t i = 0; i < frames + lat; ++i) {
        float x = 0;
        if (i < frames) {
            if (p.inputChannel < 0 || p.inputChannel >= C) { for (int c = 0; c < C; ++c) x += in.data[i * C + c]; x /= C; }
            else x = in.data[i * C + p.inputChannel];
        }
        float y = chain.process(x);
        if (i >= (size_t)lat) for (int c = 0; c < C; ++c) out.data[(i - lat) * C + c] = y;
    }
    if (!writeWav(argv[2], out)) { std::fprintf(stderr, "neviem zapisat %s\n", argv[2]); return 1; }
    std::printf("OK: %zu vzoriek, %d Hz, %d ch, latencia %d vz. (%.1f ms)\n", frames, in.rate, C, lat, 1000.0 * lat / in.rate);
    return 0;
}
