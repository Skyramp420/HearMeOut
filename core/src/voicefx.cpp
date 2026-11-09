#include "hmo/voicefx.h"
#include <algorithm>

namespace hmo {

// ---------------------------------------------------------------- PitchShifter
void PitchShifter::prepare(float fs) {
    win_ = std::min(0.045f * fs, (float)(kCap / 2 - 4));
    reset();
}
void PitchShifter::reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; phase_ = 0; }
float PitchShifter::read(float d) const {
    float pos = (float)w_ - 1.0f - d;
    int i = (int)std::floor(pos);
    float f = pos - (float)i;
    float a = buf_[i & (kCap - 1)], b = buf_[(i + 1) & (kCap - 1)];
    return a + (b - a) * f;
}
float PitchShifter::process(float x) {
    buf_[w_ & (kCap - 1)] = x;
    ++w_;
    if (w_ >= (1 << 30)) w_ -= (1 << 30) - (1 << 30) % kCap;   // bez pretečenia, zachová fázu bufferu
    // oneskorenie klesá pri vyššom hlase (ratio > 1), rastie pri nižšom
    phase_ += (1.0f - ratio_) / win_;
    phase_ -= std::floor(phase_);
    float p2 = phase_ + 0.5f; p2 -= std::floor(p2);
    float g1 = std::sin(kPi * phase_), g2 = std::sin(kPi * p2);
    return read(phase_ * win_) * g1 * g1 + read(p2 * win_) * g2 * g2;
}

// ---------------------------------------------------------------- VoiceFx
void VoiceFx::prepare(float fs) {
    fs_ = fs;
    ps_.prepare(fs);
    reset();
}
void VoiceFx::reset() {
    ps_.reset();
    hp_.reset(); lp_.reset(); peak_.reset(); fbLp_.reset();
    ringPh_ = lfoPh_ = 0; holdVal_ = 0; holdCnt_ = 0;
    std::fill(dl_.begin(), dl_.end(), 0.0f); dw_ = 0;
}

void VoiceFx::setParams(const Params& p) {
    FunType t = (p.funType > 0 && p.funType < (int)FunType::Count) ? (FunType)p.funType : FunType::Off;
    if (t != type_) { type_ = t; reset(); }
    const float a = std::min(std::max(p.funAmount, 0.0f), 100.0f) / 100.0f;
    amount_ = a;
    mix_ = std::min(std::max(p.funMix, 0.0f), 100.0f) / 100.0f;
    drive_ = 1; makeup_ = 1; baseRatio_ = 1; vibDepth_ = 0; ringInc_ = 0; lfoInc_ = 0;
    hp_.setIdentity(); lp_.setIdentity(); peak_.setIdentity(); fbLp_.setIdentity();
    switch (type_) {
    case FunType::Chipmunk: baseRatio_ = std::pow(2.0f, (4.0f + 8.0f * a) / 12.0f); hp_.highpass(fs_, 150, 0.7071f); break;
    case FunType::Monster:  baseRatio_ = std::pow(2.0f, -(3.0f + 9.0f * a) / 12.0f); drive_ = 1.0f + 2.0f * a; lp_.lowpass(fs_, 5000, 0.7071f); break;
    case FunType::Robot:    ringInc_ = (30.0f + 120.0f * a) / fs_; drive_ = 1.5f; makeup_ = 1.6f; break;
    case FunType::Radio:    hp_.highpass(fs_, 500, 0.9f); lp_.lowpass(fs_, 2600, 0.9f); peak_.peak(fs_, 1500, 1.2f, 6); drive_ = 2.0f + 8.0f * a; makeup_ = 1.3f; break;
    case FunType::Phone:    hp_.highpass(fs_, 320, 0.9f); lp_.lowpass(fs_, 3300, 0.9f); holdStep_ = fs_ / (11025.0f - 5000.0f * a); drive_ = 1.5f; makeup_ = 1.8f; break;
    case FunType::Megaphone:hp_.highpass(fs_, 650, 1.0f); lp_.lowpass(fs_, 4200, 0.8f); peak_.peak(fs_, 2000, 1.0f, 9); drive_ = 4.0f + 12.0f * a; makeup_ = 0.9f; break;
    case FunType::Cave:     dlen_ = std::min((int)((0.16f + 0.3f * a) * fs_), (int)dl_.size() - 1); fb_ = 0.3f + 0.35f * a; fbLp_.lowpass(fs_, 2500, 0.7071f); break;
    case FunType::Alien:    baseRatio_ = std::pow(2.0f, (3.0f + 4.0f * a) / 12.0f); vibDepth_ = 0.02f + 0.06f * a; lfoInc_ = 6.0f / fs_; ringInc_ = 420.0f / fs_; break;
    default: break;
    }
    ps_.setRatio(baseRatio_);
}

static inline float sat(float x) { return x / (1.0f + std::fabs(x)); }   // mäkké orezanie

float VoiceFx::process(float x) {
    if (type_ == FunType::Off) return x;
    float y = x;
    switch (type_) {
    case FunType::Chipmunk:
        y = hp_.process(ps_.process(x));
        break;
    case FunType::Monster:
        y = ps_.process(x);
        y = lp_.process(sat(y * drive_) / drive_ * 2.2f);
        break;
    case FunType::Robot: {
        ringPh_ += ringInc_; ringPh_ -= std::floor(ringPh_);
        float car = std::sin(2 * kPi * ringPh_);
        y = sat(x * car * drive_) * makeup_;
        break; }
    case FunType::Radio:
    case FunType::Megaphone:
        y = peak_.process(lp_.process(hp_.process(x)));
        y = sat(y * drive_) * makeup_;
        break;
    case FunType::Phone:
        if ((holdCnt_ += 1.0f) >= holdStep_) { holdCnt_ -= holdStep_; holdVal_ = x; }
        y = lp_.process(hp_.process(holdVal_));
        y = sat(y * drive_) * makeup_;
        break;
    case FunType::Cave: {
        float d = dl_[(dw_ - dlen_) & ((int)dl_.size() - 1)];
        dl_[dw_ & ((int)dl_.size() - 1)] = x + fbLp_.process(d) * fb_;
        ++dw_;
        y = x + d * 0.6f;
        break; }
    case FunType::Alien: {
        lfoPh_ += lfoInc_; lfoPh_ -= std::floor(lfoPh_);
        ps_.setRatio(baseRatio_ * (1.0f + vibDepth_ * std::sin(2 * kPi * lfoPh_)));
        ringPh_ += ringInc_; ringPh_ -= std::floor(ringPh_);
        float s = ps_.process(x);
        y = 0.7f * s + 0.45f * s * std::sin(2 * kPi * ringPh_);
        break; }
    default: break;
    }
    return x + (y - x) * mix_;
}

} // namespace hmo
