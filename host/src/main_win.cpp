// HearMeOutHost.exe — beží pod účtom používateľa, hostuje VST3 pluginy a spracúva zvuk pre APO cez most.
// Ovládanie: appka zapisuje %APPDATA%\HearMeOut\chain.ini a posiela príkazy cez WM_COPYDATA ("open <uid>", "reload", "quit").
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <dwmapi.h>
#include <avrt.h>
#include <shlobj.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "vst3host.h"
#include "hmo/bridge.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

using namespace Steinberg;

// ======================================================================= pomocné
static std::wstring W(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string U(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring appDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring d = (n > 0 && n < MAX_PATH ? std::wstring(buf, n) : L".") + L"\\HearMeOut";
    CreateDirectoryW(d.c_str(), nullptr);
    CreateDirectoryW((d + L"\\states").c_str(), nullptr);
    return d;
}
static std::wstring g_dir;
static void logLine(const std::string& s) {
    FILE* f = _wfopen((g_dir + L"\\host.log").c_str(), L"ab");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    std::fprintf(f, "%02d:%02d:%02d %s\r\n", t.wHour, t.wMinute, t.wSecond, s.c_str());
    std::fclose(f);
}
static bool readFile(const std::wstring& p, std::vector<uint8_t>& out) {
    std::ifstream f(std::filesystem::path(p), std::ios::binary); if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {}); return true;
}
static void writeFileAtomic(const std::wstring& p, const void* d, size_t n) {
    std::wstring tmp = p + L".tmp";
    { std::ofstream f(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc); if (!f) return; f.write((const char*)d, (std::streamsize)n); }
    MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
}
static std::string jsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

// ======================================================================= sloty pluginov
struct Slot;
struct EditorFrame final : public IPlugFrame {
    Slot* slot;
    explicit EditorFrame(Slot* s) : slot(s) {}
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid.toTUID()) || FUnknownPrivate::iidEqual(iid, FUnknown::iid.toTUID())) { *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override;
};

struct Slot {
    std::string uid, path;
    bool enabled = true;
    std::unique_ptr<hmo::Vst3Plugin> plug;
    std::string error;
    HWND editor = nullptr;
    IPlugView* view = nullptr;
    std::unique_ptr<EditorFrame> frame;
    bool resizing = false;
};

static std::vector<std::unique_ptr<Slot>> g_slots;          // vlastní GUI vlákno
static std::mutex g_chainMx;                                  // chráni g_chain počas spracovania
static std::vector<hmo::Vst3Plugin*> g_chain;                 // poradie spracovania (len zapnuté a pripravené)
static std::atomic<uint32_t> g_rate{0};                       // frekvencia, na ktorú sú pluginy pripravené
static constexpr int kMaxBlock = 2048;
static HWND g_msgWnd = nullptr;
static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_bridgeOk{false};
static std::atomic<uint32_t> g_bridgeRate{0};
static std::atomic<uint32_t> g_underruns{0};
static FILETIME g_chainStamp{};
static std::atomic<bool> g_reprepPending{false};
#define WM_HMO_REPREPARE (WM_APP + 1)

// vráť systému pamäť, ktorú pluginy potrebovali len pri načítaní / pri otvorenom okne
static void trimMemory() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }
static unsigned memMb() {
    PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof(pm);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof(pm))) return 0;
    return (unsigned)(pm.WorkingSetSize / (1024 * 1024));   // rovnaké číslo ako v Správcovi úloh (približne)
}

static std::wstring statePath(const Slot& s) { return g_dir + L"\\states\\" + W(s.uid) + L".state"; }

static void saveState(Slot& s) {
    if (!s.plug || !s.plug->prepared()) return;
    std::vector<uint8_t> st;
    if (s.plug->getState(st)) { writeFileAtomic(statePath(s), st.data(), st.size()); s.plug->dirty = false; }
}

static void publishChain() {
    std::vector<hmo::Vst3Plugin*> c;
    for (auto& s : g_slots) if (s->enabled && s->plug && s->plug->prepared()) c.push_back(s->plug.get());
    std::lock_guard<std::mutex> lk(g_chainMx);
    g_chain.swap(c);
}

static void prepareAll(uint32_t rate) {
    { std::lock_guard<std::mutex> lk(g_chainMx); g_chain.clear(); }
    for (auto& s : g_slots) if (s->plug && s->error.empty())
        if (!s->plug->prepare(rate, kMaxBlock)) { s->error = s->plug->error(); logLine("prepare zlyhal: " + s->path + " – " + s->error); }
    g_rate = rate;
    publishChain();
}

static void closeEditor(Slot& s) {
    if (s.view) { s.view->removed(); s.view->setFrame(nullptr); s.view->release(); s.view = nullptr; }
    if (s.editor) { HWND h = s.editor; s.editor = nullptr; DestroyWindow(h); }
    saveState(s);
    SetTimer(g_msgWnd, 3, 1500, nullptr);   // o chvíľu uvoľni pamäť po zavretom okne
}

// chain.ini: riadky "slot=<uid>|<0/1>|<cesta>"
static void reloadChain() {
    std::ifstream f(std::filesystem::path(g_dir + L"\\chain.ini"));
    struct Want { std::string uid, path; bool enabled; };
    std::vector<Want> want;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("slot=", 0) != 0) continue;
        std::string rest = line.substr(5);
        size_t a = rest.find('|'), b = a == std::string::npos ? a : rest.find('|', a + 1);
        if (b == std::string::npos) continue;
        want.push_back({ rest.substr(0, a), rest.substr(b + 1), rest.substr(a + 1, b - a - 1) == "1" });
    }
    std::vector<std::unique_ptr<Slot>> next;
    for (auto& w : want) {
        auto it = std::find_if(g_slots.begin(), g_slots.end(), [&](auto& s) { return s && s->uid == w.uid && s->path == w.path; });
        if (it != g_slots.end()) { (*it)->enabled = w.enabled; next.push_back(std::move(*it)); continue; }
        auto s = std::make_unique<Slot>();
        s->uid = w.uid; s->path = w.path; s->enabled = w.enabled;
        s->plug = std::make_unique<hmo::Vst3Plugin>();
        if (!s->plug->load(w.path)) { s->error = s->plug->error(); logLine("načítanie zlyhalo: " + w.path + " – " + s->error); }
        else {
            std::vector<uint8_t> st;
            if (readFile(statePath(*s), st) && !s->plug->setState(st)) logLine("stav sa nepodarilo obnoviť: " + w.path);
            if (g_rate && !s->plug->prepare(g_rate, kMaxBlock)) s->error = s->plug->error();
            logLine("načítaný: " + s->plug->name() + " (" + s->plug->vendor() + ")");
        }
        next.push_back(std::move(s));
    }
    // odstránené sloty: najprv z reťazca, potom zavrieť
    { std::lock_guard<std::mutex> lk(g_chainMx); g_chain.clear(); }
    for (auto& s : g_slots) if (s) { closeEditor(*s); }
    g_slots.clear();   // unique_ptr zničí plugin
    g_slots = std::move(next);
    publishChain();
    SetTimer(g_msgWnd, 3, 1500, nullptr);
}

static void checkChainFile(bool force) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW((g_dir + L"\\chain.ini").c_str(), GetFileExInfoStandard, &a)) {
        if (!g_slots.empty() || force) { g_chainStamp = {}; std::vector<std::unique_ptr<Slot>>().swap(g_slots); publishChain(); }
        return;
    }
    if (!force && CompareFileTime(&a.ftLastWriteTime, &g_chainStamp) == 0) return;
    g_chainStamp = a.ftLastWriteTime;
    reloadChain();
}

static void writeStatus() {
    std::ostringstream o;
    o << "{\"pid\":" << GetCurrentProcessId() << ",\"bridge\":" << (g_bridgeOk ? "true" : "false")
      << ",\"rate\":" << g_bridgeRate.load() << ",\"underruns\":" << g_underruns.load() << ",\"mem_mb\":" << memMb() << ",\"slots\":[";
    bool first = true;
    for (auto& s : g_slots) {
        if (!first) o << ","; first = false;
        bool ok = s->plug && s->error.empty();
        o << "{\"uid\":\"" << jsonEsc(s->uid) << "\",\"name\":\"" << jsonEsc(ok || s->plug ? s->plug->name() : "") << "\",\"vendor\":\""
          << jsonEsc(s->plug ? s->plug->vendor() : "") << "\",\"ok\":" << (ok ? "true" : "false") << ",\"error\":\"" << jsonEsc(s->error)
          << "\",\"enabled\":" << (s->enabled ? "true" : "false") << ",\"latency\":" << (ok ? s->plug->latencySamples() : 0)
          << ",\"editor\":" << (s->editor ? "true" : "false") << "}";
    }
    o << "]}";
    std::string j = o.str();
    writeFileAtomic(g_dir + L"\\host-status.json", j.data(), j.size());
}

// ======================================================================= okno editora
static UINT dpiOf(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return fn && h ? fn(h) : 96;
}
static void clientToWindow(HWND h, DWORD style, int w, int hgt, int& ow, int& oh) {
    RECT r{ 0, 0, w, hgt };
    AdjustWindowRectEx(&r, style, FALSE, 0);
    ow = r.right - r.left; oh = r.bottom - r.top;
    (void)h;
}

tresult PLUGIN_API EditorFrame::resizeView(IPlugView* view, ViewRect* r) {
    if (!slot->editor || !r) return kResultFalse;
    slot->resizing = true;
    DWORD style = (DWORD)GetWindowLongPtrW(slot->editor, GWL_STYLE);
    int w, h; clientToWindow(slot->editor, style, r->getWidth(), r->getHeight(), w, h);
    SetWindowPos(slot->editor, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT cr; GetClientRect(slot->editor, &cr);
    ViewRect nr{ 0, 0, cr.right, cr.bottom };
    view->onSize(&nr);
    slot->resizing = false;
    return kResultTrue;
}

static LRESULT CALLBACK EditorProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Slot* s = (Slot*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_SIZE:
        if (s && s->view && !s->resizing && wp != SIZE_MINIMIZED) {
            ViewRect r{ 0, 0, LOWORD(lp), HIWORD(lp) };
            if (s->view->canResize() == kResultTrue) { s->view->checkSizeConstraint(&r); s->view->onSize(&r); }
        }
        return 0;
    case WM_CLOSE:
        if (s) closeEditor(*s); else DestroyWindow(h);
        return 0;
    case WM_DPICHANGED:
        if (s && s->view) {
            IPlugViewContentScaleSupport* cs = nullptr;
            if (s->view->queryInterface(IPlugViewContentScaleSupport::iid, (void**)&cs) == kResultOk && cs) { cs->setContentScaleFactor(HIWORD(wp) / 96.0f); cs->release(); }
            RECT* nr = (RECT*)lp;
            SetWindowPos(h, nullptr, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void openEditor(Slot& s) {
    if (s.editor) { ShowWindow(s.editor, SW_SHOWNORMAL); SetForegroundWindow(s.editor); return; }
    if (!s.plug || !s.error.empty()) return;
    IPlugView* v = s.plug->createEditor();
    if (!v) { MessageBoxW(nullptr, L"Tento plugin nemá vlastné okno.", L"HearMeOut", MB_ICONINFORMATION); return; }
    if (v->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) { v->release(); return; }
    ViewRect r{};
    v->getSize(&r);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    if (v->canResize() == kResultTrue) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    int w, h; clientToWindow(nullptr, style, std::max(r.getWidth(), 200), std::max(r.getHeight(), 100), w, h);
    std::wstring title = W(s.plug->name()) + L" – HearMeOut";
    HWND hw = CreateWindowExW(0, L"HearMeOutPluginWnd", title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hw) { v->release(); return; }
    BOOL dark = TRUE; DwmSetWindowAttribute(hw, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)&s);
    s.editor = hw; s.view = v;
    s.frame = std::make_unique<EditorFrame>(&s);
    v->setFrame(s.frame.get());
    IPlugViewContentScaleSupport* cs = nullptr;
    if (v->queryInterface(IPlugViewContentScaleSupport::iid, (void**)&cs) == kResultOk && cs) { cs->setContentScaleFactor(dpiOf(hw) / 96.0f); cs->release(); }
    if (v->attached((void*)hw, kPlatformTypeHWND) != kResultOk) { logLine("attached zlyhal: " + s.plug->name()); closeEditor(s); return; }
    // niektoré pluginy zmenia veľkosť až po attached
    if (v->getSize(&r) == kResultOk) { clientToWindow(hw, style, r.getWidth(), r.getHeight(), w, h); SetWindowPos(hw, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER); }
    ShowWindow(hw, SW_SHOWNORMAL);
    SetForegroundWindow(hw);
}

// ======================================================================= audio vlákno
static void audioThread() {
    DWORD task = 0;
    HANDLE av = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    hmo::BridgeShared* b = nullptr;
    HANDLE ev = nullptr, map = nullptr;
    int64_t readPos = -1;
    std::vector<float> L(kMaxBlock), R(kMaxBlock);

    while (!g_quit) {
        if (!b) {
            map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, HMO_BRIDGE_NAME);
            ev = OpenEventW(SYNCHRONIZE, FALSE, HMO_BRIDGE_EVENT);
            if (map) b = (hmo::BridgeShared*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(hmo::BridgeShared));
            if (!b || b->magic != hmo::kBridgeMagic) {
                if (b) UnmapViewOfFile(b);
                if (map) CloseHandle(map);
                if (ev) CloseHandle(ev);
                b = nullptr; map = ev = nullptr;
                g_bridgeOk = false;
                Sleep(500);
                continue;
            }
            g_bridgeOk = true; readPos = -1;
            logLine("most pripojený");
        }
        if (ev) WaitForSingleObject(ev, 20); else Sleep(5);
        b->hostBeat.fetch_add(1, std::memory_order_relaxed);
        const uint32_t rate = b->sampleRate.load(std::memory_order_relaxed);
        g_bridgeRate = rate;
        g_underruns = b->underruns.load(std::memory_order_relaxed);
        if (rate && rate != g_rate.load()) {
            b->hostActive.store(0);
            if (!g_reprepPending.exchange(true)) PostMessageW(g_msgWnd, WM_HMO_REPREPARE, rate, 0);
            Sleep(20); continue;
        }

        const int64_t w = b->inWrite.load(std::memory_order_acquire);
        if (readPos < 0 || w < readPos || w - readPos > (int64_t)hmo::kBridgeCap / 2) {   // štart alebo výpadok – zosynchronizuj
            readPos = w;
            b->outStart.store(w, std::memory_order_release);
            b->outWrite.store(w, std::memory_order_release);
        }
        std::unique_lock<std::mutex> lk(g_chainMx);
        const bool any = !g_chain.empty();
        while (readPos < w) {
            const int n = (int)std::min<int64_t>(w - readPos, kMaxBlock);
            for (int i = 0; i < n; ++i) L[i] = R[i] = b->inBuf[(readPos + i) & (hmo::kBridgeCap - 1)];
            for (auto* p : g_chain) p->process(L.data(), R.data(), n);
            for (int i = 0; i < n; ++i) b->outBuf[(readPos + i) & (hmo::kBridgeCap - 1)] = 0.5f * (L[i] + R[i]);
            readPos += n;
            b->outWrite.store(readPos, std::memory_order_release);
        }
        lk.unlock();
        b->hostActive.store(any ? 1u : 0u, std::memory_order_release);
    }
    if (b) { b->hostActive.store(0); UnmapViewOfFile(b); }
    if (map) CloseHandle(map);
    if (ev) CloseHandle(ev);
    if (av) AvRevertMmThreadCharacteristics(av);
}

// ======================================================================= hlavné (skryté) okno
static LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COPYDATA: {
        auto* cd = (COPYDATASTRUCT*)lp;
        std::string cmd((const char*)cd->lpData, cd->cbData);
        while (!cmd.empty() && (cmd.back() == '\0' || cmd.back() == '\n')) cmd.pop_back();
        if (cmd.rfind("open ", 0) == 0) {
            std::string uid = cmd.substr(5);
            checkChainFile(false);
            for (auto& s : g_slots) if (s->uid == uid) { openEditor(*s); break; }
        } else if (cmd == "reload") checkChainFile(true);
        else if (cmd == "quit") PostQuitMessage(0);
        writeStatus();
        return TRUE;
    }
    case WM_HMO_REPREPARE:
        prepareAll((uint32_t)wp);
        g_reprepPending = false;
        logLine("pripravené na " + std::to_string((unsigned)wp) + " Hz");
        return 0;
    case WM_TIMER:
        if (wp == 1) checkChainFile(false);
        if (wp == 2) { for (auto& s : g_slots) if (s->plug && s->plug->dirty) saveState(*s); writeStatus(); }
        if (wp == 3) { KillTimer(h, 3); trimMemory(); }
        return 0;
    case WM_CLOSE:          // taskkill / inštalátor: ukonči slušne (uloží stavy pluginov)
        PostQuitMessage(0);
        return 0;
    case WM_ENDSESSION:
        for (auto& s : g_slots) saveState(*s);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\HearMeOutHostSingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    using DpiFn = BOOL(WINAPI*)(HANDLE);
    if (auto f = (DpiFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")) f((HANDLE)-4 /*PER_MONITOR_AWARE_V2*/);
    OleInitialize(nullptr);
    g_dir = appDir();
    logLine("HearMeOutHost štart");

    WNDCLASSW wc{}; wc.lpfnWndProc = MsgProc; wc.hInstance = inst; wc.lpszClassName = L"HearMeOutHostWnd";
    RegisterClassW(&wc);
    WNDCLASSW ec{}; ec.lpfnWndProc = EditorProc; ec.hInstance = inst; ec.lpszClassName = L"HearMeOutPluginWnd";
    ec.hCursor = LoadCursor(nullptr, IDC_ARROW); ec.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    ec.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&ec);
    g_msgWnd = CreateWindowExW(0, L"HearMeOutHostWnd", L"HearMeOutHost", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    // HWND_MESSAGE okná FindWindow nenájde – pre appku vytvoríme aj skryté top-level okno s rovnakou triedou
    HWND finder = CreateWindowExW(WS_EX_TOOLWINDOW, L"HearMeOutHostWnd", L"HearMeOutHost", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);

    checkChainFile(true);
    SetTimer(g_msgWnd, 1, 300, nullptr);
    SetTimer(g_msgWnd, 2, 1000, nullptr);
    std::thread audio(audioThread);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    g_quit = true;
    audio.join();
    for (auto& s : g_slots) closeEditor(*s);
    { std::lock_guard<std::mutex> lk(g_chainMx); g_chain.clear(); }
    g_slots.clear();
    writeStatus();
    if (finder) DestroyWindow(finder);
    OleUninitialize();
    ReleaseMutex(single);
    logLine("HearMeOutHost koniec");
    return 0;
}
