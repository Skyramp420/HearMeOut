// test VST3 jadra hosta na vlastnom testovacom plugine (Linux)
#include <cmath>
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include <vector>
#include "vst3host.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL: " __VA_ARGS__); } else std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } while (0)

static double gainDb(hmo::Vst3Plugin& p, int blocks = 20) {
    std::vector<float> L(512), R(512); double in = 0, out = 0; long t = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 512; ++i, ++t) { L[i] = R[i] = 0.3f * std::sin(2 * M_PI * 440 * t / 48000.0); in += L[i] * L[i]; }
        p.process(L.data(), R.data(), 512);
        if (b > 0) for (int i = 0; i < 512; ++i) out += L[i] * L[i];
        else in = 0;
    }
    return 10 * std::log10((out + 1e-20) / (in + 1e-20));
}

int main(int argc, char** argv) {
    std::string bundle = argv[1];
    hmo::Vst3Plugin p;
    bool ok = p.load(bundle);
    CHECK(ok, "nacitanie bundle: '%s' od '%s' %s", p.name().c_str(), p.vendor().c_str(), ok ? "" : p.error().c_str());
    if (!ok) return 1;
    void* so = dlopen(hmo::resolveVst3Binary(bundle).c_str(), RTLD_NOW | RTLD_NOLOAD);
    auto msgs = so ? (int (*)())dlsym(so, "hmoTestMessages") : nullptr;
    CHECK(msgs && msgs() >= 1, "komponent -> kontroler sprava cez host IMessage (%d)", msgs ? msgs() : -1);
    CHECK(p.prepare(48000, 512), "prepare 48 kHz / 512");
    double g = gainDb(p);
    CHECK(std::fabs(g + 6.02) < 0.1, "spracovanie: predvoleny gain 0.5 -> %.2f dB", g);
    p.simulateEdit(0, 1.0);
    g = gainDb(p);
    CHECK(std::fabs(g) < 0.1, "gombik v GUI (performEdit) sa dostal do zvuku -> %.2f dB", g);
    CHECK(p.dirty.load(), "zmena oznacila stav na ulozenie");
    std::vector<uint8_t> st;
    CHECK(p.getState(st) && st.size() == 4 + 4 + 4 + 4 + 4, "getState (%zu B)", st.size());
    hmo::Vst3Plugin q;
    q.load(bundle);
    CHECK(q.setState(st), "setState do novej instancie");
    q.prepare(48000, 512);
    double g2 = gainDb(q);
    CHECK(std::fabs(g2) < 0.1, "obnovena instancia ma ulozeny gain -> %.2f dB", g2);
    p.bypass = true;
    CHECK(std::fabs(gainDb(p)) < 0.01, "bypass");
    p.bypass = false;
    CHECK(p.prepare(44100, 1024), "re-prepare 44.1 kHz / 1024");
    CHECK(std::fabs(gainDb(p)) < 0.1, "po re-prepare stale spracuva");
    std::vector<uint8_t> bad = { 'X', 'X' };
    CHECK(!q.setState(bad), "poskodeny stav odmietnuty");
    std::printf(fails ? "\n%d zlyhalo\n" : "\nvsetko ok\n", fails);
    return fails ? 1 : 0;
}
