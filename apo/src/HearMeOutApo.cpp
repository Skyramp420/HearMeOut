// HearMeOut APO — systémový efekt (SFX/LFX) pre vstup mikrofónu.
// Beží vo Windows audio procese (audiodg.exe). Pravidlá: v APOProcess žiadne alokácie, zámky ani I/O.
#include "apo_sdk.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include "hmo/dsp.h"
#include "hmo/version.h"
#include "hmo/meter.h"
#include "hmo/bridge.h"
#include <vector>
#include <sddl.h>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
  #include <xmmintrin.h>
  #define HMO_HAS_SSE 1
#endif

// {6F3B2A9E-4C1D-4E8B-9A57-2D8E0C4F1B63}
static const CLSID CLSID_HearMeOutApo = { 0x6f3b2a9e, 0x4c1d, 0x4e8b, { 0x9a, 0x57, 0x2d, 0x8e, 0x0c, 0x4f, 0x1b, 0x63 } };
static const wchar_t* kClsidStr = L"{6F3B2A9E-4C1D-4E8B-9A57-2D8E0C4F1B63}";
static const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static HMODULE g_module = nullptr;
static std::atomic<long> g_objects{0}, g_locks{0};

// ------------------------------------------------------------------ cesty a súbory
static std::wstring dataDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L"C:\\ProgramData";
    return base + L"\\HearMeOut";
}

static bool readFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (1 << 20);
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        ok = out.empty() || (ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr) && rd == out.size());
    }
    CloseHandle(h);
    return ok;
}

static void writeFileAtomic(const std::wstring& path, const std::string& text) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &wr, nullptr);
    CloseHandle(h);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ------------------------------------------------------------------ APO objekt (podporuje COM agregáciu)
struct INonDelegatingUnknown {
    virtual HRESULT STDMETHODCALLTYPE NDQueryInterface(REFIID, void**) = 0;
    virtual ULONG STDMETHODCALLTYPE NDAddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE NDRelease() = 0;
};

class HearMeOutApo;
static std::atomic<HearMeOutApo*> g_statusOwner{nullptr};

// ------------------------------------------------------------------ most k HearMeOutHost.exe (jeden na proces)
static std::atomic<hmo::BridgeShared*> g_bridge{nullptr};
static std::atomic<HANDLE> g_bridgeEvent{nullptr};
static std::atomic<bool> g_bridgeTried{false};

static SECURITY_ATTRIBUTES* sharedSa(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd, const wchar_t* sddl) {
    sa = SECURITY_ATTRIBUTES{ sizeof(sa), nullptr, FALSE };
    sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) { sa.lpSecurityDescriptor = sd; return &sa; }
    return nullptr;
}

static hmo::BridgeShared* bridge() {
    hmo::BridgeShared* b = g_bridge.load(std::memory_order_acquire);
    if (b || g_bridgeTried.exchange(true)) return b;
    // host beží ako bežný (interaktívny) používateľ – potrebuje čítať aj zapisovať
    SECURITY_ATTRIBUTES sa; PSECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES* psa = sharedSa(sa, sd, L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;GA;;;IU)");
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, psa, PAGE_READWRITE, 0, sizeof(hmo::BridgeShared), HMO_BRIDGE_NAME);
    HANDLE ev = CreateEventW(psa, FALSE, FALSE, HMO_BRIDGE_EVENT);
    if (sd) LocalFree(sd);
    if (!h) { if (ev) CloseHandle(ev); return nullptr; }
    bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, sizeof(hmo::BridgeShared));
    if (!v) { CloseHandle(h); if (ev) CloseHandle(ev); return nullptr; }
    b = static_cast<hmo::BridgeShared*>(v);
    if (!existed || b->magic != hmo::kBridgeMagic) {
        std::memset(v, 0, sizeof(hmo::BridgeShared));
        b->version = hmo::kBridgeVersion;
        b->magic = hmo::kBridgeMagic;
    }
    g_bridgeEvent.store(ev);
    g_bridge.store(b, std::memory_order_release);
    return b;
}

// posledný spracovaný blok vlastníka – ostatné inštancie (iná appka s mikrofónom) si ho len skopírujú
static constexpr UINT32 kShareMax = 8192;
static float g_lastOut[kShareMax];
static std::atomic<uint32_t> g_lastSeq{0};
static std::atomic<UINT32> g_lastN{0};

// ------------------------------------------------------------------ zdieľaná pamäť pre meter (jedna na proces)
static std::atomic<hmo::MeterShared*> g_meter{nullptr};
static std::atomic<bool> g_meterTried{false};

static hmo::MeterShared* meter() {
    hmo::MeterShared* m = g_meter.load(std::memory_order_acquire);
    if (m || g_meterTried.exchange(true)) return m;
    // čítať smie každý (appka beží ako bežný používateľ), zapisovať len SYSTEM / LOCAL SERVICE / admin
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, FALSE };
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;GR;;;WD)",
                                                             SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0,
                                  sizeof(hmo::MeterShared), HMO_METER_NAME);
    if (sd) LocalFree(sd);
    if (!h) return nullptr;
    void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, sizeof(hmo::MeterShared));
    if (!v) { CloseHandle(h); return nullptr; }      // handle zámerne nezatvárame – mapovanie žije s procesom
    m = static_cast<hmo::MeterShared*>(v);
    std::memset(m, 0, sizeof(*m));
    m->magic = hmo::kMeterMagic;
    m->version = hmo::kMeterVersion;
    m->pid = GetCurrentProcessId();
    g_meter.store(m, std::memory_order_release);
    return m;
}

class HearMeOutApo final : public INonDelegatingUnknown,
                           public IAudioProcessingObject,
                           public IAudioProcessingObjectRT,
                           public IAudioProcessingObjectConfiguration,
                           public IAudioSystemEffects {
public:
    explicit HearMeOutApo(IUnknown* outer) {
        outer_ = outer ? outer : reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));
        ++g_objects;
        dir_ = dataDir();
        loadConfig(true);
        params_.fetch();
        chain_.setParams(params_.front());
        chain_.prepare((float)rate_);
        HearMeOutApo* expected = nullptr;
        g_statusOwner.compare_exchange_strong(expected, this);   // dočasne, kým sa nejaká inštancia nezamkne
        meter();
        mono_.assign(kMaxBlock, 0.0f);
        wet_.assign(kMaxBlock, 0.0f);
        dry_.assign(kDryCap, 0.0f);
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent_) {
            try { watcher_ = std::thread([this] { watchLoop(); }); } catch (...) {}
        }
    }

    ~HearMeOutApo() {
        if (stopEvent_) SetEvent(stopEvent_);
        if (watcher_.joinable()) watcher_.join();
        if (stopEvent_) CloseHandle(stopEvent_);
        HearMeOutApo* me = this;
        if (g_statusOwner.compare_exchange_strong(me, nullptr)) {
            writeFileAtomic(dir_ + L"\\status.ini", "loaded=0\n");
            if (auto* m = g_meter.load()) m->locked = 0;
        }
        --g_objects;
    }

    // ---- INonDelegatingUnknown
    HRESULT STDMETHODCALLTYPE NDQueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown))                              *ppv = static_cast<INonDelegatingUnknown*>(this);
        else if (riid == __uuidof(IAudioProcessingObject))           *ppv = static_cast<IAudioProcessingObject*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectRT))         *ppv = static_cast<IAudioProcessingObjectRT*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectConfiguration)) *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
        else if (riid == __uuidof(IAudioSystemEffects))              *ppv = static_cast<IAudioSystemEffects*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        reinterpret_cast<IUnknown*>(*ppv)->AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE NDAddRef() override { return (ULONG)++refs_; }
    ULONG STDMETHODCALLTYPE NDRelease() override {
        long r = --refs_;
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // ---- IUnknown (deleguje na outer)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { return outer_->QueryInterface(riid, ppv); }
    ULONG STDMETHODCALLTYPE AddRef() override { return outer_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return outer_->Release(); }

    // ---- IAudioProcessingObject
    HRESULT STDMETHODCALLTYPE Reset() override { chain_.reset(); return S_OK; }

    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* t) override {
        if (!t) return E_POINTER;
        *t = (HNSTIME)(chain_.latencySamples() * 10000000.0 / (rate_ > 0 ? rate_ : 48000));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** pp) override {
        if (!pp) return E_POINTER;
        auto* p = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
        if (!p) return E_OUTOFMEMORY;
        std::memset(p, 0, sizeof(*p));
        p->clsid = CLSID_HearMeOutApo;
        p->Flags = APO_FLAG_DEFAULT;
        lstrcpynW(p->szFriendlyName, L"HearMeOut", 256);
        lstrcpynW(p->szCopyrightInfo, L"HearMeOut (WheresTheApp)", 256);
        p->u32MajorVersion = HMO_VERSION_MAJOR; p->u32MinorVersion = HMO_VERSION_MINOR;
        p->u32MinInputConnections = p->u32MaxInputConnections = 1;
        p->u32MinOutputConnections = p->u32MaxOutputConnections = 1;
        p->u32MaxInstances = 0xffffffff;
        p->u32NumAPOInterfaces = 1;
        p->iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);
        *pp = p;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Initialize(UINT32, BYTE*) override {
        // APOInitSystemEffects{,2,3} – endpoint ani ďalšie údaje zatiaľ nepotrebujeme
        return locked_ ? APOERR_APO_LOCKED : S_OK;
    }

    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* req, IAudioMediaType** out) override {
        return checkFormat(opposite, req, out);
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* req, IAudioMediaType** out) override {
        return checkFormat(opposite, req, out);
    }
    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* c) override {
        if (!c) return E_POINTER;
        *c = (UINT32)channels_;
        return S_OK;
    }

    // ---- IAudioProcessingObjectConfiguration
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 nIn, APO_CONNECTION_DESCRIPTOR** in, UINT32 nOut, APO_CONNECTION_DESCRIPTOR** out) override {
        if (locked_) return APOERR_APO_LOCKED;
        if (nIn != 1 || nOut != 1 || !in || !out || !in[0] || !out[0]) return APOERR_NUM_CONNECTIONS_INVALID;
        UNCOMPRESSEDAUDIOFORMAT fi{}, fo{};
        if (!in[0]->pFormat || !out[0]->pFormat ||
            FAILED(in[0]->pFormat->GetUncompressedAudioFormat(&fi)) || FAILED(out[0]->pFormat->GetUncompressedAudioFormat(&fo)))
            return E_INVALIDARG;
        if (fi.guidFormatType != kSubtypeFloat || fi.dwBytesPerSampleContainer != 4 ||
            fi.dwSamplesPerFrame != fo.dwSamplesPerFrame || fi.fFramesPerSecond != fo.fFramesPerSecond || fi.dwSamplesPerFrame == 0)
            return APOERR_FORMAT_NOT_SUPPORTED;
        channels_ = (int)fi.dwSamplesPerFrame;
        rate_ = (int)fi.fFramesPerSecond;
        params_.fetch();
        chain_.setParams(params_.front());
        chain_.prepare((float)rate_);
        locked_ = true;
        bridgeLat_ = (uint32_t)std::min(2048.0, std::ceil(0.020 * rate_));   // 20 ms
        std::fill(dry_.begin(), dry_.end(), 0.0f); dryPos_ = 0;
        g_statusOwner.store(this);   // status hlási inštancia, ktorá reálne spracúva zvuk
        if (auto* m = meter()) {
            m->sampleRate = (uint32_t)rate_; m->channels = (uint32_t)channels_; m->locked = 1;
            m->latencyMs = 1000.0f * chain_.latencySamples() / (float)rate_;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockForProcess() override {
        if (!locked_) return APOERR_ALREADY_UNLOCKED;
        locked_ = false;
        HearMeOutApo* me = this;
        if (g_statusOwner.compare_exchange_strong(me, nullptr))
            if (auto* m = g_meter.load()) m->locked = 0;
        return S_OK;
    }

    // ---- IAudioProcessingObjectRT
    void STDMETHODCALLTYPE APOProcess(UINT32 nIn, APO_CONNECTION_PROPERTY** in, UINT32 nOut, APO_CONNECTION_PROPERTY** out) override {
        if (nIn < 1 || nOut < 1 || !in[0] || !out[0]) return;
        APO_CONNECTION_PROPERTY* ci = in[0];
        APO_CONNECTION_PROPERTY* co = out[0];
        const UINT32 n = ci->u32ValidFrameCount;
        const float* src = reinterpret_cast<const float*>(ci->pBuffer);
        float* dst = reinterpret_cast<float*>(co->pBuffer);
        const int C = channels_;

        if (params_.fetch()) chain_.setParams(params_.front());
        const hmo::Params& p = chain_.params();

        if (p.bypass) {
            if (src != dst && ci->u32BufferFlags != BUFFER_SILENT) std::memcpy(dst, src, sizeof(float) * n * C);
            co->u32ValidFrameCount = n;
            co->u32BufferFlags = ci->u32BufferFlags;
            float pk = 0;
            if (ci->u32BufferFlags != BUFFER_SILENT) {
                const int ch = (p.inputChannel >= 0 && p.inputChannel < C) ? p.inputChannel : 0;
                for (UINT32 i = 0; i < n; ++i) pk = std::max(pk, std::fabs(src[(size_t)i * C + ch]));
            }
            blocks_.fetch_add(1, std::memory_order_relaxed);
            publishMeter(pk, pk, n, true);
            return;
        }

#ifdef HMO_HAS_SSE
        const unsigned csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);   // flush-to-zero + denormals-are-zero
#endif
        const bool silent = ci->u32BufferFlags == BUFFER_SILENT;
        const int ch = p.inputChannel;
        // vlastníka prevezme ktokoľvek zamknutý, ak pôvodný skončil
        if (!g_statusOwner.load(std::memory_order_relaxed)) { HearMeOutApo* none = nullptr; g_statusOwner.compare_exchange_strong(none, this); }
        const bool owner = g_statusOwner.load(std::memory_order_relaxed) == this;
        float pin = 0, pout = 0;

        for (UINT32 off = 0; off < n; off += kMaxBlock) {
            const UINT32 m = std::min<UINT32>(kMaxBlock, n - off);
            // 1) mono vstup
            for (UINT32 i = 0; i < m; ++i) {
                float x = 0;
                if (!silent) {
                    const float* f = src + (size_t)(off + i) * C;
                    if (ch >= 0 && ch < C) x = f[ch];
                    else { for (int c = 0; c < C; ++c) x += f[c]; x /= (float)C; }
                }
                mono_[i] = x;
                pin = std::max(pin, std::fabs(x));
            }
            // 2) VST cez hosta, inak vlastné efekty
            engineState_ = 0;
            bool done = false;
            if (p.engine == 1) {
                engineState_ = 2;
                done = owner ? bridgeProcess(m, p) : copyOwnerOutput(m);
                if (done) engineState_ = 1;
            }
            if (!done) for (UINT32 i = 0; i < m; ++i) wet_[i] = chain_.process(mono_[i]);
            if (owner && n <= kShareMax) publishShared(off, m, n);
            // 3) do všetkých kanálov
            for (UINT32 i = 0; i < m; ++i) {
                float y = wet_[i];
                pout = std::max(pout, std::fabs(y));
                float* o = dst + (size_t)(off + i) * C;
                for (int c = 0; c < C; ++c) o[c] = y;
            }
        }
#ifdef HMO_HAS_SSE
        _mm_setcsr(csr);
#endif
        if (pin > peakIn_.load(std::memory_order_relaxed)) peakIn_.store(pin, std::memory_order_relaxed);
        if (pout > peakOut_.load(std::memory_order_relaxed)) peakOut_.store(pout, std::memory_order_relaxed);
        gr_.store(chain_.compGainReductionDb(), std::memory_order_relaxed);
        blocks_.fetch_add(1, std::memory_order_relaxed);
        publishMeter(pin, pout, n, p.bypass || engineState_ == 1);

        co->u32ValidFrameCount = n;
        co->u32BufferFlags = BUFFER_VALID;
    }
    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 n) override { return n; }

    // RT: pošli blok hostovi a vezmi spracovaný blok spred bridgeLat_ vzoriek. false = host nebeží.
    bool bridgeProcess(UINT32 m, const hmo::Params& p) {
        hmo::BridgeShared* b = bridge();
        // suchá oneskorená kópia (rovnaká latencia ako most) – použije sa pri výpadku hosta
        for (UINT32 i = 0; i < m; ++i) dry_[(dryPos_ + i) & (kDryCap - 1)] = mono_[i] * inGain(p);
        const size_t dryStart = dryPos_;
        dryPos_ += m;
        if (!b) return false;
        b->sampleRate.store((uint32_t)rate_, std::memory_order_relaxed);
        b->latency.store(bridgeLat_, std::memory_order_relaxed);
        b->apoBeat.fetch_add(1, std::memory_order_relaxed);
        const uint32_t hb = b->hostBeat.load(std::memory_order_relaxed);
        if (hb != lastHostBeat_) { lastHostBeat_ = hb; staleBlocks_ = 0; } else if (staleBlocks_ < 1000) ++staleBlocks_;
        const bool alive = b->hostActive.load(std::memory_order_acquire) == 1 && staleBlocks_ < 40;

        const int64_t w = b->inWrite.load(std::memory_order_relaxed);
        const float g = inGain(p);
        for (UINT32 i = 0; i < m; ++i) b->inBuf[(w + i) & (hmo::kBridgeCap - 1)] = mono_[i] * g;
        b->inWrite.store(w + m, std::memory_order_release);
        if (HANDLE ev = g_bridgeEvent.load(std::memory_order_relaxed)) SetEvent(ev);
        if (!alive) return false;

        const int64_t ow = b->outWrite.load(std::memory_order_acquire);
        const int64_t os = b->outStart.load(std::memory_order_acquire);
        const int64_t start = w - (int64_t)bridgeLat_;
        const float og = hmo::dbToLin(p.outputGainDb);
        bool under = false;
        for (UINT32 i = 0; i < m; ++i) {
            const int64_t pos = start + i;
            float y;
            if (pos >= os && pos < ow && ow - pos < (int64_t)hmo::kBridgeCap / 2) y = b->outBuf[pos & (hmo::kBridgeCap - 1)];
            else { y = dry_[(dryStart + i - bridgeLat_) & (kDryCap - 1)]; under = true; }
            wet_[i] = std::max(-1.0f, std::min(1.0f, y * og));
        }
        if (under) b->underruns.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    static float inGain(const hmo::Params& p) { return hmo::dbToLin(p.inputGainDb); }

    // RT: druhá appka s mikrofónom – vezmi rovnaký blok, aký spracoval vlastník (seqlock)
    bool copyOwnerOutput(UINT32 m) {
        const uint32_t s1 = g_lastSeq.load(std::memory_order_acquire);
        if (s1 & 1) return false;
        const UINT32 have = g_lastN.load(std::memory_order_relaxed);
        if (have < m) return false;
        for (UINT32 i = 0; i < m; ++i) wet_[i] = g_lastOut[have - m + i];
        std::atomic_thread_fence(std::memory_order_acquire);
        return g_lastSeq.load(std::memory_order_relaxed) == s1 && s1 != 0;
    }
    void publishShared(UINT32 off, UINT32 m, UINT32 n) {
        if (off == 0) { g_lastSeq.fetch_add(1, std::memory_order_acq_rel); g_lastN.store(0, std::memory_order_relaxed); }
        for (UINT32 i = 0; i < m; ++i) g_lastOut[off + i] = wet_[i];
        if (off + m == n) { g_lastN.store(n, std::memory_order_relaxed); g_lastSeq.fetch_add(1, std::memory_order_release); }
    }

    // RT-safe: len zápis pár čísel do zdieľanej pamäte
    void publishMeter(float pin, float pout, UINT32 n, bool bypass) {
        if (g_statusOwner.load(std::memory_order_relaxed) != this) return;
        hmo::MeterShared* m = g_meter.load(std::memory_order_relaxed);
        if (!m) return;
        float decay = std::exp(-(float)n / (0.3f * (float)rate_));     // 300 ms dozvuk
        lvlIn_  = std::max(pin,  lvlIn_ * decay);
        lvlOut_ = std::max(pout, lvlOut_ * decay);
        m->inDb  = hmo::linToDb(lvlIn_);
        m->outDb = hmo::linToDb(lvlOut_);
        m->gateDb = bypass ? 0.0f : chain_.gateGainDb();
        m->compGrDb = bypass ? 0.0f : chain_.compGainReductionDb();
        m->deessDb = bypass ? 0.0f : chain_.deessGainDb();
        m->engineState = (float)engineState_;
        m->blocks = blocks_.load(std::memory_order_relaxed);
        m->locked = 1;
        std::atomic_thread_fence(std::memory_order_release);
        m->seq = m->seq + 1;
    }
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 n) override { return n; }

private:
    HRESULT checkFormat(IAudioMediaType* opposite, IAudioMediaType* req, IAudioMediaType** out) {
        if (!req || !out) return E_POINTER;
        *out = nullptr;
        UNCOMPRESSEDAUDIOFORMAT f{};
        HRESULT hr = req->GetUncompressedAudioFormat(&f);
        if (FAILED(hr)) return hr;
        if (f.guidFormatType != kSubtypeFloat || f.dwBytesPerSampleContainer != 4) return APOERR_FORMAT_NOT_SUPPORTED;
        if (opposite) {
            UNCOMPRESSEDAUDIOFORMAT o{};
            if (SUCCEEDED(opposite->GetUncompressedAudioFormat(&o)) &&
                (o.dwSamplesPerFrame != f.dwSamplesPerFrame || o.fFramesPerSecond != f.fFramesPerSecond))
                return APOERR_FORMAT_NOT_SUPPORTED;
        }
        req->AddRef();
        *out = req;
        return S_OK;
    }

    void loadConfig(bool force) {
        std::wstring path = dir_ + L"\\config.ini";
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) { configOk_ = false; return; }
        ULONGLONG stamp = ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        if (!force && stamp == configStamp_) return;
        std::string text;
        if (!readFile(path, text)) return;     // súbor sa práve zapisuje – skúsime nabudúce
        configStamp_ = stamp;
        params_.back() = hmo::parseConfig(text);
        params_.publish();
        configOk_ = true;
    }

    void writeStatus() {
        char buf[512];
        float pi = peakIn_.exchange(0), po = peakOut_.exchange(0);
        std::snprintf(buf, sizeof(buf),
            "loaded=1\nversion=%d.%d\nlocked=%d\nconfig_ok=%d\nsample_rate=%d\nchannels=%d\nlatency_ms=%.1f\n"
            "blocks=%llu\npeak_in_db=%.1f\npeak_out_db=%.1f\ngain_reduction_db=%.1f\npid=%lu\n",
            HMO_VERSION_MAJOR, HMO_VERSION_MINOR, locked_ ? 1 : 0, configOk_ ? 1 : 0, rate_, channels_,
            1000.0 * chain_.latencySamples() / (rate_ > 0 ? rate_ : 48000),
            (unsigned long long)blocks_.load(), hmo::linToDb(pi), hmo::linToDb(po), gr_.load(),
            (unsigned long)GetCurrentProcessId());
        writeFileAtomic(dir_ + L"\\status.ini", buf);
    }

    void watchLoop() {
        int tick = 0;
        while (WaitForSingleObject(stopEvent_, 100) == WAIT_TIMEOUT) {
            loadConfig(false);
            if ((++tick % 10) == 0 && g_statusOwner.load() == this) writeStatus();   // každú sekundu
        }
    }

    IUnknown* outer_;
    std::atomic<long> refs_{1};
    std::wstring dir_;
    hmo::Chain chain_;
    hmo::TripleBuffer<hmo::Params> params_;
    ULONGLONG configStamp_ = 0;
    bool configOk_ = false;
    volatile bool locked_ = false;
    int channels_ = 2, rate_ = 48000;
    HANDLE stopEvent_ = nullptr;
    std::thread watcher_;
    std::atomic<float> peakIn_{0}, peakOut_{0}, gr_{0};
    std::atomic<unsigned long long> blocks_{0};
    float lvlIn_ = 0, lvlOut_ = 0;
    static constexpr UINT32 kMaxBlock = 2048;
    static constexpr size_t kDryCap = 8192;          // mocnina 2, > max latencia + blok
    std::vector<float> mono_, wet_, dry_;
    size_t dryPos_ = 0;
    uint32_t bridgeLat_ = 960, lastHostBeat_ = 0, staleBlocks_ = 1000;
    int engineState_ = 0;
};

// ------------------------------------------------------------------ class factory
class Factory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) { *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }   // statický objekt
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer && riid != __uuidof(IUnknown)) return CLASS_E_NOAGGREGATION;
        auto* obj = new (std::nothrow) HearMeOutApo(outer);
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->NDQueryInterface(riid, ppv);
        obj->NDRelease();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { lock ? ++g_locks : --g_locks; return S_OK; }
};
static Factory g_factory;

// ------------------------------------------------------------------ registrácia
static bool regSet(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& val) {
    HKEY h; if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h); return r == ERROR_SUCCESS;
}
static bool regSet(HKEY root, const std::wstring& key, const wchar_t* name, DWORD val) {
    HKEY h; if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(h); return r == ERROR_SUCCESS;
}

static std::wstring guidStr(const GUID& g) {
    wchar_t b[64];
    StringFromGUID2(g, b, 64);
    return b;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return E_FAIL;
    const std::wstring com = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsidStr;
    const std::wstring apo = std::wstring(L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\") + kClsidStr;
    bool ok = regSet(HKEY_LOCAL_MACHINE, com, nullptr, std::wstring(L"HearMeOut APO"))
           && regSet(HKEY_LOCAL_MACHINE, com + L"\\InprocServer32", nullptr, std::wstring(path))
           && regSet(HKEY_LOCAL_MACHINE, com + L"\\InprocServer32", L"ThreadingModel", std::wstring(L"Both"))
           && regSet(HKEY_LOCAL_MACHINE, apo, L"FriendlyName", std::wstring(L"HearMeOut"))
           && regSet(HKEY_LOCAL_MACHINE, apo, L"Copyright", std::wstring(L"HearMeOut (WheresTheApp)"))
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MajorVersion", (DWORD)HMO_VERSION_MAJOR)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MinorVersion", (DWORD)HMO_VERSION_MINOR)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"Flags", (DWORD)APO_FLAG_DEFAULT)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MinInputConnections", (DWORD)1)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MaxInputConnections", (DWORD)1)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MinOutputConnections", (DWORD)1)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MaxOutputConnections", (DWORD)1)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"MaxInstances", (DWORD)0xffffffff)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"NumAPOInterfaces", (DWORD)1)
           && regSet(HKEY_LOCAL_MACHINE, apo, L"APOInterface0", guidStr(__uuidof(IAudioProcessingObject)));
    return ok ? S_OK : E_ACCESSDENIED;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\") + kClsidStr).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsidStr).c_str());
    return S_OK;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_HearMeOutApo) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_objects.load() == 0 && g_locks.load() == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_module = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
