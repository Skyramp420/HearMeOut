// Jednoduché testy DSP jadra (bez frameworku). Návratový kód != 0 = chyba.
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>
#include "hmo/dsp.h"

using namespace hmo;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

static double rms(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i]; return std::sqrt(s / (b - a));
}

int main() {
    const float fs = 48000;

    // 1) denoise s 0 dB redukciou = presná rekonštrukcia, zmeraj latenciu
    {
        Params p; p.nrReductionDb = 0;
        NoiseReducer nr; nr.prepare(fs); nr.setParams(p);
        std::vector<float> in(8192, 0.0f), out(8192);
        std::mt19937 rng(1); std::normal_distribution<float> nd(0, 0.1f);
        for (auto& x : in) x = nd(rng);
        for (size_t i = 0; i < in.size(); ++i) out[i] = nr.process(in[i]);
        int lat = NoiseReducer::latencySamples();
        double err = 0; for (size_t i = 2048; i < in.size(); ++i) err = std::max(err, (double)std::fabs(out[i] - in[i - lat]));
        CHECK(err < 1e-4, "denoise rekonstrukcia pri latencii %d vz. (max chyba %.2e)", lat, err);
    }

    // 2) limiter neprekročí strop
    {
        Params p; p.limCeilingDb = -1;
        Limiter l; l.prepare(fs); l.setParams(p);
        float mx = 0; std::mt19937 rng(2); std::uniform_real_distribution<float> ud(-3, 3);
        for (int i = 0; i < 48000; ++i) {
            float x = (i % 4800 < 100) ? ud(rng) : 0.5f * std::sin(2 * kPi * 440 * i / fs);
            mx = std::max(mx, std::fabs(l.process(x)));
        }
        CHECK(mx <= dbToLin(-1) + 1e-5f, "limiter strop -1 dBFS (max %.4f / %.4f)", mx, dbToLin(-1));
    }

    // 3) potlačenie statického šumu: šum -50 dBFS + tónové dávky -20 dBFS (1 s zap / 1 s vyp)
    {
        Params p; p.hpEnabled = p.gateEnabled = p.eqEnabled = p.compEnabled = p.limEnabled = p.satEnabled = p.deessEnabled = false;
        Chain c; c.setParams(p); c.prepare(fs); c.setParams(p);
        const size_t n = (size_t)(fs * 10); const int lat = c.latencySamples();
        std::vector<float> in(n), out(n);
        std::mt19937 rng(3); std::normal_distribution<float> nd(0, dbToLin(-50));
        for (size_t i = 0; i < n; ++i) {
            bool on = (i / (size_t)fs) % 2 == 1;
            float tone = on ? dbToLin(-20) * std::sqrt(2.0f) * (0.6f * std::sin(2 * kPi * 220 * i / fs) + 0.8f * std::sin(2 * kPi * 1250 * i / fs)) / 1.0f : 0;
            in[i] = nd(rng) + tone;
        }
        for (size_t i = 0; i < n; ++i) { float y = c.process(in[i]); if (i >= (size_t)lat) out[i - lat] = y; }
        // vyhodnoť 6.–7. s (ticho) a 7.–8. s (tón), mimo prechodov
        size_t s0 = (size_t)(fs * 6.2), s1 = (size_t)(fs * 6.9), t0 = (size_t)(fs * 7.2), t1 = (size_t)(fs * 7.9);
        double noiseIn = linToDb((float)rms(in, s0, s1)), noiseOut = linToDb((float)rms(out, s0, s1));
        double toneIn = linToDb((float)rms(in, t0, t1)), toneOut = linToDb((float)rms(out, t0, t1));
        CHECK(noiseIn - noiseOut > 15, "sum v tichu %.1f -> %.1f dBFS (potlacenie %.1f dB)", noiseIn, noiseOut, noiseIn - noiseOut);
        CHECK(std::fabs(toneIn - toneOut) < 1.0, "signal zachovany %.2f -> %.2f dBFS", toneIn, toneOut);
    }

    // 4) config parser
    {
        Params p = parseConfig("[denoise]\nreduction_db = 30 ; komentar\n[gate]\nenabled=off\n[global]\ninput_channel=-1\nfoo=bar\n");
        CHECK(p.nrReductionDb == 30 && !p.gateEnabled && p.inputChannel == -1 && p.compEnabled, "config parser");
    }


    // 6) saturácia: malý signál nezmenený, veľký skreslený, bez aliasingu (2× oversampling)
    {
        auto tone = [&](float f, float db, float drive, double fProbe, double& gainDb, double& probeRelDb) {
            Params p; p.satDrive = drive;
            Saturator s; s.prepare(fs); s.setParams(p);
            const int n = 48000, lat = Saturator::latencySamples();
            std::vector<float> out(n);
            float a = dbToLin(db);
            for (int i = 0; i < n + lat; ++i) { float y = s.process(i < n ? a * std::sin(2 * kPi * f * i / fs) : 0); if (i >= lat) out[i - lat] = y; }
            // Goertzel na f a fProbe (od 0.5 s)
            auto mag = [&](double fr) { double w = 2 * 3.14159265358979 * fr / fs, c = 2 * std::cos(w), s1 = 0, s2 = 0;
                for (int i = 24000; i < n; ++i) { double s0 = out[i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / ((n - 24000) / 2.0); };
            double m = mag(f);
            gainDb = 20 * std::log10(m / a); probeRelDb = 20 * std::log10(mag(fProbe) / m + 1e-12);
        };
        double g, pr;
        tone(1000, -40, 50, 2000, g, pr);
        CHECK(std::fabs(g) < 0.3, "saturacia: tichy signal bez zmeny (%.2f dB)", g);
        tone(200, -3, 100, 400, g, pr);
        CHECK(pr > -40, "saturacia: hlasny signal ma harmonicke (2. harm. %.1f dB)", pr);
        tone(15000, -3, 100, 3000, g, pr);   // 3. harmonická 45 kHz by bez oversamplingu padla na 3 kHz
        CHECK(pr < -45, "saturacia: alias na 3 kHz potlaceny (%.1f dB pod zakladom)", pr);
    }

    // 7) de-esser: sykavka (7 kHz) stlmená, hlas (300 Hz) nedotknutý
    {
        auto run = [&](float f) {
            Params p; p.deessThresholdDb = -30; p.deessRangeDb = 8; p.deessFreq = 6000;
            DeEsser d; d.prepare(fs); d.setParams(p);
            double in = 0, out = 0;
            for (int i = 0; i < 48000; ++i) { float x = dbToLin(-10) * std::sin(2 * kPi * f * i / fs); float y = d.process(x);
                if (i > 24000) { in += x * x; out += y * y; } }
            return 10 * std::log10(out / in);
        };
        double s = run(7000), v = run(300);
        CHECK(s < -5 && std::fabs(v) < 0.5, "de-esser: 7 kHz %.1f dB, 300 Hz %.2f dB", s, v);
    }

    // 8) kompresor: všetky charaktery stlačia hlasný signál a auto-makeup drží úroveň
    {
        for (int mode = 0; mode < 3; ++mode) {
            Params p; p.compMode = mode; p.compThresholdDb = -24; p.compRatio = 3;
            Compressor c; c.prepare(fs); c.setParams(p);
            float pk = 0;
            for (int i = 0; i < 48000; ++i) { float y = c.process(dbToLin(-12) * std::sin(2 * kPi * 220 * i / fs)); if (i > 24000) pk = std::max(pk, std::fabs(y)); }
            CHECK(c.gainReductionDb() < -3 && std::fabs(linToDb(pk) + 12) < 4, "kompresor mode %d: GR %.1f dB, vystup %.1f dBFS (vstup -12)", mode, c.gainReductionDb(), linToDb(pk));
        }
    }

    // 5) výkon celého reťazca
    {
        Params p; Chain c; c.setParams(p); c.prepare(fs); c.setParams(p);
        const int secs = 60; std::mt19937 rng(4); std::normal_distribution<float> nd(0, 0.05f);
        std::vector<float> buf(480); for (auto& x : buf) x = nd(rng);
        volatile float sink = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < secs * 100; ++b) for (float x : buf) sink = sink + c.process(x);
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("info: %d s audia spracovanych za %.3f s => %.2f %% jedneho jadra, latencia %.1f ms\n",
                    secs, el, 100.0 * el / secs, 1000.0 * c.latencySamples() / fs);
    }

    std::printf(failures ? "\n%d test(ov) zlyhalo\n" : "\nvsetko ok\n", failures);
    return failures ? 1 : 0;
}
