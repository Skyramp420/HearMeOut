// HearMeOutHost – soundboard: načíta soundboard.ini, klávesové skratky, dekóduje a prehráva zvuky
// do mikrofónu (cez zdieľanú pamäť s APO) a do slúchadiel (miniaudio).
#pragma once
#include <string>
#include <windows.h>

namespace sb {
void start(const std::wstring& dir, HWND notify, UINT notifyMsg);   // notifyMsg sa pošle pri zmene stavu
void stop();
void reload();                         // GUI vlákno (registruje skratky)
void onHotkey(int id);                 // GUI vlákno
void play(const std::string& id);
void stopAll();
bool hasPads();
std::string statusJson();              // {"pads":N,"playing":[...],"hk_fail":[...],"mic":bool}
}
