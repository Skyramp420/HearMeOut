#include "hmo/dsp.h"
#include <algorithm>
#include <cstring>

namespace hmo {

// ================================================================ Biquad
static inline float clampFreq(float fs, float f) { return std::min(std::max(f, 10.0f), 0.45f * fs); }

static inline void normalize(Biquad& q, float b0, float b1, float b2, float a0, float a1, float a2) {
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}

void Biquad::highpass(float fs, float f, float q) {
    float w = 2 * kPi * clampFreq(fs, f) / fs, cs = std::cos(w), al = std::sin(w) / (2 * q);
    normalize(*this, (1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
}

void Biquad::lowpass(float fs, float f, float q) {
    float w = 2 * kPi * clampFreq(fs, f) / fs, cs = std::cos(w), al = std::sin(w) / (2 * q);
    normalize(*this, (1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
}

void Biquad::peak(float fs, float f, float q, float g) {
    float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * clampFreq(fs, f) / fs;
    float cs = std::cos(w), al = std::sin(w) / (2 * std::max(q, 0.1f));
    normalize(*this, 1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}

void Biquad::lowShelf(float fs, float f, float g) {
    float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * clampFreq(fs, f) / fs;
    float cs = std::cos(w), al = std::sin(w) / 2 * std::sqrt(2.0f), sA = 2 * std::sqrt(A) * al;
    normalize(*this,
              A * ((A + 1) - (A - 1) * cs + sA), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sA),
              (A + 1) + (A - 1) * cs + sA, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sA);
}

void Biquad::highShelf(float fs, float f, float g) {
    float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * clampFreq(fs, f) / fs;
    float cs = std::cos(w), al = std::sin(w) / 2 * std::sqrt(2.0f), sA = 2 * std::sqrt(A) * al;
    normalize(*this,
              A * ((A + 1) + (A - 1) * cs + sA), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sA),
              (A + 1) - (A - 1) * cs + sA, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sA);
}

// ================================================================ NoiseReducer
void NoiseReducer::prepare(float fs) {
    fs_ = fs;
    for (int n = 0; n < N; ++n) window_[n] = 0.5f - 0.5f * std::cos(2 * kPi * n / N); // periodický Hann
    int bits = 0; while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i) {
        int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[i] = (uint16_t)r;
    }
    for (int k = 0; k < N / 2; ++k) twiddle_[k] = std::polar(1.0f, -2 * kPi * k / N);
    V_ = std::max(4, (int)std::lround(1.5 * fs / H / U));
    reset();
}

void NoiseReducer::reset() {
    analysis_.fill(0); ola_.fill(0); inQ_.fill(0); outQ_.fill(0);
    slowPow_.fill(0); noise_.fill(0); gain_.fill(1); prevClean_.fill(0); pos_ = 0;
    curMin_.fill(1e30f); for (auto& s : subMin_) s.fill(1e30f);
    frames_ = subCount_ = subIdx_ = 0;
}

void NoiseReducer::setParams(const Params& p) {
    floor_   = dbToLin(-std::max(0.0f, p.nrReductionDb));
    over_    = std::max(0.5f, p.nrStrength);
    slowA_   = std::exp(-(float)H / (fs_ * 0.100f));   // výkon pre odhad šumu ~100 ms
}

float NoiseReducer::process(float x) {
    float y = outQ_[pos_];
    inQ_[pos_] = x;
    if (++pos_ == H) { pos_ = 0; processFrame(); }
    return y;
}

void NoiseReducer::fft(std::complex<float>* d, bool inverse) {
    for (int i = 0; i < N; ++i) if (i < bitrev_[i]) std::swap(d[i], d[bitrev_[i]]);
    for (int len = 2; len <= N; len <<= 1) {
        int half = len / 2, step = N / len;
        for (int i = 0; i < N; i += len)
            for (int j = 0; j < half; ++j) {
                std::complex<float> w = twiddle_[j * step];
                if (inverse) w = std::conj(w);
                std::complex<float> u = d[i + j], v = d[i + j + half] * w;
                d[i + j] = u + v; d[i + j + half] = u - v;
            }
    }
    if (inverse) { float s = 1.0f / N; for (int i = 0; i < N; ++i) d[i] *= s; }
}

static constexpr float kBias = 1.5f;   // korekcia minima (kalibrované na bielom šume)
static constexpr float kDD = 0.98f;    // decision-directed vyhladenie

void NoiseReducer::processFrame() {
    std::memmove(analysis_.data(), analysis_.data() + H, sizeof(float) * (N - H));
    std::memcpy(analysis_.data() + N - H, inQ_.data(), sizeof(float) * H);

    for (int n = 0; n < N; ++n) buf_[n] = { analysis_[n] * window_[n], 0.0f };
    fft(buf_.data(), false);

    // prvé snímky obsahujú ešte nuly z prázdneho bufferu — nepočítaj z nich štatistiku
    if (frames_ < N / H) {
        ++frames_;
        std::memmove(ola_.data(), ola_.data() + H, sizeof(float) * (N - H));
        std::fill(ola_.begin() + (N - H), ola_.end(), 0.0f);
        constexpr float norm0 = 1.0f / 1.5f;
        fft(buf_.data(), true);
        for (int n = 0; n < N; ++n) ola_[n] += buf_[n].real() * window_[n] * norm0;
        std::memcpy(outQ_.data(), ola_.data(), sizeof(float) * H);
        return;
    }
    const bool first = frames_ == N / H;
    if (first) ++frames_;

    for (int k = 0; k < B; ++k) {
        float P = std::norm(buf_[k]) + 1e-20f;
        slowPow_[k] = first ? P : slowA_ * slowPow_[k] + (1 - slowA_) * P;
        // minimum statistics: minimum vyhladeného výkonu za posledných ~1.5 s
        curMin_[k] = std::min(curMin_[k], slowPow_[k]);
        float m = curMin_[k];
        for (int u = 0; u < U; ++u) m = std::min(m, subMin_[u][k]);
        noise_[k] = kBias * m;   // minimum leží pod priemerom šumu -> korekcia biasu
        // decision-directed Wiener (Ephraim–Malah): a-priori SNR vyhladený cez predchádzajúci čistý odhad
        float gammaPost = P / noise_[k];
        float xi = first ? std::max(gammaPost - 1.0f, 0.0f)
                         : kDD * prevClean_[k] / noise_[k] + (1 - kDD) * std::max(gammaPost - 1.0f, 0.0f);
        float g = xi / (xi + over_);
        g = std::max(g, floor_);
        prevClean_[k] = g * g * P;
        gain_[k] = g;
    }
    if (++subCount_ >= V_) {
        subMin_[subIdx_] = curMin_; subIdx_ = (subIdx_ + 1) % U;
        curMin_.fill(1e30f); subCount_ = 0;
    }
    // frekvenčné vyhladenie proti „muzikálnemu šumu“
    for (int k = 0; k < B; ++k) {
        float l = gain_[k > 0 ? k - 1 : k], r = gain_[k < B - 1 ? k + 1 : k];
        tmpGain_[k] = 0.25f * l + 0.5f * gain_[k] + 0.25f * r;
    }
    buf_[0] *= tmpGain_[0];
    buf_[N / 2] *= tmpGain_[N / 2];
    for (int k = 1; k < N / 2; ++k) { buf_[k] *= tmpGain_[k]; buf_[N - k] = std::conj(buf_[k]); }
    fft(buf_.data(), true);

    // Hann analýza * Hann syntéza pri 75 % prekrytí sčíta na 1.5
    constexpr float norm = 1.0f / 1.5f;
    std::memmove(ola_.data(), ola_.data() + H, sizeof(float) * (N - H));
    std::fill(ola_.begin() + (N - H), ola_.end(), 0.0f);
    for (int n = 0; n < N; ++n) ola_[n] += buf_[n].real() * window_[n] * norm;
    std::memcpy(outQ_.data(), ola_.data(), sizeof(float) * H);
}

// ================================================================ Expander
void Expander::setParams(const Params& p) {
    thr_ = p.gateThresholdDb; ratio_ = std::max(1.0f, p.gateRatio); range_ = std::max(0.0f, p.gateRangeDb);
    envAtk_ = timeCoef(0.5f, fs_); envRel_ = timeCoef(30.0f, fs_);
    gAtk_ = timeCoef(std::max(0.1f, p.gateAttackMs), fs_); gRel_ = timeCoef(std::max(1.0f, p.gateReleaseMs), fs_);
    holdSamples_ = (int)(std::max(0.0f, p.gateHoldMs) * 0.001f * fs_);
}

float Expander::process(float x) {
    float a = std::fabs(x);
    env_ = a > env_ ? envAtk_ * env_ + (1 - envAtk_) * a : envRel_ * env_ + (1 - envRel_) * a;
    float envDb = fastLinToDb(env_), target;
    if (envDb >= thr_)        { holdLeft_ = holdSamples_; target = 0; }
    else if (holdLeft_ > 0)   { --holdLeft_; target = 0; }
    else                      target = std::max(-range_, (envDb - thr_) * (ratio_ - 1));
    float c = target > gainDb_ ? gAtk_ : gRel_;
    gainDb_ = c * gainDb_ + (1 - c) * target;
    return x * fastDbToLin(gainDb_);
}

// ================================================================ Compressor
float Compressor::gainComputer(float lvl, float thr, float ratio, float knee) {
    float over = lvl - thr, slope = 1.0f / ratio - 1.0f;
    if (2 * over < -knee) return 0;
    if (knee > 0 && 2 * std::fabs(over) <= knee) { float t = over + knee / 2; return slope * t * t / (2 * knee); }
    return slope * over;
}

void Compressor::setParams(const Params& p) {
    mode_ = std::min(std::max(p.compMode, 0), 2);
    const float ratio = std::max(1.0f, p.compRatio), knee = std::max(0.0f, p.compKneeDb);
    const float atk = std::max(0.1f, p.compAttackMs), rel = std::max(5.0f, p.compReleaseMs);
    rmsC_ = timeCoef(8.0f, fs_);                    // RMS okno ~8 ms
    if (mode_ == 1) {                              // FET sám: všetko riadi stupeň B
        thrB_ = p.compThresholdDb; ratioB_ = ratio; kneeB_ = knee;
        atkB_ = timeCoef(std::min(atk, 5.0f), fs_); relB_ = timeCoef(rel, fs_);
    } else {
        thrA_ = p.compThresholdDb; ratioA_ = ratio; kneeA_ = knee;
        atkA_ = timeCoef(atk, fs_);
        relFastA_ = timeCoef(rel, fs_);
        relSlowA_ = timeCoef(rel * 4.0f, fs_);       // po silnej kompresii sa opto uvoľňuje pomalšie
        // FET za opto: chytá len špičky, ktoré opto nestihne
        thrB_ = p.compThresholdDb + 7.0f; ratioB_ = 4.0f; kneeB_ = 2.0f;
        atkB_ = timeCoef(0.4f, fs_); relB_ = timeCoef(60.0f, fs_);
    }
    float mk = p.compMakeupDb;
    if (p.compAutoMakeup) {
        // vráť hlasitosť tak, aby hlasná reč (~-12 dBFS) vyšla približne rovnako hlasno
        const float ref = -12.0f;
        float g = 0;
        if (mode_ != 1) g += gainComputer(ref + 3.0f, thrA_, ratioA_, kneeA_);
        if (mode_ != 0) g += gainComputer(ref + g, thrB_, ratioB_, kneeB_);
        mk += -0.9f * g;
    }
    makeup_ = dbToLin(mk);
}

float Compressor::process(float x) {
    if (mode_ != 1) {
        ms_ = rmsC_ * ms_ + (1 - rmsC_) * x * x;
        float lvl = 3.01029996f * fastLog2(ms_ + 1e-12f) + 3.0f;      // RMS -> úroveň porovnateľná so špičkou sínusu
        float gc = gainComputer(lvl, thrA_, ratioA_, kneeA_);
        float c;
        if (gc < grA_) c = atkA_;
        else { float w = std::min(1.0f, -grA_ / 10.0f); c = relFastA_ + (relSlowA_ - relFastA_) * w; }
        grA_ = c * grA_ + (1 - c) * gc;
        x *= fastDbToLin(grA_);
    }
    if (mode_ != 0) {
        float gc = gainComputer(fastLinToDb(std::fabs(x)), thrB_, ratioB_, kneeB_);
        float c = gc < grB_ ? atkB_ : relB_;
        grB_ = c * grB_ + (1 - c) * gc;
        x *= fastDbToLin(grB_);
    }
    return x * makeup_;
}

// ================================================================ HalfbandFir + Saturator
void HalfbandFir::design() {
    const int M = (L - 1) / 2;
    float sum = 0;
    for (int n = 0; n < L; ++n) {
        int k = n - M;
        float sinc = k == 0 ? 0.5f : std::sin(kPi * k / 2.0f) / (kPi * k);
        float w = 0.42f - 0.5f * std::cos(2 * kPi * n / (L - 1)) + 0.08f * std::cos(4 * kPi * n / (L - 1));   // Blackman
        h_[n] = sinc * w; sum += h_[n];
    }
    for (auto& v : h_) v /= sum;
    reset();
}

void Saturator::setParams(const Params& p) {
    float d = std::min(std::max(p.satDrive, 0.0f), 100.0f);
    k_ = 1.0f + d * 0.045f;                     // 1 … 5.5
    b_ = 0.12f;                                 // asymetria -> párne harmonické („teplo“)
    tb_ = std::tanh(k_ * b_);
    norm_ = 1.0f / (k_ * (1.0f - tb_ * tb_));  // zisk pre malé signály = 1
}

float Saturator::process(float x) {
    // 2× oversampling: vlož nulu, prefiltruj, ohni, prefiltruj, zahoď každú druhú vzorku
    float u0 = up_.process(2.0f * x), u1 = up_.process(0.0f);
    down_.process(shape(u0));
    float y = down_.process(shape(u1));
    // odstráň DC z asymetrie
    float o = y - dcX_ + 0.9995f * dcY_;
    dcX_ = y; dcY_ = o;
    return o;
}

// ================================================================ DeEsser
void DeEsser::setParams(const Params& p) {
    thr_ = p.deessThresholdDb; range_ = std::max(0.0f, p.deessRangeDb);
    lp_.lowpass(fs_, std::min(std::max(p.deessFreq, 2000.0f), 12000.0f), 0.7071f);
    envA_ = timeCoef(0.5f, fs_); envR_ = timeCoef(40.0f, fs_);
    gA_ = timeCoef(1.0f, fs_);   gR_ = timeCoef(70.0f, fs_);
}

float DeEsser::process(float x) {
    float low = lp_.process(x), high = x - low;       // low + high = x presne
    float a = std::fabs(high);
    env_ = a > env_ ? envA_ * env_ + (1 - envA_) * a : envR_ * env_ + (1 - envR_) * a;
    float over = fastLinToDb(env_) - thr_;
    float target = over > 0 ? std::max(-range_, -over * 0.75f) : 0.0f;   // pomer 4:1
    float c = target < gDb_ ? gA_ : gR_;
    gDb_ = c * gDb_ + (1 - c) * target;
    return low + high * fastDbToLin(gDb_);
}

// ================================================================ Limiter
void Limiter::prepare(float fs) {
    fs_ = fs;
    look_ = std::min(std::max((int)std::lround(0.0015 * fs), 1), kMaxLook);
    reset();
}

void Limiter::reset() {
    delay_.fill(0); box_.fill(1); bsum_ = look_; dpos_ = bpos_ = 0;
    qh_ = qt_ = 0; n_ = 0; relGain_ = 1;
}

void Limiter::setParams(const Params& p) {
    ceil_ = dbToLin(std::min(0.0f, p.limCeilingDb));
    rel_  = timeCoef(std::max(1.0f, p.limReleaseMs), fs_);
}

float Limiter::process(float x) {
    constexpr int cap = kMaxLook + 2;
    const int W = look_ + 1;
    float a = std::fabs(x), gt = a > ceil_ ? ceil_ / a : 1.0f;
    // posuvné minimum cez okno W
    while (qt_ > qh_ && qv_[(qt_ - 1) % cap] >= gt) --qt_;
    qv_[qt_ % cap] = gt; qi_[qt_ % cap] = n_; ++qt_;
    while (qi_[qh_ % cap] <= n_ - W) ++qh_;
    float gmin = qv_[qh_ % cap];
    ++n_;
    if (n_ > (1 << 30)) {          // ochrana pred pretečením počítadla
        for (int i = qh_; i < qt_; ++i) qi_[i % cap] -= (1 << 29);
        n_ -= (1 << 29);
    }
    // release
    relGain_ = gmin < relGain_ ? gmin : relGain_ + (gmin - relGain_) * (1 - rel_);
    // box filter dĺžky look_ -> plynulý nábeh, ktorý stihne pred špičkou
    bsum_ += relGain_ - box_[bpos_]; box_[bpos_] = relGain_; bpos_ = (bpos_ + 1) % look_;
    float g = (float)(bsum_ / look_);
    float y = delay_[dpos_]; delay_[dpos_] = x; dpos_ = (dpos_ + 1) % look_;
    return y * g;
}

// ================================================================ Chain
// Poradie: vstup → dunenie → šum → gate → „krabica“ → kompresor → saturácia → tón → sykavky → výstup → limiter
void Chain::prepare(float fs) {
    fs_ = fs;
    nr_.prepare(fs); gate_.prepare(fs); comp_.prepare(fs); sat_.prepare(fs); deess_.prepare(fs); lim_.prepare(fs);
    setParams(p_);
    reset();
}

void Chain::reset() {
    hp1_.reset(); hp2_.reset(); eqLow_.reset(); eqMud_.reset(); eqPres_.reset(); eqAir_.reset();
    nr_.reset(); gate_.reset(); comp_.reset(); sat_.reset(); deess_.reset(); lim_.reset();
}

void Chain::setParams(const Params& p) {
    p_ = p;
    inGain_ = dbToLin(p.inputGainDb); outGain_ = dbToLin(p.outputGainDb);
    hp1_.highpass(fs_, p.hpFreq, 0.5412f);   // 4. rád Butterworth
    hp2_.highpass(fs_, p.hpFreq, 1.3066f);
    eqLow_.lowShelf(fs_, p.eqLowFreq, p.eqLowGainDb);
    eqMud_.peak(fs_, p.eqMudFreq, p.eqMudQ, p.eqMudGainDb);
    eqPres_.peak(fs_, p.eqPresFreq, p.eqPresQ, p.eqPresGainDb);
    eqAir_.highShelf(fs_, p.eqAirFreq, p.eqAirGainDb);
    nr_.setParams(p); gate_.setParams(p); comp_.setParams(p); sat_.setParams(p); deess_.setParams(p); lim_.setParams(p);
}

int Chain::latencySamples() const {
    return (p_.nrEnabled ? NoiseReducer::latencySamples() : 0)
         + (p_.satEnabled ? Saturator::latencySamples() : 0)
         + (p_.limEnabled ? lim_.latencySamples() : 0);
}

float Chain::process(float x) {
    if (p_.bypass) return x;
    x *= inGain_;
    if (p_.hpEnabled)    x = hp2_.process(hp1_.process(x));
    if (p_.nrEnabled)    x = nr_.process(x);
    if (p_.gateEnabled)  x = gate_.process(x);
    if (p_.eqEnabled)    x = eqMud_.process(x);
    if (p_.compEnabled)  x = comp_.process(x);
    if (p_.satEnabled)   x = sat_.process(x);
    if (p_.eqEnabled)    x = eqAir_.process(eqPres_.process(eqLow_.process(x)));
    if (p_.deessEnabled) x = deess_.process(x);
    x *= outGain_;
    if (p_.limEnabled)   x = lim_.process(x);
    return x;
}

} // namespace hmo
