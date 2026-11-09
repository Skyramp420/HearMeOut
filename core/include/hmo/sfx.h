// HearMeOut — soundboard most: HearMeOutHost.exe zapisuje zvuky dopredu, APO ich primiešava do mikrofónu.
// Pozície sú v absolútnych vzorkách. APO číta len pozície < writePos, host zapisuje len >= writePos,
// takže sa nikdy nebijú o tú istú vzorku (pokiaľ je náskok < kSfxCap/2).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace hmo {

constexpr uint32_t kSfxMagic   = 0x58464648;   // "HFFX"
constexpr uint32_t kSfxVersion = 1;
constexpr uint32_t kSfxCap     = 1u << 15;     // mocnina 2
#define HMO_SFX_NAME L"Global\\HearMeOutSfx"

struct SfxShared {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> sampleRate;   // APO
    std::atomic<uint32_t> apoBeat;      // APO: +1 každý blok
    std::atomic<int64_t>  readPos;      // APO: po akú pozíciu už prehral
    std::atomic<int64_t>  writePos;     // host: po akú pozíciu sú dáta platné
    uint32_t reserved[8];
    float buf[kSfxCap];
};
static_assert(std::atomic<int64_t>::is_always_lock_free, "64-bit atomics");

inline void sfxInit(SfxShared* s) {
    s->sampleRate.store(0); s->apoBeat.store(0); s->readPos.store(0); s->writePos.store(0);
    for (auto& r : s->reserved) r = 0;
    std::fill(s->buf, s->buf + kSfxCap, 0.0f);
    s->version = kSfxVersion;
    s->magic = kSfxMagic;
}

// APO (RT): pripočítaj čakajúce vzorky do dst a posuň čítanie o m. Vráti true, ak niečo hralo.
inline bool sfxMix(SfxShared* s, float* dst, uint32_t m) {
    const int64_t r = s->readPos.load(std::memory_order_relaxed);
    const int64_t w = s->writePos.load(std::memory_order_acquire);
    bool any = false;
    if (w > r && w - r <= (int64_t)kSfxCap / 2) {
        const int64_t n = std::min<int64_t>(m, w - r);
        for (int64_t i = 0; i < n; ++i) dst[i] += s->buf[(r + i) & (kSfxCap - 1)];
        any = n > 0;
    }
    s->readPos.store(r + m, std::memory_order_release);
    s->apoBeat.fetch_add(1, std::memory_order_relaxed);
    return any;
}

// host: udržuje náskok pred APO. gen(dst, n) naplní n vzoriek (doplní nulami) a vráti false, ak nič nehrá.
class SfxWriter {
public:
    template <class Gen>
    int fill(SfxShared* s, Gen&& gen) {
        const uint32_t rate = s->sampleRate.load(std::memory_order_relaxed);
        const int64_t startLead = rate ? rate / 100 : 480;          // 10 ms – prvá vzorka nového zvuku
        const int64_t lead = rate ? (int64_t)rate * 6 / 100 : 2880;  // 60 ms – maximálny náskok
        const int64_t r = s->readPos.load(std::memory_order_acquire);
        int64_t w = s->writePos.load(std::memory_order_relaxed);
        if (w < r) w = r + startLead;
        const int64_t target = r + lead;
        if (w >= target) return 0;
        const int n = (int)(target - w);
        if ((int)tmp_.size() < n) tmp_.resize(n);
        if (!gen(tmp_.data(), n)) return 0;
        for (int i = 0; i < n; ++i) s->buf[(w + i) & (kSfxCap - 1)] = tmp_[i];
        s->writePos.store(w + n, std::memory_order_release);
        return n;
    }
private:
    std::vector<float> tmp_ = std::vector<float>(kSfxCap / 2);
};

} // namespace hmo
