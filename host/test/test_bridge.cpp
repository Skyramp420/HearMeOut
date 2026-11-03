// simulácia protokolu mosta APO <-> host (rovnaká logika ako HearMeOutApo.cpp::bridgeProcess a main_win.cpp::audioThread)
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
#include "hmo/bridge.h"
using namespace std::chrono;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL: " __VA_ARGS__); } else std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } while (0)

int main() {
    auto b = std::make_unique<hmo::BridgeShared>();
    std::memset((void*)b.get(), 0, sizeof(*b));
    std::mutex m; std::condition_variable cv; bool sig = false;
    std::atomic<bool> hostRun{true}, quit{false};
    const uint32_t L = 960, n = 480; const size_t mask = hmo::kBridgeCap - 1;

    std::thread host([&] {
        int64_t readPos = -1;
        while (!quit) {
            { std::unique_lock<std::mutex> lk(m); cv.wait_for(lk, milliseconds(20), [&] { return sig; }); sig = false; }
            if (!hostRun) { readPos = -1; b->hostActive = 0; continue; }
            b->hostBeat++;
            int64_t w = b->inWrite.load(std::memory_order_acquire);
            if (readPos < 0 || w < readPos || w - readPos > (int64_t)hmo::kBridgeCap / 2) { readPos = w; b->outStart = w; b->outWrite = w; }
            while (readPos < w) { int k = (int)std::min<int64_t>(w - readPos, 2048);
                for (int i = 0; i < k; ++i) b->outBuf[(readPos + i) & mask] = 0.5f * b->inBuf[(readPos + i) & mask];
                std::this_thread::sleep_for(microseconds(800));   // „plugin“ trvá 0,8 ms
                readPos += k; b->outWrite.store(readPos, std::memory_order_release); }
            b->hostActive = 1;
        }
    });

    // APO strana
    std::vector<float> dry(8192); size_t dryPos = 0; uint32_t lastBeat = 0, stale = 1000;
    int64_t sample = 0; int wrong = 0, wet = 0, dryUsed = 0, garbage = 0;
    auto block = [&](bool expectWet) {
        std::vector<float> x(n), y(n);
        for (uint32_t i = 0; i < n; ++i) x[i] = (float)((sample + i) % 1000 + 1);   // nenulová rampa, nikdy 0
        for (uint32_t i = 0; i < n; ++i) dry[(dryPos + i) & 8191] = x[i];
        size_t dryStart = dryPos; dryPos += n;
        uint32_t hb = b->hostBeat; if (hb != lastBeat) { lastBeat = hb; stale = 0; } else if (stale < 1000) ++stale;
        bool alive = b->hostActive == 1 && stale < 40;
        int64_t w = b->inWrite.load(); for (uint32_t i = 0; i < n; ++i) b->inBuf[(w + i) & mask] = x[i];
        b->inWrite.store(w + n, std::memory_order_release);
        { std::lock_guard<std::mutex> lk(m); sig = true; } cv.notify_one();
        if (alive) {
            int64_t ow = b->outWrite.load(std::memory_order_acquire), os = b->outStart.load(), st = w - L;
            for (uint32_t i = 0; i < n; ++i) { int64_t pos = st + i;
                if (pos >= os && pos < ow && ow - pos < (int64_t)hmo::kBridgeCap / 2) { y[i] = b->outBuf[pos & mask]; ++wet; }
                else { y[i] = dry[(dryStart + i - L) & 8191]; ++dryUsed; } }
        } else for (uint32_t i = 0; i < n; ++i) y[i] = dry[(dryStart + i - L) & 8191];
        // overenie: výstup musí byť vstup spred L vzoriek (×0.5 ak wet, ×1 ak dry), nikdy nič iné
        for (uint32_t i = 0; i < n; ++i) { int64_t src = sample + i - L; if (src < 0) continue;
            float e = (float)(src % 1000 + 1);
            if (std::fabs(y[i] - e) > 1e-3 && std::fabs(y[i] - 0.5f * e) > 1e-3) ++garbage;
            else if (expectWet && std::fabs(y[i] - 0.5f * e) > 1e-3) ++wrong; }
        sample += n;
    };
    auto t = steady_clock::now();
    auto tick = [&](int count, bool expectWet) { for (int k = 0; k < count; ++k) { block(expectWet); t += milliseconds(10); std::this_thread::sleep_until(t); } };
    tick(20, false);                 // rozbeh
    wrong = 0; garbage = 0; wet = dryUsed = 0;
    tick(300, true);                 // 3 s ustálený stav
    CHECK(garbage == 0 && wrong == 0, "ustaleny stav: presne vstup spred %u vz. (wet %d, dry %d, chyby %d/%d)", L, wet, dryUsed, wrong, garbage);
    hostRun = false;                 // host spadne
    garbage = 0; tick(80, false);
    CHECK(garbage == 0, "vypadok hosta: zaloha bez cudzieho obsahu (chyby %d)", garbage);
    hostRun = true;                  // host sa vráti
    garbage = 0; tick(50, false);
    wrong = 0; garbage = 0; tick(100, true);
    CHECK(garbage == 0 && wrong == 0, "po navrate hosta opat cisto wet (chyby %d/%d)", wrong, garbage);
    quit = true; { std::lock_guard<std::mutex> lk(m); sig = true; } cv.notify_one(); host.join();
    std::printf(fails ? "\n%d zlyhalo\n" : "\nvsetko ok\n", fails);
    return fails;
}
