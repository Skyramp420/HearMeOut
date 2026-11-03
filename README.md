# HearMeOut 🎙️

Efekty na mikrofón priamo vo Windows – čistý, „štúdiový“ hlas bez Light Hostu, bez loopbacku a bez virtuálneho kábla.
Súčasť rodiny **WheresTheApp**.

## Inštalácia

1. Stiahni najnovší `HearMeOut_x.y.z_x64-setup.exe` z [Releases](../../releases/latest) a spusti ho.
2. Otvor HearMeOut → **Nastavenia** → vyber mikrofón → **Nainštalovať** (vypýta si admin práva, zvuk na chvíľu vypadne).
3. Hotovo. Appku môžeš zavrieť – efekty bežia priamo vo Windows.

Aktualizácie: appka sa pri štarte sama pozrie, či je nová verzia, a ponúkne **Aktualizovať**.

## Čo vie

- **Vlastné efekty** – čistenie šumu, tlmenie v tichu, dynamika (Opto / Opto+FET / FET) s analógovou farbou, tón s krivkou, de-esser, limiter.
- **VST pluginy** – tvoje VST3 pluginy (napr. Speachy, Xvox) bežia v malom hostiteľovi pod tvojím účtom a zvuk ide rovno do mikrofónu. Licencie fungujú ako v Light Hoste; ak host spadne, mikrofón sa prepne na vlastné efekty.
- **Odposluch** a A/B (klávesa **B**), živé metre, presety.

## Ako to funguje

```
Focusrite ──► HearMeOut APO (Windows audio) ──► Discord / OBS / hry
                   │  vlastné efekty
                   └─► VST most ◄──► HearMeOutHost.exe (Speachy, Xvox…)
```

APO je malá DLL, ktorú Windows vloží do spracovania mikrofónu. Nastavenia číta z `C:\ProgramData\HearMeOut\config.ini`
(appka ho zapisuje, zmena platí hneď). VST reťazec a stavy pluginov sú v `%APPDATA%\HearMeOut`.

## Štruktúra

```
core/        DSP jadro (C++17) – zdieľa ho APO aj nástroje
apo/         Windows APO (COM DLL)
host/        HearMeOutHost.exe – VST3 host + testy (vlastný testovací plugin, simulácia mostu)
app/         appka (Tauri 2: Rust + HTML/CSS/JS, téma Čistá)
install/     PowerShell inštalátor efektu (appka ho spúšťa sama)
tools/       hmo-wav – offline spracovanie WAV rovnakým reťazcom
tests/       testy DSP
third_party/ VST3 pluginterfaces (MIT, Steinberg)
```

## Vydanie novej verzie

```
git tag v0.5.0 && git push origin v0.5.0
```
GitHub Actions zostaví C++ časti (MinGW), appku a inštalátor (NSIS) a vytvorí Release. Verzia sa berie z tagu.

## Lokálny build

- C++: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (MSYS2 MinGW64)
- Appka: v `app/src-tauri` → `npx @tauri-apps/cli@2 build --config tauri.release.json`
  (predtým skopíruj `HearMeOutApo.dll` + skripty z `install/` + `config/default.ini` do `app/src-tauri/payload/` a `HearMeOutHost.exe` do `app/src-tauri/`)
