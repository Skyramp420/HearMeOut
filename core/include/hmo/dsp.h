// HearMeOut — DSP bloky. Žiadne alokácie po prepare(), všetko vhodné do realtime vlákna.
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include "params.h"

namespace hmo {

constexpr float kPi = 3.14159265358979f;

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float x)  { return 20.0f * std::log10(x > 1e-12f ? x : 1e-12f); }
// rýchle aproximácie pre per-sample dynamiku (chyba < 0.05 dB)
inline float fastLog2(float x) {
    union { float f; uint32_t i; } v{x};
    float e = (float)(int)((v.i >> 23) & 255) - 128.0f;
    v.i = (v.i & 0x007FFFFF) | 0x3F800000;
    float m = v.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
inline float fastExp2(float p) {
    if (p < -126.0f) p = -126.0f;
    float offset = p < 0 ? 1.0f : 0.0f;
    int w = (int)p;
    float z = p - (float)w + offset;
    union { uint32_t i; float f; } v{ (uint32_t)((1 << 23) * (p + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z)) };
    return v.f;
}
inline float fastDbToLin(float db) { return fastExp2(db * 0.16609640474f); }
inline float fastLinToDb(float x)  { return 6.02059991f * fastLog2(x > 1e-12f ? x : 1e-12f); }
inline float timeCoef(float ms, float fs) { return ms <= 0.0f ? 0.0f : std::exp(-1.0f / (0.001f * ms * fs)); }

// ---------------------------------------------------------------- Biquad (RBJ cookbook)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void highpass(float fs, float f, float q);
    void lowpass(float fs, float f, float q);
    void lowShelf(float fs, float f, float gainDb);
    void highShelf(float fs, float f, float gainDb);
    void peak(float fs, float f, float q, float gainDb);
    void reset() { z1 = z2 = 0; }

    inline float process(float x) {           // transposed direct form II
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// ---------------------------------------------------------------- statický denoise
class NoiseReducer {
public:
    static constexpr int N = 512;     // veľkosť FFT
    static constexpr int H = N / 4;   // hop (75 % prekrytie)
    static constexpr int B = N / 2 + 1;

    void prepare(float fs);
    void setParams(const Params& p);
    void reset();
    float process(float x);
    static constexpr int latencySamples() { return N; }

private:
    void processFrame();
    void fft(std::complex<float>* d, bool inverse);

    float fs_ = 48000.0f;
    static constexpr int U = 8;          // počet pod-okien minimum statistics (spolu ~1.5 s)
    float floor_ = 0.06f, over_ = 1.6f, slowA_ = 0.9f;
    int   frames_ = 0, V_ = 70, subCount_ = 0, subIdx_ = 0;

    std::array<float, N> window_{};
    std::array<float, N> analysis_{};   // posledných N vstupných vzoriek
    std::array<float, N> ola_{};        // overlap-add akumulátor
    std::array<float, H> inQ_{}, outQ_{};
    int pos_ = 0;

    std::array<std::complex<float>, N> buf_{};
    std::array<std::complex<float>, N / 2> twiddle_{};
    std::array<uint16_t, N> bitrev_{};
    std::array<float, B> slowPow_{}, noise_{}, gain_{}, tmpGain_{}, curMin_{}, prevClean_{};
    std::array<std::array<float, B>, U> subMin_{};
public:
    float noiseEstimate(int k) const { return noise_[k]; }   // pre UI / testy
};

// ---------------------------------------------------------------- gate / expander
class Expander {
public:
    void prepare(float fs) { fs_ = fs; reset(); }
    void setParams(const Params& p);
    void reset() { env_ = 0; gainDb_ = 0; holdLeft_ = 0; }
    float process(float x);
    float gainDb() const { return gainDb_; }
private:
    float fs_ = 48000, thr_ = -50, ratio_ = 2.5f, range_ = 18;
    float envAtk_ = 0, envRel_ = 0, gAtk_ = 0, gRel_ = 0;
    int   holdSamples_ = 0, holdLeft_ = 0;
    float env_ = 0, gainDb_ = 0;
};

// ---------------------------------------------------------------- kompresor (Opto / FET / Opto + FET)
class Compressor {
public:
    void prepare(float fs) { fs_ = fs; reset(); }
    void setParams(const Params& p);
    void reset() { grA_ = grB_ = 0; ms_ = 0; }
    float process(float x);
    float gainReductionDb() const { return grA_ + grB_; }
    static float gainComputer(float lvl, float thr, float ratio, float knee);
private:
    float fs_ = 48000;
    int   mode_ = 2;
    // stupeň A – Opto: RMS detektor, mäkký, uvoľnenie závislé od programu
    float thrA_ = -24, ratioA_ = 3, kneeA_ = 6, atkA_ = 0, relFastA_ = 0, relSlowA_ = 0, rmsC_ = 0, ms_ = 0, grA_ = 0;
    // stupeň B – FET: špičkový detektor, rýchly
    float thrB_ = -18, ratioB_ = 4, kneeB_ = 2, atkB_ = 0, relB_ = 0, grB_ = 0;
    float makeup_ = 1;
};

// ---------------------------------------------------------------- polpásmový FIR pre 2× oversampling
class HalfbandFir {
public:
    static constexpr int L = 31;
    void design();
    void reset() { buf_.fill(0); pos_ = 0; }
    float process(float x) {
        buf_[pos_] = x;
        float acc = 0; int j = pos_;
        for (int i = 0; i < L; ++i) { acc += h_[i] * buf_[j]; j = j == 0 ? L - 1 : j - 1; }
        pos_ = pos_ + 1 == L ? 0 : pos_ + 1;
        return acc;
    }
private:
    std::array<float, L> h_{}, buf_{};
    int pos_ = 0;
};

// ---------------------------------------------------------------- saturácia („farba“)
class Saturator {
public:
    void prepare(float fs) { (void)fs; up_.design(); down_.design(); reset(); }
    void setParams(const Params& p);
    void reset() { up_.reset(); down_.reset(); dcX_ = dcY_ = 0; }
    float process(float x);
    static constexpr int latencySamples() { return (HalfbandFir::L - 1) / 2; }   // (L-1)/2 pri fs
private:
    float shape(float x) const { return (std::tanh(k_ * (x + b_)) - tb_) * norm_; }
    HalfbandFir up_, down_;
    float k_ = 1, b_ = 0.15f, tb_ = 0, norm_ = 1;
    float dcX_ = 0, dcY_ = 0;
};

// ---------------------------------------------------------------- de-esser (dynamické stlmenie výšok)
class DeEsser {
public:
    void prepare(float fs) { fs_ = fs; reset(); }
    void setParams(const Params& p);
    void reset() { lp_.reset(); env_ = 0; gDb_ = 0; }
    float process(float x);
    float gainDb() const { return gDb_; }
private:
    float fs_ = 48000, thr_ = -30, range_ = 8;
    float envA_ = 0, envR_ = 0, gA_ = 0, gR_ = 0, env_ = 0, gDb_ = 0;
    Biquad lp_;
};

// ---------------------------------------------------------------- lookahead limiter
class Limiter {
public:
    static constexpr int kMaxLook = 256;
    void prepare(float fs);
    void setParams(const Params& p);
    void reset();
    float process(float x);
    int latencySamples() const { return look_; }
private:
    float fs_ = 48000, ceil_ = 0.89f, rel_ = 0;
    int   look_ = 72;
    std::array<float, kMaxLook + 1> delay_{};
    int   dpos_ = 0;
    // sliding-window minimum (monotónny deque cez kruhový buffer)
    std::array<float, kMaxLook + 2> qv_{};
    std::array<int,   kMaxLook + 2> qi_{};
    int   qh_ = 0, qt_ = 0, n_ = 0;
    float relGain_ = 1;
    // box filter
    std::array<float, kMaxLook + 1> box_{};
    int   bpos_ = 0;
    double bsum_ = 0;
};

// ---------------------------------------------------------------- celý reťazec (mono)
class Chain {
public:
    void prepare(float fs);
    void setParams(const Params& p);   // RT-safe (len prepočet koeficientov)
    void reset();
    float process(float x);
    int latencySamples() const;
    const Params& params() const { return p_; }
    float compGainReductionDb() const { return p_.compEnabled ? comp_.gainReductionDb() : 0.0f; }
    float gateGainDb() const { return p_.gateEnabled ? gate_.gainDb() : 0.0f; }
    float deessGainDb() const { return p_.deessEnabled ? deess_.gainDb() : 0.0f; }

private:
    float fs_ = 48000;
    Params p_;
    float inGain_ = 1, outGain_ = 1;
    Biquad hp1_, hp2_;                 // 24 dB/okt.
    Biquad eqLow_, eqMud_, eqPres_, eqAir_;
    NoiseReducer nr_;
    Expander gate_;
    Compressor comp_;
    Saturator sat_;
    DeEsser deess_;
    Limiter lim_;
};

// ---------------------------------------------------------------- triple buffer (1 zapisovateľ, 1 čitateľ, bez zámkov)
template <typename T>
class TripleBuffer {
public:
    // zapisovateľ
    T& back() { return slots_[back_]; }
    void publish() { back_ = state_.exchange(back_ | kDirty, std::memory_order_acq_rel) & 3; }
    // čitateľ — vráti true, ak prišla nová hodnota
    bool fetch() {
        if (!(state_.load(std::memory_order_acquire) & kDirty)) return false;
        front_ = state_.exchange(front_, std::memory_order_acq_rel) & 3;
        return true;
    }
    const T& front() const { return slots_[front_]; }
private:
    static constexpr int kDirty = 4;
    T slots_[3]{};
    int back_ = 0, front_ = 1;
    std::atomic<int> state_{2};
};

} // namespace hmo
