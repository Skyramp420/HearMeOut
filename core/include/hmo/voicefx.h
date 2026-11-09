// HearMeOut — zábavné hlasové efekty (robot, chipmunk, vysielačka…). Bez alokácií po prepare().
#pragma once
#include <vector>
#include "dsp.h"

namespace hmo {

enum class FunType : int { Off = 0, Chipmunk, Monster, Robot, Radio, Phone, Megaphone, Cave, Alien, Count };

// posun výšky hlasu cez dve kĺzavé čítacie hlavy (latencia ~ window/2)
class PitchShifter {
public:
    void prepare(float fs);
    void reset();
    void setRatio(float r) { ratio_ = r; }
    float process(float x);
private:
    static constexpr int kCap = 8192;          // mocnina 2
    std::vector<float> buf_ = std::vector<float>(kCap, 0.0f);
    int w_ = 0;
    float win_ = 2400, phase_ = 0, ratio_ = 1;
    float read(float delay) const;
};

class VoiceFx {
public:
    void prepare(float fs);
    void setParams(const Params& p);   // RT-safe
    void reset();
    bool active() const { return type_ != FunType::Off && mix_ > 0.0f; }
    float process(float x);
private:
    float fs_ = 48000;
    FunType type_ = FunType::Off;
    float amount_ = 0.5f, mix_ = 1.0f;
    PitchShifter ps_;
    Biquad hp_, lp_, peak_, fbLp_;
    float ringPh_ = 0, ringInc_ = 0, lfoPh_ = 0, lfoInc_ = 0;
    float drive_ = 1, makeup_ = 1, baseRatio_ = 1, vibDepth_ = 0;
    // telefón: zníženie vzorkovania
    float holdVal_ = 0, holdCnt_ = 0, holdStep_ = 1;
    // jaskyňa: ozvena
    std::vector<float> dl_ = std::vector<float>(1 << 15, 0.0f);
    int dw_ = 0, dlen_ = 12000;
    float fb_ = 0.4f;
};

} // namespace hmo
