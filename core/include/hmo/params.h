// HearMeOut — parametre spracovania (POD, bezpečné kopírovať v RT vlákne)
#pragma once
#include <string>

namespace hmo {

struct Params {
    // global
    bool  bypass         = false;
    int   engine         = 0;      // 0 = vlastné efekty HearMeOut, 1 = VST pluginy cez HearMeOutHost
    int   inputChannel   = 0;      // -1 = priemer všetkých kanálov, inak index kanála s mikrofónom
    float inputGainDb    = 0.0f;
    float outputGainDb   = 0.0f;

    // high-pass (odstráni dunenie, hukot, nárazy do stola)
    bool  hpEnabled      = true;
    float hpFreq         = 80.0f;

    // statický denoise (adaptívny odhad šumu + spektrálne odčítanie)
    bool  nrEnabled      = true;
    float nrReductionDb  = 24.0f;  // max. potlačenie šumu
    float nrStrength     = 1.6f;   // over-subtraction (1.0 jemné … 3.0 agresívne)

    // gate / expander
    bool  gateEnabled    = true;
    float gateThresholdDb = -50.0f;
    float gateRatio      = 2.5f;
    float gateRangeDb    = 18.0f;
    float gateAttackMs   = 1.0f;
    float gateHoldMs     = 80.0f;
    float gateReleaseMs  = 120.0f;

    // EQ (teplý, plný hlas)
    bool  eqEnabled      = true;
    float eqLowFreq      = 120.0f;  float eqLowGainDb  = 2.0f;                       // low shelf – telo
    float eqMudFreq      = 320.0f;  float eqMudGainDb  = -2.5f; float eqMudQ  = 1.0f; // peak – „krabica“
    float eqPresFreq     = 3500.0f; float eqPresGainDb = 2.5f;  float eqPresQ = 0.9f; // peak – zrozumiteľnosť
    float eqAirFreq      = 10000.0f;float eqAirGainDb  = 2.0f;                       // high shelf – vzduch

    // kompresor (charakter: 0 = Opto – jemný, 1 = FET – rýchly/rádio, 2 = Opto + FET – úderný)
    bool  compEnabled    = true;
    int   compMode       = 2;
    float compThresholdDb = -24.0f;
    float compRatio      = 3.0f;
    float compKneeDb     = 6.0f;
    float compAttackMs   = 8.0f;
    float compReleaseMs  = 120.0f;
    bool  compAutoMakeup = true;   // automaticky vráti hlasitosť stratenú kompresiou
    float compMakeupDb   = 0.0f;   // + ručná korekcia

    // saturácia („farba“ – harmonické ako z analógového predzosilňovača)
    bool  satEnabled     = true;
    float satDrive       = 35.0f;  // 0 … 100

    // de-esser (sykavky s/š/c)
    bool  deessEnabled   = true;
    float deessFreq      = 6000.0f;
    float deessThresholdDb = -30.0f;
    float deessRangeDb   = 8.0f;

    // limiter (posledná poistka proti clipu)
    bool  limEnabled     = true;
    float limCeilingDb   = -1.0f;
    float limReleaseMs   = 60.0f;
};

// Načíta INI text (sekcie [global], [highpass], [denoise], [gate], [eq], [compressor], [limiter]).
// Neznáme kľúče ignoruje, chýbajúce nechá na hodnotách z `base`.
Params parseConfig(const std::string& text, const Params& base = Params{});

} // namespace hmo
