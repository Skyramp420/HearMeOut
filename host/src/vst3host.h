// HearMeOut Host — minimálny VST3 host (prenosný: Windows aj Linux kvôli testom).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Steinberg { class FUnknown; class IPlugView; class IPlugFrame; }

namespace hmo {

struct PluginImpl;

class Vst3Plugin {
public:
    Vst3Plugin();
    ~Vst3Plugin();
    Vst3Plugin(const Vst3Plugin&) = delete;
    Vst3Plugin& operator=(const Vst3Plugin&) = delete;

    // path = priečinok .vst3 (bundle) alebo priamo binárka. Vráti false a nastaví error().
    bool load(const std::string& path);
    // pripraví spracovanie (volať na GUI vlákne, keď nebeží process)
    bool prepare(double sampleRate, int maxBlock);
    void unprepare();
    // RT: stereo na mieste, n <= maxBlock
    void process(float* left, float* right, int n);

    // stav = komponent + kontrolér (vlastný kontajner)
    bool getState(std::vector<uint8_t>& out);
    bool setState(const std::vector<uint8_t>& in);

    // editor (Windows: parent = HWND)
    bool hasEditor() const;
    Steinberg::IPlugView* createEditor();   // volajúci zavolá release()

    // ako keby používateľ pohol gombíkom v okne pluginu (testy, automatizácia)
    void simulateEdit(uint32_t paramId, double normalized);

    const std::string& name() const;
    const std::string& vendor() const;
    const std::string& error() const;
    int latencySamples() const;
    bool prepared() const;

    std::atomic<bool> bypass{false};
    std::atomic<bool> dirty{false};          // zmenil sa parameter (treba uložiť stav)

private:
    std::unique_ptr<PluginImpl> d;
};

// pomocné: nájde binárku vo VST3 bundli
std::string resolveVst3Binary(const std::string& path);

} // namespace hmo
