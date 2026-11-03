#include "hmo/params.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace hmo {

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool parseBool(const std::string& v) {
    std::string l = lower(v);
    return l == "1" || l == "true" || l == "on" || l == "yes";
}

Params parseConfig(const std::string& text, const Params& base) {
    Params p = base;
    struct F { const char* key; float* f = nullptr; bool* b = nullptr; int* i = nullptr; };
    const F table[] = {
        {"global.bypass", nullptr, &p.bypass, nullptr},
        {"global.engine", nullptr, nullptr, &p.engine},
        {"global.input_channel", nullptr, nullptr, &p.inputChannel},
        {"global.input_gain_db", &p.inputGainDb},
        {"global.output_gain_db", &p.outputGainDb},

        {"highpass.enabled", nullptr, &p.hpEnabled},
        {"highpass.freq", &p.hpFreq},

        {"denoise.enabled", nullptr, &p.nrEnabled},
        {"denoise.reduction_db", &p.nrReductionDb},
        {"denoise.strength", &p.nrStrength},

        {"gate.enabled", nullptr, &p.gateEnabled},
        {"gate.threshold_db", &p.gateThresholdDb},
        {"gate.ratio", &p.gateRatio},
        {"gate.range_db", &p.gateRangeDb},
        {"gate.attack_ms", &p.gateAttackMs},
        {"gate.hold_ms", &p.gateHoldMs},
        {"gate.release_ms", &p.gateReleaseMs},

        {"eq.enabled", nullptr, &p.eqEnabled},
        {"eq.low_freq", &p.eqLowFreq},         {"eq.low_gain_db", &p.eqLowGainDb},
        {"eq.mud_freq", &p.eqMudFreq},         {"eq.mud_gain_db", &p.eqMudGainDb},   {"eq.mud_q", &p.eqMudQ},
        {"eq.presence_freq", &p.eqPresFreq},   {"eq.presence_gain_db", &p.eqPresGainDb}, {"eq.presence_q", &p.eqPresQ},
        {"eq.air_freq", &p.eqAirFreq},         {"eq.air_gain_db", &p.eqAirGainDb},

        {"compressor.enabled", nullptr, &p.compEnabled},
        {"compressor.mode", nullptr, nullptr, &p.compMode},
        {"compressor.threshold_db", &p.compThresholdDb},
        {"compressor.ratio", &p.compRatio},
        {"compressor.knee_db", &p.compKneeDb},
        {"compressor.attack_ms", &p.compAttackMs},
        {"compressor.release_ms", &p.compReleaseMs},
        {"compressor.auto_makeup", nullptr, &p.compAutoMakeup},
        {"compressor.makeup_db", &p.compMakeupDb},

        {"saturation.enabled", nullptr, &p.satEnabled},
        {"saturation.drive", &p.satDrive},

        {"deesser.enabled", nullptr, &p.deessEnabled},
        {"deesser.freq", &p.deessFreq},
        {"deesser.threshold_db", &p.deessThresholdDb},
        {"deesser.range_db", &p.deessRangeDb},

        {"limiter.enabled", nullptr, &p.limEnabled},
        {"limiter.ceiling_db", &p.limCeilingDb},
        {"limiter.release_ms", &p.limReleaseMs},
    };

    std::string section;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = lower(trim(line.substr(1, line.size() - 2))); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = section + "." + lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        size_t c = val.find_first_of(";#");
        if (c != std::string::npos) val = trim(val.substr(0, c));
        for (const F& f : table) {
            if (key != f.key) continue;
            if (f.f) { char* e; float v = std::strtof(val.c_str(), &e); if (e != val.c_str() && std::isfinite(v)) *f.f = v; }
            else if (f.b) *f.b = parseBool(val);
            else if (f.i) *f.i = std::atoi(val.c_str());
            break;
        }
    }
    return p;
}

} // namespace hmo
