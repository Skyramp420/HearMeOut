// HearMeOut — most medzi APO (audiodg, session 0) a HearMeOutHost.exe (VST pluginy pod účtom používateľa).
// APO zapisuje mono vstup do inBuf a po pevnej latencii číta spracovaný zvuk z outBuf na rovnakých pozíciách.
#pragma once
#include <atomic>
#include <cstdint>

namespace hmo {

constexpr uint32_t kBridgeMagic   = 0x42524D48;  // "HMRB"
constexpr uint32_t kBridgeVersion = 1;
constexpr uint32_t kBridgeCap     = 1u << 15;    // 32768 vzoriek (~0,68 s pri 48 kHz), mocnina 2
#define HMO_BRIDGE_NAME  L"Global\\HearMeOutBridge"
#define HMO_BRIDGE_EVENT L"Global\\HearMeOutBridgeIn"

struct BridgeShared {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> sampleRate;   // APO
    std::atomic<uint32_t> apoBeat;      // APO: +1 každý blok
    std::atomic<uint32_t> hostBeat;     // host: +1 každý cyklus (aj keď nič nespracúva)
    std::atomic<uint32_t> hostActive;   // host: 1 = má načítané pluginy a chce spracúvať
    std::atomic<uint32_t> latency;      // APO: latencia mostu vo vzorkách
    std::atomic<uint32_t> underruns;    // APO: koľkokrát host nestihol
    std::atomic<int64_t>  inWrite;      // APO: celkový počet zapísaných vstupných vzoriek
    std::atomic<int64_t>  outWrite;     // host: po akú pozíciu je výstup hotový
    std::atomic<int64_t>  outStart;     // host: od akej pozície je výstup platný (po (re)štarte)
    uint32_t reserved[6];
    float inBuf[kBridgeCap];
    float outBuf[kBridgeCap];
};

static_assert(std::atomic<int64_t>::is_always_lock_free, "64-bit atomics");

} // namespace hmo
