// HearMeOut Host — implementácia minimálneho VST3 hosta.
#include "vst3host.h"

#include <cstring>
#include <map>
#include <mutex>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/gui/iplugview.h"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <sys/stat.h>
  #include <dirent.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace hmo {

// ======================================================================= pomocné FUnknown
#define HMO_REFCOUNT                                                              \
    std::atomic<int32> refs_{1};                                                  \
    uint32 PLUGIN_API addRef() override { return (uint32)++refs_; }               \
    uint32 PLUGIN_API release() override { int32 r = --refs_; if (r == 0) delete this; return (uint32)r; }

static bool iidIs(const TUID a, const FUID& b) { return FUnknownPrivate::iidEqual(a, b.toTUID()); }

static void toString128(String128 dst, const char* src) {
    int i = 0;
    for (; src[i] && i < 127; ++i) dst[i] = (char16)(unsigned char)src[i];
    dst[i] = 0;
}

// ======================================================================= IAttributeList / IMessage
class HostAttributes final : public IAttributeList {
public:
    HMO_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IAttributeList::iid)) { addRef(); *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API setInt(AttrID id, int64 v) override { ints_[id] = v; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64& v) override { auto it = ints_.find(id); if (it == ints_.end()) return kResultFalse; v = it->second; return kResultOk; }
    tresult PLUGIN_API setFloat(AttrID id, double v) override { floats_[id] = v; return kResultOk; }
    tresult PLUGIN_API getFloat(AttrID id, double& v) override { auto it = floats_.find(id); if (it == floats_.end()) return kResultFalse; v = it->second; return kResultOk; }
    tresult PLUGIN_API setString(AttrID id, const TChar* s) override {
        std::u16string v; for (; s && *s; ++s) v.push_back((char16_t)*s); strings_[id] = v; return kResultOk;
    }
    tresult PLUGIN_API getString(AttrID id, TChar* s, uint32 sizeInBytes) override {
        auto it = strings_.find(id); if (it == strings_.end() || sizeInBytes < sizeof(TChar)) return kResultFalse;
        uint32 n = std::min<uint32>((uint32)it->second.size(), sizeInBytes / sizeof(TChar) - 1);
        for (uint32 i = 0; i < n; ++i) s[i] = (TChar)it->second[i];
        s[n] = 0; return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* d, uint32 n) override {
        auto& b = bins_[id]; b.assign((const uint8_t*)d, (const uint8_t*)d + n); return kResultOk;
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& d, uint32& n) override {
        auto it = bins_.find(id); if (it == bins_.end()) return kResultFalse;
        d = it->second.data(); n = (uint32)it->second.size(); return kResultOk;
    }
private:
    std::map<std::string, int64> ints_;
    std::map<std::string, double> floats_;
    std::map<std::string, std::u16string> strings_;
    std::map<std::string, std::vector<uint8_t>> bins_;
};

class HostMessage final : public IMessage {
public:
    HMO_REFCOUNT
    HostMessage() : attrs_(new HostAttributes) {}
    ~HostMessage() { attrs_->release(); }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IMessage::iid)) { addRef(); *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    FIDString PLUGIN_API getMessageID() override { return id_.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { id_ = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { return attrs_; }
private:
    std::string id_;
    HostAttributes* attrs_;
};

// ======================================================================= IHostApplication
class HostApp final : public IHostApplication {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IHostApplication::iid)) { *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getName(String128 name) override { toString128(name, "HearMeOut"); return kResultOk; }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override {
        if (iidIs(cid, IMessage::iid) && iidIs(iid, IMessage::iid)) { *obj = static_cast<IMessage*>(new HostMessage); return kResultOk; }
        if (iidIs(cid, IAttributeList::iid) && iidIs(iid, IAttributeList::iid)) { *obj = static_cast<IAttributeList*>(new HostAttributes); return kResultOk; }
        *obj = nullptr; return kResultFalse;
    }
};
static HostApp g_hostApp;

// ======================================================================= IBStream v pamäti
class MemStream final : public IBStream, public ISizeableStream {
public:
    HMO_REFCOUNT
    MemStream() = default;
    explicit MemStream(const uint8_t* d, size_t n) : data_(d, d + n) {}
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IBStream::iid)) { addRef(); *obj = static_cast<IBStream*>(this); return kResultOk; }
        if (iidIs(iid, ISizeableStream::iid)) { addRef(); *obj = static_cast<ISizeableStream*>(this); return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API read(void* buf, int32 n, int32* got) override {
        int64 avail = (int64)data_.size() - pos_;
        int32 k = (int32)std::max<int64>(0, std::min<int64>(n, avail));
        if (k > 0) std::memcpy(buf, data_.data() + pos_, k);
        pos_ += k; if (got) *got = k;
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buf, int32 n, int32* wrote) override {
        if (n < 0) return kInvalidArgument;
        if ((size_t)(pos_ + n) > data_.size()) data_.resize((size_t)(pos_ + n));
        std::memcpy(data_.data() + pos_, buf, n);
        pos_ += n; if (wrote) *wrote = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64* res) override {
        int64 np = mode == kIBSeekSet ? p : mode == kIBSeekCur ? pos_ + p : (int64)data_.size() + p;
        if (np < 0) return kInvalidArgument;
        pos_ = np; if (res) *res = pos_;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* p) override { if (p) *p = pos_; return kResultOk; }
    tresult PLUGIN_API getStreamSize(int64& s) override { s = (int64)data_.size(); return kResultOk; }
    tresult PLUGIN_API setStreamSize(int64 s) override { data_.resize((size_t)s); return kResultOk; }
    // FUnknown je spoločný – vyrieš nejednoznačnosť
    const std::vector<uint8_t>& data() const { return data_; }
    void rewind() { pos_ = 0; }
private:
    std::vector<uint8_t> data_;
    int64 pos_ = 0;
};

// ======================================================================= zmeny parametrov (bez alokácií v RT)
class ParamQueue final : public IParamValueQueue {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IParamValueQueue::iid)) { *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return count; }
    tresult PLUGIN_API getPoint(int32 i, int32& off, ParamValue& v) override {
        if (i < 0 || i >= count) return kResultFalse; off = offs[i]; v = vals[i]; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& index) override {
        if (count >= kMax) { vals[count - 1] = v; index = count - 1; return kResultOk; }
        offs[count] = off; vals[count] = v; index = count++; return kResultOk;
    }
    static constexpr int kMax = 16;
    ParamID id = 0; int32 count = 0; int32 offs[kMax]{}; ParamValue vals[kMax]{};
};

class ParamChanges final : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IParameterChanges::iid)) { *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return used; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < used ? &q[i] : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (int32 i = 0; i < used; ++i) if (q[i].id == id) { index = i; return &q[i]; }
        if (used >= kMax) { index = -1; return nullptr; }
        q[used].id = id; q[used].count = 0; index = used; return &q[used++];
    }
    void clear() { used = 0; }
    static constexpr int kMax = 128;
    ParamQueue q[kMax];
    int32 used = 0;
};

// SPSC fronta zmien z GUI (performEdit) do audio vlákna
struct EditRing {
    static constexpr uint32_t kCap = 1024;
    struct Item { ParamID id; ParamValue v; };
    Item items[kCap];
    std::atomic<uint32_t> head{0}, tail{0};
    bool push(ParamID id, ParamValue v) {
        uint32_t h = head.load(std::memory_order_relaxed), t = tail.load(std::memory_order_acquire);
        if (h - t >= kCap) return false;
        items[h % kCap] = { id, v }; head.store(h + 1, std::memory_order_release); return true;
    }
    bool pop(Item& it) {
        uint32_t t = tail.load(std::memory_order_relaxed), h = head.load(std::memory_order_acquire);
        if (t == h) return false;
        it = items[t % kCap]; tail.store(t + 1, std::memory_order_release); return true;
    }
};

// ======================================================================= IComponentHandler
class Handler final : public IComponentHandler {
public:
    explicit Handler(PluginImpl* p) : p_(p) {}
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (iidIs(iid, FUnknown::iid) || iidIs(iid, IComponentHandler::iid)) { *obj = this; return kResultOk; }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override;
    tresult PLUGIN_API endEdit(ParamID) override;
    tresult PLUGIN_API restartComponent(int32) override;
private:
    PluginImpl* p_;
};

// ======================================================================= modul (dll / so)
struct Module {
#ifdef _WIN32
    HMODULE h = nullptr;
    void* sym(const char* n) { return h ? (void*)GetProcAddress(h, n) : nullptr; }
    bool open(const std::string& path, std::string& err) {
        int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring w(len, L'\0'); MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], len);
        h = LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) { err = "LoadLibrary zlyhal (" + std::to_string(GetLastError()) + ")"; return false; }
        using InitFn = bool (*)();
        if (auto init = (InitFn)sym("InitDll")) init();
        return true;
    }
    void close() {
        if (!h) return;
        using ExitFn = bool (*)();
        if (auto ex = (ExitFn)sym("ExitDll")) ex();
        FreeLibrary(h); h = nullptr;
    }
#else
    void* h = nullptr;
    void* sym(const char* n) { return h ? dlsym(h, n) : nullptr; }
    bool open(const std::string& path, std::string& err) {
        h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) { err = dlerror(); return false; }
        using EntryFn = bool (*)(void*);
        if (auto entry = (EntryFn)sym("ModuleEntry")) if (!entry(h)) { err = "ModuleEntry zlyhal"; return false; }
        return true;
    }
    void close() {
        if (!h) return;
        using ExitFn = bool (*)();
        if (auto ex = (ExitFn)sym("ModuleExit")) ex();
        dlclose(h); h = nullptr;
    }
#endif
};

// ======================================================================= PluginImpl
struct PluginImpl {
    Module mod;
    IPluginFactory* factory = nullptr;
    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;
    bool controllerIsComponent = false;
    IConnectionPoint* cpComp = nullptr;
    IConnectionPoint* cpCtrl = nullptr;
    Handler handler{this};
    std::string name, vendor, error;
    bool active = false;
    int maxBlock = 0;
    double rate = 48000;
    int inCh = 2, outCh = 2;
    int32 numInBuses = 0, numOutBuses = 0;
    std::vector<AudioBusBuffers> inBus, outBus;
    std::vector<std::vector<float>> scratch;   // buffery pre vedľajšie/nepoužité kanály
    std::vector<float*> inPtrs, outPtrs;
    std::vector<float> outL, outR, zero;
    ParamChanges inChanges, outChanges;
    EditRing edits;
    ProcessContext ctx{};
    Vst3Plugin* owner = nullptr;

    void destroy() {
        if (active) { if (processor) processor->setProcessing(false); component->setActive(false); active = false; }
        if (cpComp && cpCtrl) { cpComp->disconnect(cpCtrl); cpCtrl->disconnect(cpComp); }
        if (cpComp) { cpComp->release(); cpComp = nullptr; }
        if (cpCtrl) { cpCtrl->release(); cpCtrl = nullptr; }
        if (controller) {
            controller->setComponentHandler(nullptr);
            if (!controllerIsComponent) controller->terminate();
            controller->release(); controller = nullptr;
        }
        if (processor) { processor->release(); processor = nullptr; }
        if (component) { component->terminate(); component->release(); component = nullptr; }
        if (factory) { factory->release(); factory = nullptr; }
        mod.close();
    }
};

tresult PLUGIN_API Handler::performEdit(ParamID id, ParamValue v) {
    p_->edits.push(id, v);
    if (p_->owner) p_->owner->dirty = true;
    return kResultOk;
}
tresult PLUGIN_API Handler::endEdit(ParamID) { if (p_->owner) p_->owner->dirty = true; return kResultOk; }
tresult PLUGIN_API Handler::restartComponent(int32) { if (p_->owner) p_->owner->dirty = true; return kResultOk; }

// ======================================================================= cesty
static bool isDir(const std::string& p) {
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring w(len, L'\0'); MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], len);
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string resolveVst3Binary(const std::string& path) {
    if (!isDir(path)) return path;
    std::string stem = path;
    while (!stem.empty() && (stem.back() == '/' || stem.back() == '\\')) stem.pop_back();
    size_t slash = stem.find_last_of("/\\");
    std::string base = slash == std::string::npos ? stem : stem.substr(slash + 1);
    size_t dot = base.rfind(".vst3");
    if (dot != std::string::npos) base = base.substr(0, dot);
#ifdef _WIN32
    return stem + "\\Contents\\x86_64-win\\" + base + ".vst3";
#else
    return stem + "/Contents/x86_64-linux/" + base + ".so";
#endif
}

// ======================================================================= Vst3Plugin
Vst3Plugin::Vst3Plugin() : d(new PluginImpl) { d->owner = this; }
Vst3Plugin::~Vst3Plugin() { d->destroy(); }

const std::string& Vst3Plugin::name() const { return d->name; }
const std::string& Vst3Plugin::vendor() const { return d->vendor; }
const std::string& Vst3Plugin::error() const { return d->error; }
bool Vst3Plugin::prepared() const { return d->active; }
int Vst3Plugin::latencySamples() const { return d->processor ? (int)d->processor->getLatencySamples() : 0; }
bool Vst3Plugin::hasEditor() const { return d->controller != nullptr; }

bool Vst3Plugin::load(const std::string& path) {
    PluginImpl& p = *d;
    const std::string bin = resolveVst3Binary(path);
    if (!p.mod.open(bin, p.error)) return false;
    using GetFactory = IPluginFactory* (PLUGIN_API*)();
    auto gf = (GetFactory)p.mod.sym("GetPluginFactory");
    if (!gf) { p.error = "Chýba GetPluginFactory – nie je to VST3 plugin."; return false; }
    p.factory = gf();
    if (!p.factory) { p.error = "Plugin nevrátil factory."; return false; }

    PFactoryInfo fi{};
    if (p.factory->getFactoryInfo(&fi) == kResultOk) p.vendor = fi.vendor;
    { // hostiteľský kontext pre Factory3
        IPluginFactory3* f3 = nullptr;
        if (p.factory->queryInterface(IPluginFactory3::iid, (void**)&f3) == kResultOk && f3) { f3->setHostContext(&g_hostApp); f3->release(); }
    }
    // nájdi prvý audio efekt
    TUID cid{}; bool found = false;
    for (int32 i = 0; i < p.factory->countClasses(); ++i) {
        PClassInfo ci{};
        if (p.factory->getClassInfo(i, &ci) != kResultOk) continue;
        if (std::strcmp(ci.category, kVstAudioEffectClass) == 0) { std::memcpy(cid, ci.cid, sizeof(TUID)); p.name = ci.name; found = true; break; }
    }
    if (!found) { p.error = "Plugin neobsahuje audio efekt."; return false; }

    if (p.factory->createInstance(cid, IComponent::iid, (void**)&p.component) != kResultOk || !p.component) {
        p.error = "Nepodarilo sa vytvoriť komponent."; return false;
    }
    if (p.component->initialize(&g_hostApp) != kResultOk) { p.error = "Komponent sa nepodarilo inicializovať."; return false; }
    if (p.component->queryInterface(IAudioProcessor::iid, (void**)&p.processor) != kResultOk || !p.processor) {
        p.error = "Komponent nemá IAudioProcessor."; return false;
    }

    // kontrolér: buď ten istý objekt, alebo samostatná trieda
    if (p.component->queryInterface(IEditController::iid, (void**)&p.controller) == kResultOk && p.controller) {
        p.controllerIsComponent = true;
    } else {
        TUID ccid{};
        if (p.component->getControllerClassId(ccid) == kResultOk &&
            p.factory->createInstance(ccid, IEditController::iid, (void**)&p.controller) == kResultOk && p.controller) {
            if (p.controller->initialize(&g_hostApp) != kResultOk) { p.controller->release(); p.controller = nullptr; }
        }
    }
    if (p.controller) {
        p.controller->setComponentHandler(&p.handler);
        if (!p.controllerIsComponent) {
            p.component->queryInterface(IConnectionPoint::iid, (void**)&p.cpComp);
            p.controller->queryInterface(IConnectionPoint::iid, (void**)&p.cpCtrl);
            if (p.cpComp && p.cpCtrl) { p.cpComp->connect(p.cpCtrl); p.cpCtrl->connect(p.cpComp); }
        }
        // počiatočný stav komponentu do kontroléra
        MemStream s;
        if (p.component->getState(&s) == kResultOk) { s.rewind(); p.controller->setComponentState(&s); }
    }
    return true;
}

bool Vst3Plugin::prepare(double sampleRate, int maxBlock) {
    PluginImpl& p = *d;
    if (!p.processor) return false;
    unprepare();
    p.rate = sampleRate; p.maxBlock = maxBlock;

    // zbernice: hlavná stereo (alebo mono), ostatné vypnuté
    p.numInBuses = p.component->getBusCount(kAudio, kInput);
    p.numOutBuses = p.component->getBusCount(kAudio, kOutput);
    if (p.numOutBuses < 1) { p.error = "Plugin nemá audio výstup."; return false; }
    std::vector<SpeakerArrangement> ins(std::max(0, p.numInBuses), SpeakerArr::kStereo), outs(p.numOutBuses, SpeakerArr::kStereo);
    for (int32 i = 1; i < p.numInBuses; ++i) p.processor->getBusArrangement(kInput, i, ins[i]);
    for (int32 i = 1; i < p.numOutBuses; ++i) p.processor->getBusArrangement(kOutput, i, outs[i]);
    if (p.processor->setBusArrangements(ins.data(), p.numInBuses, outs.data(), p.numOutBuses) != kResultOk) {
        if (p.numInBuses > 0) ins[0] = SpeakerArr::kMono;
        outs[0] = SpeakerArr::kMono;
        p.processor->setBusArrangements(ins.data(), p.numInBuses, outs.data(), p.numOutBuses);
    }
    auto chCount = [&](BusDirection dir, int32 idx) {
        SpeakerArrangement a = 0; p.processor->getBusArrangement(dir, idx, a);
        int n = SpeakerArr::getChannelCount(a); return n > 0 ? n : 2;
    };
    for (int32 i = 0; i < p.numInBuses; ++i) p.component->activateBus(kAudio, kInput, i, i == 0);
    for (int32 i = 0; i < p.numOutBuses; ++i) p.component->activateBus(kAudio, kOutput, i, i == 0);
    for (int32 i = 0; i < p.component->getBusCount(kEvent, kInput); ++i) p.component->activateBus(kEvent, kInput, i, false);
    for (int32 i = 0; i < p.component->getBusCount(kEvent, kOutput); ++i) p.component->activateBus(kEvent, kOutput, i, false);

    p.inCh = p.numInBuses > 0 ? chCount(kInput, 0) : 0;
    p.outCh = chCount(kOutput, 0);

    // buffery
    p.outL.assign(maxBlock, 0.f); p.outR.assign(maxBlock, 0.f); p.zero.assign(maxBlock, 0.f);
    p.scratch.clear();
    p.inBus.assign(std::max(0, p.numInBuses), AudioBusBuffers{});
    p.outBus.assign(p.numOutBuses, AudioBusBuffers{});
    size_t ptrCount = 0;
    for (int32 i = 0; i < p.numInBuses; ++i) ptrCount += i == 0 ? p.inCh : chCount(kInput, i);
    p.inPtrs.assign(ptrCount, nullptr);
    size_t optrCount = 0;
    for (int32 i = 0; i < p.numOutBuses; ++i) optrCount += i == 0 ? p.outCh : chCount(kOutput, i);
    p.outPtrs.assign(optrCount, nullptr);
    size_t k = 0;
    for (int32 i = 0; i < p.numInBuses; ++i) {
        int n = i == 0 ? p.inCh : chCount(kInput, i);
        p.inBus[i].numChannels = n; p.inBus[i].channelBuffers32 = &p.inPtrs[k];
        for (int c = 0; c < n; ++c) p.inPtrs[k + c] = p.zero.data();
        k += n;
    }
    k = 0;
    for (int32 i = 0; i < p.numOutBuses; ++i) {
        int n = i == 0 ? p.outCh : chCount(kOutput, i);
        p.outBus[i].numChannels = n; p.outBus[i].channelBuffers32 = &p.outPtrs[k];
        for (int c = 0; c < n; ++c) {
            if (i == 0 && c == 0) p.outPtrs[k + c] = p.outL.data();
            else if (i == 0 && c == 1) p.outPtrs[k + c] = p.outR.data();
            else { p.scratch.emplace_back(maxBlock, 0.f); p.outPtrs[k + c] = p.scratch.back().data(); }
        }
        k += n;
    }

    ProcessSetup setup{ kRealtime, kSample32, maxBlock, sampleRate };
    if (p.processor->setupProcessing(setup) != kResultOk) { p.error = "Plugin odmietol nastavenie (" + std::to_string((int)sampleRate) + " Hz)."; return false; }
    if (p.component->setActive(true) != kResultOk) { p.error = "Plugin sa nepodarilo aktivovať."; return false; }
    p.processor->setProcessing(true);
    p.ctx = ProcessContext{};
    p.ctx.sampleRate = sampleRate;
    p.ctx.tempo = 120; p.ctx.timeSigNumerator = 4; p.ctx.timeSigDenominator = 4;
    p.ctx.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;
    p.active = true;
    return true;
}

void Vst3Plugin::unprepare() {
    PluginImpl& p = *d;
    if (!p.active) return;
    p.processor->setProcessing(false);
    p.component->setActive(false);
    p.active = false;
}

void Vst3Plugin::process(float* left, float* right, int n) {
    PluginImpl& p = *d;
    if (!p.active || bypass.load(std::memory_order_relaxed) || n <= 0 || n > p.maxBlock) return;
    p.inChanges.clear(); p.outChanges.clear();
    EditRing::Item it;
    while (p.edits.pop(it)) {
        int32 idx; IParamValueQueue* q = p.inChanges.addParameterData(it.id, idx);
        if (q) { int32 pi; q->addPoint(0, it.v, pi); }
    }
    // vstup: mono alebo stereo
    if (p.numInBuses > 0) {
        p.inBus[0].channelBuffers32[0] = left;
        if (p.inCh > 1) p.inBus[0].channelBuffers32[1] = right;
    }
    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = n;
    data.numInputs = p.numInBuses;
    data.numOutputs = p.numOutBuses;
    data.inputs = p.numInBuses > 0 ? p.inBus.data() : nullptr;
    data.outputs = p.outBus.data();
    data.inputParameterChanges = &p.inChanges;
    data.outputParameterChanges = &p.outChanges;
    data.processContext = &p.ctx;
    for (auto& b : p.outBus) b.silenceFlags = 0;
    if (p.numInBuses > 0) p.inBus[0].silenceFlags = 0;
    if (p.processor->process(data) != kResultOk) return;
    p.ctx.projectTimeSamples += n;
    std::memcpy(left, p.outL.data(), sizeof(float) * n);
    std::memcpy(right, p.outCh > 1 ? p.outR.data() : p.outL.data(), sizeof(float) * n);
}

// kontajner stavu: "HMV3" | u32 len | komponent | u32 len | kontrolér
bool Vst3Plugin::getState(std::vector<uint8_t>& out) {
    PluginImpl& p = *d;
    if (!p.component) return false;
    MemStream cs, ks;
    if (p.component->getState(&cs) != kResultOk) return false;
    if (p.controller) p.controller->getState(&ks);
    out.clear();
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); };
    out.insert(out.end(), { 'H', 'M', 'V', '3' });
    put32((uint32_t)cs.data().size()); out.insert(out.end(), cs.data().begin(), cs.data().end());
    put32((uint32_t)ks.data().size()); out.insert(out.end(), ks.data().begin(), ks.data().end());
    return true;
}

bool Vst3Plugin::setState(const std::vector<uint8_t>& in) {
    PluginImpl& p = *d;
    if (!p.component || in.size() < 12 || std::memcmp(in.data(), "HMV3", 4) != 0) return false;
    auto get32 = [&](size_t at) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)in[at + i] << (8 * i); return v; };
    size_t pos = 4;
    uint32_t cl = get32(pos); pos += 4;
    if (pos + cl + 4 > in.size()) return false;
    MemStream cs(in.data() + pos, cl); pos += cl;
    uint32_t kl = get32(pos); pos += 4;
    if (pos + kl > in.size()) return false;
    MemStream ks(in.data() + pos, kl);
    if (p.component->setState(&cs) != kResultOk) return false;
    if (p.controller) {
        cs.rewind(); p.controller->setComponentState(&cs);
        if (kl > 0) p.controller->setState(&ks);
    }
    return true;
}

void Vst3Plugin::simulateEdit(uint32_t id, double v) { d->handler.performEdit(id, v); }

IPlugView* Vst3Plugin::createEditor() {
    PluginImpl& p = *d;
    if (!p.controller) return nullptr;
    return p.controller->createView(ViewType::kEditor);
}

} // namespace hmo
