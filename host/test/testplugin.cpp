// Vlastný testovací VST3 plugin pre test hosta: oddelený komponent + kontrolér, správy cez IConnectionPoint,
// parameter „gain“ (id 0), stav. Slúži iba na overenie HearMeOut hosta.
#include <atomic>
#include <cmath>
#include <cstring>
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
#include "pluginterfaces/vst/vstspeaker.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kCompUID(0x11111111, 0x22222222, 0x33333333, 0x44444444);
static const FUID kCtrlUID(0x55555555, 0x66666666, 0x77777777, 0x88888888);
std::atomic<int> g_msgReceived{0};

#define REFS std::atomic<int32> r_{1}; \
    uint32 PLUGIN_API addRef() override { return ++r_; } \
    uint32 PLUGIN_API release() override { int32 v = --r_; if (!v) delete this; return v; }
static bool is(const TUID a, const FUID& b) { return FUnknownPrivate::iidEqual(a, b.toTUID()); }

class Comp final : public IComponent, public IAudioProcessor, public IConnectionPoint {
public:
    REFS
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (is(iid, FUnknown::iid) || is(iid, IComponent::iid) || is(iid, IPluginBase::iid)) { addRef(); *o = static_cast<IComponent*>(this); return kResultOk; }
        if (is(iid, IAudioProcessor::iid)) { addRef(); *o = static_cast<IAudioProcessor*>(this); return kResultOk; }
        if (is(iid, IConnectionPoint::iid)) { addRef(); *o = static_cast<IConnectionPoint*>(this); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API initialize(FUnknown* ctx) override { ctx->queryInterface(IHostApplication::iid, (void**)&host_); return kResultOk; }
    tresult PLUGIN_API terminate() override { if (host_) host_->release(); host_ = nullptr; return kResultOk; }
    tresult PLUGIN_API getControllerClassId(TUID cid) override { std::memcpy(cid, kCtrlUID.toTUID(), 16); return kResultOk; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType t, BusDirection) override { return t == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType, BusDirection, int32, BusInfo& b) override { b.channelCount = 2; b.busType = kMain; return kResultOk; }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool s) override { active_ = s; return kResultOk; }
    tresult PLUGIN_API setState(IBStream* s) override { float g; int32 n = 0; s->read(&g, 4, &n); if (n == 4) gain_ = g; return kResultOk; }
    tresult PLUGIN_API getState(IBStream* s) override { float g = gain_; s->write(&g, 4); return kResultOk; }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 ni, SpeakerArrangement* out, int32 no) override {
        return ni == 1 && no == 1 && in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement& a) override { a = SpeakerArr::kStereo; return kResultOk; }
    tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& s) override { rate_ = s.sampleRate; return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& d) override {
        if (!active_) return kResultFalse;
        if (auto* pc = d.inputParameterChanges)
            for (int32 i = 0; i < pc->getParameterCount(); ++i) {
                auto* q = pc->getParameterData(i);
                int32 off; ParamValue v;
                if (q && q->getParameterId() == 0 && q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) gain_ = (float)v;
            }
        for (int c = 0; c < 2; ++c)
            for (int32 i = 0; i < d.numSamples; ++i)
                d.outputs[0].channelBuffers32[c][i] = d.inputs[0].channelBuffers32[c][i] * gain_;
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return 0; }
    tresult PLUGIN_API connect(IConnectionPoint* other) override {
        peer_ = other;
        // pošli správu cez hostom vytvorený IMessage (testuje HostApp::createInstance)
        IMessage* m = nullptr;
        TUID iid; std::memcpy(iid, IMessage::iid.toTUID(), 16);
        if (host_ && host_->createInstance(iid, iid, (void**)&m) == kResultOk && m) {
            m->setMessageID("hello");
            m->getAttributes()->setInt("value", 42);
            peer_->notify(m);
            m->release();
        }
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { peer_ = nullptr; return kResultOk; }
    tresult PLUGIN_API notify(IMessage*) override { return kResultOk; }
private:
    IHostApplication* host_ = nullptr;
    IConnectionPoint* peer_ = nullptr;
    float gain_ = 0.5f;
    bool active_ = false;
    double rate_ = 0;
};

class Ctrl final : public IEditController, public IConnectionPoint {
public:
    REFS
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (is(iid, FUnknown::iid) || is(iid, IEditController::iid) || is(iid, IPluginBase::iid)) { addRef(); *o = static_cast<IEditController*>(this); return kResultOk; }
        if (is(iid, IConnectionPoint::iid)) { addRef(); *o = static_cast<IConnectionPoint*>(this); return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }
    tresult PLUGIN_API setComponentState(IBStream* s) override { float g; int32 n = 0; s->read(&g, 4, &n); if (n == 4) gain_ = g; return kResultOk; }
    tresult PLUGIN_API setState(IBStream* s) override { char b[4]; int32 n = 0; s->read(b, 4, &n); return n == 4 && !std::memcmp(b, "ctrl", 4) ? kResultOk : kResultFalse; }
    tresult PLUGIN_API getState(IBStream* s) override { s->write((void*)"ctrl", 4); return kResultOk; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    tresult PLUGIN_API getParameterInfo(int32, ParameterInfo& i) override { std::memset(&i, 0, sizeof(i)); i.id = 0; i.flags = ParameterInfo::kCanAutomate; return kResultOk; }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override { return kResultFalse; }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kResultFalse; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID) override { return gain_; }
    tresult PLUGIN_API setParamNormalized(ParamID, ParamValue v) override { gain_ = (float)v; return kResultOk; }
    tresult PLUGIN_API setComponentHandler(IComponentHandler* h) override { handler_ = h; return kResultOk; }
    IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }
    tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* m) override {
        int64 v = 0;
        if (m && !std::strcmp(m->getMessageID(), "hello") && m->getAttributes()->getInt("value", v) == kResultOk && v == 42) ++g_msgReceived;
        return kResultOk;
    }
    // test: „otočenie gombíka“ v GUI
    IComponentHandler* handler_ = nullptr;
    float gain_ = 0.5f;
};

class Factory final : public IPluginFactory {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** o) override {
        if (is(iid, FUnknown::iid) || is(iid, IPluginFactory::iid)) { *o = this; return kResultOk; }
        *o = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* i) override { std::memset(i, 0, sizeof(*i)); std::strcpy(i->vendor, "HearMeOut Test"); return kResultOk; }
    int32 PLUGIN_API countClasses() override { return 2; }
    tresult PLUGIN_API getClassInfo(int32 idx, PClassInfo* i) override {
        std::memset(i, 0, sizeof(*i));
        if (idx == 0) { std::memcpy(i->cid, kCompUID.toTUID(), 16); std::strcpy(i->category, kVstAudioEffectClass); std::strcpy(i->name, "HMO Test Gain"); }
        else { std::memcpy(i->cid, kCtrlUID.toTUID(), 16); std::strcpy(i->category, kVstComponentControllerClass); std::strcpy(i->name, "HMO Test Gain Ctrl"); }
        i->cardinality = PClassInfo::kManyInstances;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** o) override {
        FUnknown* u = nullptr;
        if (FUnknownPrivate::iidEqual(cid, kCompUID.toTUID())) u = static_cast<IComponent*>(new Comp);
        else if (FUnknownPrivate::iidEqual(cid, kCtrlUID.toTUID())) u = static_cast<IEditController*>(new Ctrl);
        if (!u) { *o = nullptr; return kInvalidArgument; }
        tresult r = u->queryInterface(*(const TUID*)iid, o);
        u->release();
        return r;
    }
};
static Factory g_factory;

extern "C" {
__attribute__((visibility("default"))) bool ModuleEntry(void*) { return true; }
__attribute__((visibility("default"))) bool ModuleExit() { return true; }
__attribute__((visibility("default"))) IPluginFactory* GetPluginFactory() { return &g_factory; }
__attribute__((visibility("default"))) int hmoTestMessages() { return g_msgReceived.load(); }
// „GUI“: nájdi posledný kontrolér a pohni parametrom cez IComponentHandler, ako keby to spravil používateľ
}
