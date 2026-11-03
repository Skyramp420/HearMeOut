// HearMeOut — zdieľaná pamäť pre živý meter (APO zapisuje, appka číta).
#pragma once
#include <cstdint>

namespace hmo {

constexpr uint32_t kMeterMagic = 0x314F4D48;   // "HMO1"
constexpr uint32_t kMeterVersion = 2;
#define HMO_METER_NAME L"Global\\HearMeOutMeter"

#pragma pack(push, 8)
struct MeterShared {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;          // zvyšuje sa s každým blokom – appka podľa neho pozná, že zvuk tečie
    uint32_t locked;
    uint32_t sampleRate;
    uint32_t channels;
    float    inDb;         // úroveň vstupu (peak s 300 ms dozvukom)
    float    outDb;        // úroveň výstupu
    float    gateDb;       // aktuálne stlmenie gate (0 = otvorený)
    float    compGrDb;     // aktuálne stlačenie kompresora (<= 0)
    float    latencyMs;
    uint32_t pid;
    uint64_t blocks;
    float    deessDb;      // aktuálne stlmenie sykaviek (<= 0)   [v2]
    float    engineState;  // 0 = vlastné efekty, 1 = VST cez hosta, 2 = VST zapnuté, ale host nebeží
};
#pragma pack(pop)
static_assert(sizeof(MeterShared) == 64, "MeterShared layout");

} // namespace hmo
