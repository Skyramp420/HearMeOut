// HearMeOutHost – koleso zvukov (overlay nad hrou). Kreslí Direct2D do priehľadného okna.
#pragma once
#include <windows.h>

namespace ov {
void init(HINSTANCE inst);
void toggle(UINT heldVk);   // heldVk = kláves skratky (podržať a pustiť = prehrať), 0 = len otvoriť
bool isOpen();
void shutdown();
}
