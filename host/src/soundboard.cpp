#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "soundboard.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "hmo/sfx.h"
#include "miniaudio.h"

namespace sb {
namespace {

struct Pad { std::string id, name, file, cat, label; float vol = 1; UINT mods = 0, vk = 0; bool hkOk = true, fav = false; uint32_t startMs = 0, endMs = 0; };
struct Voice {
    std::string id;
    std::shared_ptr<const std::vector<float>> pcm;
    float gMic = 1, gLoc = 1;
    size_t mic = 0, loc = 0;
    bool micDone = false, locDone = false;
};

std::wstring g_dir;
HWND g_notify = nullptr; UINT g_notifyMsg = 0;
std::mutex g_mx;                       // chráni všetko nižšie
std::vector<Pad> g_pads;
UINT g_stopMods = 0, g_stopVk = 0; bool g_stopOk = true;
UINT g_wheelMods = 0, g_wheelVk = 0; bool g_wheelOk = true;
std::vector<std::string> g_cats;
float g_micVol = 0.8f, g_locVol = 0.6f; bool g_normalize = true;
std::vector<Voice> g_voices;
std::vector<int> g_registered;
std::atomic<uint32_t> g_rate{48000};
std::atomic<bool> g_micLive{false};
std::atomic<bool> g_quit{false};
std::thread g_thread;

// miniaudio zariadenie na odposluch (inicializuje sa až pri prvom zvuku)
ma_device g_dev; bool g_devInit = false, g_devRunning = false; uint32_t g_devRate = 0;
ULONGLONG g_idleSince = 0;

constexpr int kStopHotkey = 0xB000;
constexpr int kWheelHotkey = 0xB001;
void (*g_onWheel)(UINT vk) = nullptr;
constexpr double kMaxSeconds = 180.0;

std::wstring W(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string jsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c >= 0x20) o += c; }
    return o;
}
void notify() { if (g_notify) PostMessageW(g_notify, g_notifyMsg, 0, 0); }

// odposluch – miniaudio audio vlákno
void devCallback(ma_device*, void* out, const void*, ma_uint32 n) {
    float* o = (float*)out;
    std::fill(o, o + n, 0.0f);
    std::unique_lock<std::mutex> lk(g_mx, std::try_to_lock);
    if (!lk.owns_lock()) return;
    bool ended = false;
    for (auto& v : g_voices) {
        if (v.locDone) continue;
        const auto& p = *v.pcm;
        const size_t k = std::min<size_t>(n, p.size() - std::min(v.loc, p.size()));
        for (size_t i = 0; i < k; ++i) o[i] += p[v.loc + i] * v.gLoc;
        v.loc += k;
        if (v.loc >= p.size()) { v.locDone = true; ended = true; }
    }
    for (ma_uint32 i = 0; i < n; ++i) o[i] = std::max(-1.0f, std::min(1.0f, o[i]));
    if (ended) notify();
}

void devEnsure(uint32_t rate) {   // pod zámkom
    if (g_devInit && g_devRate != rate && !g_devRunning) { ma_device_uninit(&g_dev); g_devInit = false; }
    if (!g_devInit) {
        ma_device_config c = ma_device_config_init(ma_device_type_playback);
        c.playback.format = ma_format_f32;
        c.playback.channels = 1;
        c.sampleRate = rate;
        c.dataCallback = devCallback;
        c.performanceProfile = ma_performance_profile_low_latency;
        if (ma_device_init(nullptr, &c, &g_dev) != MA_SUCCESS) return;
        g_devInit = true; g_devRate = rate;
    }
    if (!g_devRunning) g_devRunning = ma_device_start(&g_dev) == MA_SUCCESS;
}

// mono, vzorkovacia frekvencia APO
std::shared_ptr<std::vector<float>> decode(const std::wstring& path, uint32_t rate, std::string& err, uint32_t startMs = 0, uint32_t endMs = 0) {
    ma_decoder_config c = ma_decoder_config_init(ma_format_f32, 1, rate);
    ma_decoder d;
    if (ma_decoder_init_file_w(path.c_str(), &c, &d) != MA_SUCCESS) { err = "nepodporovaný alebo poškodený súbor"; return nullptr; }
    auto pcm = std::make_shared<std::vector<float>>();
    const size_t maxN = (size_t)(kMaxSeconds * rate);
    float buf[4096];
    for (;;) {
        ma_uint64 got = 0;
        if (ma_decoder_read_pcm_frames(&d, buf, 4096, &got) != MA_SUCCESS && got == 0) break;
        if (got == 0) break;
        pcm->insert(pcm->end(), buf, buf + got);
        if (pcm->size() >= maxN) { pcm->resize(maxN); break; }
    }
    ma_decoder_uninit(&d);
    // strih z appky (nedeštruktívny) + krátke dolaďovanie, nech na okrajoch necvakne
    if (startMs || endMs) {
        size_t a = std::min(pcm->size(), (size_t)((uint64_t)startMs * rate / 1000));
        size_t b = endMs ? std::min(pcm->size(), (size_t)((uint64_t)endMs * rate / 1000)) : pcm->size();
        if (b > a) {
            pcm->erase(pcm->begin() + b, pcm->end());
            pcm->erase(pcm->begin(), pcm->begin() + a);
            const size_t f = std::min(pcm->size() / 2, (size_t)(rate / 200));   // 5 ms
            for (size_t i = 0; i < f; ++i) { float g = (float)i / (float)f; (*pcm)[i] *= g; (*pcm)[pcm->size() - 1 - i] *= g; }
        }
    }
    if (pcm->empty()) { err = "prázdny zvuk"; return nullptr; }
    pcm->shrink_to_fit();
    return pcm;
}

std::wstring padPath(const Pad& p) {
    std::wstring f = W(p.file);
    if (f.find(L':') != std::wstring::npos || f.rfind(L"\\\\", 0) == 0) return f;
    return g_dir + L"\\sounds\\" + f;
}

// zapisovač do mikrofónu – vlastné vlákno
void sfxThread() {
    HANDLE map = nullptr; hmo::SfxShared* sh = nullptr;
    hmo::SfxWriter wr;
    int64_t lastRead = -1; ULONGLONG lastMove = 0, lastTry = 0;
    while (!g_quit) {
        Sleep(4);
        const ULONGLONG now = GetTickCount64();
        if (!sh && now - lastTry > 1000) {
            lastTry = now;
            map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, HMO_SFX_NAME);
            if (map) {
                sh = (hmo::SfxShared*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(hmo::SfxShared));
                if (!sh || sh->magic != hmo::kSfxMagic) { if (sh) UnmapViewOfFile(sh); CloseHandle(map); sh = nullptr; map = nullptr; }
            }
        }
        bool live = false;
        if (sh) {
            const int64_t r = sh->readPos.load(std::memory_order_acquire);
            if (r != lastRead) { lastRead = r; lastMove = now; }
            live = now - lastMove < 300;                // nikto práve nenahráva mikrofón => do mikrofónu nič
            if (uint32_t rt = sh->sampleRate.load(std::memory_order_relaxed)) g_rate = rt;
        }
        g_micLive = live;

        std::unique_lock<std::mutex> lk(g_mx);
        bool changed = false;
        if (!live) for (auto& v : g_voices) if (!v.micDone) { v.micDone = true; changed = true; }
        if (live) {
            wr.fill(sh, [&](float* d, int n) {
                bool any = false;
                std::fill(d, d + n, 0.0f);
                for (auto& v : g_voices) {
                    if (v.micDone) continue;
                    any = true;
                    const auto& p = *v.pcm;
                    const size_t k = std::min<size_t>((size_t)n, p.size() - std::min(v.mic, p.size()));
                    for (size_t i = 0; i < k; ++i) d[i] += p[v.mic + i] * v.gMic;
                    v.mic += k;
                    if (v.mic >= p.size()) { v.micDone = true; changed = true; }
                }
                return any;
            });
        }
        const size_t before = g_voices.size();
        g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [](const Voice& v) { return v.micDone && v.locDone; }), g_voices.end());
        if (g_voices.size() != before) changed = true;
        // odposluch: zastav po chvíli ticha, uvoľni po dlhšej
        const bool needDev = std::any_of(g_voices.begin(), g_voices.end(), [](const Voice& v) { return !v.locDone; });
        if (needDev) g_idleSince = 0;
        else if (g_devInit) {
            if (!g_idleSince) g_idleSince = now;
            if (g_devRunning && now - g_idleSince > 1500) { lk.unlock(); ma_device_stop(&g_dev); lk.lock(); g_devRunning = false; }
            if (!g_devRunning && now - g_idleSince > 15000) { ma_device_uninit(&g_dev); g_devInit = false; }
        }
        if (changed) notify();
    }
    if (sh) UnmapViewOfFile(sh);
    if (map) CloseHandle(map);
}

void parseHotkey(const std::string& s, UINT& mods, UINT& vk) {
    mods = vk = 0;
    size_t c = s.find(',');
    if (c == std::string::npos) return;
    mods = (UINT)std::strtoul(s.c_str(), nullptr, 10) & 0xF;
    vk = (UINT)std::strtoul(s.c_str() + c + 1, nullptr, 10) & 0xFF;
}

void registerHotkeys() {   // GUI vlákno, pod zámkom
    for (int id : g_registered) UnregisterHotKey(nullptr, id);
    g_registered.clear();
    for (size_t i = 0; i < g_pads.size(); ++i) {
        auto& p = g_pads[i];
        p.hkOk = true;
        if (!p.vk) continue;
        int id = (int)i + 1;
        if (RegisterHotKey(nullptr, id, p.mods | MOD_NOREPEAT, p.vk)) g_registered.push_back(id);
        else p.hkOk = false;
    }
    g_wheelOk = true;
    if (g_wheelVk) {
        if (RegisterHotKey(nullptr, kWheelHotkey, g_wheelMods | MOD_NOREPEAT, g_wheelVk)) g_registered.push_back(kWheelHotkey);
        else g_wheelOk = false;
    }
    g_stopOk = true;
    if (g_stopVk) {
        if (RegisterHotKey(nullptr, kStopHotkey, g_stopMods | MOD_NOREPEAT, g_stopVk)) g_registered.push_back(kStopHotkey);
        else g_stopOk = false;
    }
}

} // namespace

void start(const std::wstring& dir, HWND notifyWnd, UINT notifyMsg) {
    g_dir = dir; g_notify = notifyWnd; g_notifyMsg = notifyMsg;
    CreateDirectoryW((dir + L"\\sounds").c_str(), nullptr);
    reload();
    g_thread = std::thread(sfxThread);
}

void stop() {
    g_quit = true;
    if (g_thread.joinable()) g_thread.join();
    std::lock_guard<std::mutex> lk(g_mx);
    for (int id : g_registered) UnregisterHotKey(nullptr, id);
    g_registered.clear();
    g_voices.clear();
    if (g_devInit) { ma_device_uninit(&g_dev); g_devInit = false; g_devRunning = false; }
}

// soundboard.ini (UTF-8):
//   mic_volume=80  local_volume=60  normalize=1  stop_hotkey=<mods>,<vk>
//   pad=<id>|<hlasitosť %>|<mods>,<vk>|<súbor>|<názov>
void reload() {
    std::ifstream f(std::filesystem::path(g_dir + L"\\soundboard.ini"), std::ios::binary);
    std::vector<Pad> pads;
    float micVol = 80, locVol = 60; bool norm = true; UINT sm = 0, sv = 0, wm = 0, wv = 0;
    std::vector<std::string> cats;
    struct Meta { bool fav; uint32_t a, b; std::string cat; };
    std::vector<std::pair<std::string, Meta>> metas;
    std::vector<std::pair<std::string, std::string>> labels;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "mic_volume") micVol = (float)std::atof(v.c_str());
        else if (k == "local_volume") locVol = (float)std::atof(v.c_str());
        else if (k == "normalize") norm = v == "1";
        else if (k == "stop_hotkey") parseHotkey(v, sm, sv);
        else if (k == "wheel_hotkey") parseHotkey(v, wm, wv);
        else if (k == "cat") { if (!v.empty()) cats.push_back(v); }
        else if (k == "label") { size_t b = v.find('|'); if (b != std::string::npos) labels.push_back({ v.substr(0, b), v.substr(b + 1) }); }
        else if (k == "meta") {   // meta=<id>|<obľúbený 0/1>|<začiatok ms>|<koniec ms>|<kategória>
            std::vector<std::string> f; size_t a = 0;
            for (int i = 0; i < 4; ++i) { size_t b = v.find('|', a); if (b == std::string::npos) break; f.push_back(v.substr(a, b - a)); a = b + 1; }
            if (f.size() == 4) metas.push_back({ f[0], { f[1] == "1", (uint32_t)std::strtoul(f[2].c_str(), nullptr, 10), (uint32_t)std::strtoul(f[3].c_str(), nullptr, 10), v.substr(a) } });
        }
        else if (k == "pad") {
            std::vector<std::string> parts; size_t a = 0;
            for (int i = 0; i < 4; ++i) { size_t b = v.find('|', a); if (b == std::string::npos) break; parts.push_back(v.substr(a, b - a)); a = b + 1; }
            if (parts.size() < 4) continue;
            Pad p; p.id = parts[0]; p.vol = std::max(0.0f, std::min(2.0f, (float)std::atof(parts[1].c_str()) / 100.0f));
            parseHotkey(parts[2], p.mods, p.vk); p.file = parts[3]; p.name = v.substr(a);
            if (!p.id.empty() && !p.file.empty()) pads.push_back(std::move(p));
        }
    }
    for (auto& p : pads) {
        for (auto& m : metas) if (m.first == p.id) { p.fav = m.second.fav; p.startMs = m.second.a; p.endMs = m.second.b; p.cat = m.second.cat; }
        for (auto& l : labels) if (l.first == p.id) p.label = l.second;
    }
    std::lock_guard<std::mutex> lk(g_mx);
    g_pads = std::move(pads);
    g_cats = std::move(cats);
    g_wheelMods = wm; g_wheelVk = wv;
    g_micVol = std::max(0.0f, std::min(1.5f, micVol / 100.0f));
    g_locVol = std::max(0.0f, std::min(1.5f, locVol / 100.0f));
    g_normalize = norm; g_stopMods = sm; g_stopVk = sv;
    registerHotkeys();
}

void onHotkey(int id) {
    if (id == kStopHotkey) { stopAll(); return; }
    if (id == kWheelHotkey) { UINT vk; { std::lock_guard<std::mutex> lk(g_mx); vk = g_wheelVk; } if (g_onWheel) g_onWheel(vk); return; }
    std::string pid;
    { std::lock_guard<std::mutex> lk(g_mx); if (id >= 1 && id <= (int)g_pads.size()) pid = g_pads[id - 1].id; }
    if (!pid.empty()) play(pid);
}

void play(const std::string& id) {
    Pad pad; float micVol, locVol; bool norm;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = std::find_if(g_pads.begin(), g_pads.end(), [&](const Pad& p) { return p.id == id; });
        if (it == g_pads.end()) return;
        pad = *it; micVol = g_micVol; locVol = g_locVol; norm = g_normalize;
        // opätovné stlačenie = znova od začiatku
        for (auto& v : g_voices) if (v.id == id) v.micDone = v.locDone = true;
    }
    const uint32_t rate = g_rate.load();
    std::thread([pad, micVol, locVol, norm, rate] {
        std::string err;
        auto pcm = decode(padPath(pad), rate, err, pad.startMs, pad.endMs);
        if (!pcm) return;
        float g = pad.vol;
        if (norm) {
            float pk = 0; for (float x : *pcm) pk = std::max(pk, std::fabs(x));
            if (pk > 1e-4f) g *= std::min(8.0f, 0.7f / pk);
        }
        std::lock_guard<std::mutex> lk(g_mx);
        Voice v; v.id = pad.id; v.pcm = pcm; v.gMic = g * micVol; v.gLoc = g * locVol;
        v.micDone = !g_micLive.load() || micVol <= 0;
        v.locDone = locVol <= 0;
        if (v.micDone && v.locDone) return;
        g_voices.push_back(std::move(v));
        if (!g_voices.back().locDone) devEnsure(rate);
        notify();
    }).detach();
}

void stopAll() {
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& v : g_voices) v.micDone = v.locDone = true;
    notify();
}

View view() {
    std::lock_guard<std::mutex> lk(g_mx);
    View v; v.cats = g_cats;
    for (auto& p : g_pads) v.pads.push_back({ p.id, p.name, p.cat, p.label, p.fav });
    return v;
}
bool isPlaying(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& v : g_voices) if (v.id == id && !(v.micDone && v.locDone)) return true;
    return false;
}
void setWheelHandler(void (*fn)(UINT vk)) { g_onWheel = fn; }

bool hasPads() { std::lock_guard<std::mutex> lk(g_mx); return !g_pads.empty(); }

std::string statusJson() {
    std::lock_guard<std::mutex> lk(g_mx);
    std::ostringstream o;
    o << "{\"pads\":" << g_pads.size() << ",\"mic\":" << (g_micLive ? "true" : "false") << ",\"playing\":[";
    bool first = true;
    for (auto& v : g_voices) if (!(v.micDone && v.locDone)) { if (!first) o << ","; first = false; o << "\"" << jsonEsc(v.id) << "\""; }
    o << "],\"hk_fail\":[";
    first = true;
    for (auto& p : g_pads) if (!p.hkOk) { if (!first) o << ","; first = false; o << "\"" << jsonEsc(p.id) << "\""; }
    if (!g_stopOk) { if (!first) o << ","; first = false; o << "\"stop\""; }
    if (!g_wheelOk) { if (!first) o << ","; o << "\"wheel\""; }
    o << "]}";
    return o.str();
}

} // namespace sb
