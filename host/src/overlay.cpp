// Koleso zvukov: podrž skratku, ukáž myšou, pusti = prehrá. Krátke stlačenie = ostane otvorené (klik).
// Koliesko myši = strany, Q/E = kategórie, stred = stop všetko, Esc = zavrieť.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "overlay.h"
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "soundboard.h"

namespace ov {
namespace {

constexpr float kW = 760, kH = 840;            // logický rozmer okna (px pri 100 %)
constexpr float kCX = 380, kCY = 470;          // stred kolesa
constexpr float kRIn = 118, kROut = 290, kRBg = 312, kRCenter = 104;
constexpr int kSlots = 8;
constexpr float kPi = 3.14159265f;

HINSTANCE g_inst = nullptr;
HWND g_wnd = nullptr;
ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dw = nullptr;
ID2D1DCRenderTarget* g_rt = nullptr;
ID2D1SolidColorBrush* g_br = nullptr;
IDWriteTextFormat *g_fName = nullptr, *g_fSmall = nullptr, *g_fCenter = nullptr, *g_fTab = nullptr, *g_fHint = nullptr;
HDC g_mem = nullptr; HBITMAP g_dib = nullptr, g_oldBmp = nullptr;
int g_pw = 0, g_ph = 0;      // fyzické pixely
float g_s = 1;               // mierka (DPI)
POINT g_pos{};

bool g_open = false, g_closing = false;
double g_tOpen = 0, g_tClose = 0, g_tPage = 0, g_last = 0;
int g_pageDir = 0;
UINT g_heldVk = 0, g_openVk = 0;
double g_heldSince = 0;
HWND g_prevFg = nullptr;
sb::View g_view;
std::vector<int> g_list;     // indexy do g_view.pads pre aktuálnu kategóriu
int g_tab = 0, g_page = 0, g_hover = -1;   // hover: -1 nič, -2 stred, 0..7 slot
float g_hAnim[kSlots] = {};
float g_centerAnim = 0;

double now() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
std::wstring W(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float easeOutBack(float t) { const float c1 = 1.5f, c3 = c1 + 1; t -= 1; return 1 + c3 * t * t * t + c1 * t * t; }
float easeOutCubic(float t) { t = 1 - t; return 1 - t * t * t; }

int tabCount() { return 2 + (int)g_view.cats.size(); }
std::wstring tabName(int i) {
    if (i == 0) return L"Všetko";
    if (i == 1) return L"★ Obľúbené";
    return W(g_view.cats[i - 2]);
}
void rebuildList() {
    g_list.clear();
    for (int i = 0; i < (int)g_view.pads.size(); ++i) {
        const auto& p = g_view.pads[i];
        if (g_tab == 0 || (g_tab == 1 && p.fav) || (g_tab >= 2 && p.cat == g_view.cats[g_tab - 2])) g_list.push_back(i);
    }
}
int pageCount() { return std::max(1, ((int)g_list.size() + kSlots - 1) / kSlots); }
int padAt(int slot) {
    const int i = g_page * kSlots + slot;
    return (slot >= 0 && i < (int)g_list.size()) ? g_list[i] : -1;
}

void setTab(int t) {
    const int n = tabCount();
    g_tab = ((t % n) + n) % n;
    g_page = 0; rebuildList();
    g_pageDir = 0; g_tPage = now();
}
void setPage(int p) {
    const int n = pageCount();
    if (n <= 1) return;
    const int np = ((p % n) + n) % n;
    g_pageDir = p > g_page ? 1 : -1;
    g_page = np; g_tPage = now();
}

// ---------------------------------------------------------------- kreslenie
float g_alpha = 1;
ID2D1SolidColorBrush* col(float r, float g, float b, float a) { g_br->SetColor(D2D1::ColorF(r, g, b, a * g_alpha)); return g_br; }
D2D1_POINT_2F P(float r, float deg) { float a = deg * kPi / 180; return D2D1::Point2F(kCX + r * std::cos(a), kCY + r * std::sin(a)); }

ID2D1PathGeometry* sector(float r0, float r1, float a0, float a1) {
    ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* s = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&g))) return nullptr;
    if (FAILED(g->Open(&s))) { g->Release(); return nullptr; }
    s->BeginFigure(P(r0, a0), D2D1_FIGURE_BEGIN_FILLED);
    s->AddLine(P(r1, a0));
    s->AddArc(D2D1::ArcSegment(P(r1, a1), D2D1::SizeF(r1, r1), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    s->AddLine(P(r0, a1));
    s->AddArc(D2D1::ArcSegment(P(r0, a0), D2D1::SizeF(r0, r0), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close(); s->Release();
    return g;
}
void text(const std::wstring& t, IDWriteTextFormat* f, float x, float y, float w, float h, ID2D1Brush* b) {
    g_rt->DrawText(t.c_str(), (UINT32)t.size(), f, D2D1::RectF(x, y, x + w, y + h), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
float textWidth(const std::wstring& t, IDWriteTextFormat* f) {
    IDWriteTextLayout* l = nullptr;
    if (FAILED(g_dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, 1000, 100, &l))) return 60;
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release();
    return m.widthIncludingTrailingWhitespace;
}

void render() {
    const double t = now();
    float dt = (float)std::min(0.05, t - g_last); g_last = t;
    // animácie
    float open = g_closing ? 1 - clamp01((float)((t - g_tClose) / 0.13)) : clamp01((float)((t - g_tOpen) / 0.26));
    float scale = g_closing ? 0.94f + 0.06f * open : 0.84f + 0.16f * easeOutBack(open);
    g_alpha = g_closing ? open : easeOutCubic(std::min(1.0f, open * 1.6f));
    const float pageT = easeOutCubic(clamp01((float)((t - g_tPage) / 0.24)));
    for (int i = 0; i < kSlots; ++i) { float target = g_hover == i ? 1.f : 0.f; g_hAnim[i] += (target - g_hAnim[i]) * std::min(1.0f, dt * 16); }
    g_centerAnim += ((g_hover == -2 ? 1.f : 0.f) - g_centerAnim) * std::min(1.0f, dt * 16);

    RECT rc{ 0, 0, g_pw, g_ph };
    g_rt->BindDC(g_mem, &rc);
    g_rt->BeginDraw();
    g_rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(kCX, kCY)) * D2D1::Matrix3x2F::Scale(g_s, g_s));

    // pozadie: mäkký tieň + disk
    for (int i = 6; i >= 1; --i) g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY + 6), kRBg + i * 6.0f, kRBg + i * 6.0f), col(0, 0, 0, 0.045f));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY), kRBg, kRBg), col(0.11f, 0.11f, 0.12f, 0.9f));
    g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY), kRBg - 0.5f, kRBg - 0.5f), col(1, 1, 1, 0.08f), 1);

    // sloty
    const float rot = g_pageDir * (1 - pageT) * 16.0f;
    const float contentA = g_pageDir ? pageT : std::max(pageT, 0.0f);
    for (int k = 0; k < kSlots; ++k) {
        const int pi = padAt(k);
        const float h = g_hAnim[k];
        const float mid = -90.0f + k * (360.0f / kSlots);
        const float half = 180.0f / kSlots - 1.4f;
        const float r1 = kROut + 12 * h, r0 = kRIn + 2 * h;
        if (ID2D1PathGeometry* g = sector(r0, r1, mid - half, mid + half)) {
            if (pi < 0) g_rt->FillGeometry(g, col(1, 1, 1, 0.025f));
            else {
                const bool playing = sb::isPlaying(g_view.pads[pi].id);
                // neutrálna -> akcent
                g_rt->FillGeometry(g, col(1, 1, 1, 0.065f * (1 - h)));
                if (h > 0.01f) g_rt->FillGeometry(g, col(1.0f, 0.624f, 0.039f, 0.95f * h));
                if (playing) g_rt->DrawGeometry(g, col(1.0f, 0.624f, 0.039f, 0.9f), 2);
            }
            g->Release();
        }
        if (pi < 0) continue;
        const auto& p = g_view.pads[pi];
        // text v strede výseku
        const float tr = (kRIn + kROut) / 2 + 6 * h;
        const D2D1_POINT_2F c = P(tr, mid + rot);
        const float ink = h;   // na akcentovom pozadí tmavý text
        const float tcR = 0.96f * (1 - ink) + 0.11f * ink;
        const float sa = g_alpha; g_alpha *= contentA;
        std::wstring name = (p.fav ? L"★ " : L"") + W(p.name);
        text(name, g_fName, c.x - 64, c.y - 22, 128, 38, col(tcR, tcR, tcR * 0.98f + 0.0f, 1));
        if (!p.hk.empty()) text(W(p.hk), g_fSmall, c.x - 64, c.y + 17, 128, 16, col(tcR, tcR, tcR, ink > 0.5f ? 0.7f : 0.5f));
        g_alpha = sa;
    }

    // stred
    const float cr = kRCenter + 3 * g_centerAnim;
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY), cr, cr), col(0.17f, 0.17f, 0.18f, 0.98f));
    if (g_centerAnim > 0.01f) g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY), cr, cr), col(1.0f, 0.27f, 0.23f, 0.22f * g_centerAnim));
    g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(kCX, kCY), cr, cr), col(1, 1, 1, 0.09f), 1);
    std::wstring big, sub;
    if (g_hover >= 0 && padAt(g_hover) >= 0) { const auto& p = g_view.pads[padAt(g_hover)]; big = W(p.name); sub = p.cat.empty() ? L"" : W(p.cat); }
    else if (g_hover == -2) { big = L"Stop"; sub = L"zastaviť všetko"; }
    else if (g_list.empty()) { big = L"Prázdne"; sub = g_tab == 1 ? L"označ si obľúbené v appke" : L"žiadne zvuky"; }
    else { big = L"Vyber zvuk"; sub = L""; }
    text(big, g_fCenter, kCX - 88, kCY - 40, 176, 52, col(0.97f, 0.97f, 0.98f, 1));
    if (!sub.empty()) text(sub, g_fSmall, kCX - 88, kCY + 14, 176, 16, col(0.65f, 0.65f, 0.68f, 1));
    // strany
    const int pc = pageCount();
    if (pc > 1) {
        const float w = pc * 12.0f;
        for (int i = 0; i < pc; ++i) {
            const bool on = i == g_page;
            g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(kCX - w / 2 + i * 12 + (on ? 0 : 2), kCY + 44, kCX - w / 2 + i * 12 + (on ? 10 : 8), kCY + 50), 3, 3),
                                       on ? col(1.0f, 0.624f, 0.039f, 1) : col(1, 1, 1, 0.25f));
        }
    }

    // karty kategórií (pilulky nad kolesom)
    const int n = tabCount();
    std::vector<float> wv(n);
    for (int i = 0; i < n; ++i) wv[i] = textWidth(tabName(i), g_fTab) + 28;
    int a = 0, b = n;   // ak sa nezmestia, okno okolo aktívnej
    auto sum = [&](int x, int y) { float s = 0; for (int i = x; i < y; ++i) s += wv[i] + 6; return s; };
    while (sum(a, b) > kW - 40) { if (g_tab - a > b - 1 - g_tab) ++a; else --b; }
    float x = kCX - (sum(a, b) - 6) / 2;
    for (int i = a; i < b; ++i) {
        const bool on = i == g_tab;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, 40, x + wv[i], 74), 17, 17);
        g_rt->FillRoundedRectangle(rr, on ? col(1.0f, 0.624f, 0.039f, 1) : col(0.11f, 0.11f, 0.12f, 0.88f));
        if (!on) g_rt->DrawRoundedRectangle(rr, col(1, 1, 1, 0.1f), 1);
        text(tabName(i), g_fTab, x, 47, wv[i], 22, on ? col(0.11f, 0.11f, 0.12f, 1) : col(0.9f, 0.9f, 0.92f, 1));
        x += wv[i] + 6;
    }
    text(L"Q / E  kategórie   ·   koliesko  strany   ·   Esc  zavrieť", g_fHint, 0, kCY + kRBg + 26, kW, 20, col(1, 1, 1, 0.55f));

    g_rt->EndDraw();

    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    SIZE sz{ g_pw, g_ph }; POINT src{ 0, 0 };
    UpdateLayeredWindow(g_wnd, nullptr, &g_pos, &sz, g_mem, &src, 0, &bf, ULW_ALPHA);
}

// ---------------------------------------------------------------- vstup
int hitTest(POINT screen) {
    const float lx = (screen.x - g_pos.x) / g_s, ly = (screen.y - g_pos.y) / g_s;
    const float dx = lx - kCX, dy = ly - kCY, d = std::sqrt(dx * dx + dy * dy);
    if (d < kRCenter) return -2;
    if (d < kRIn - 6) return -1;
    // smer funguje aj mimo kolesa (ako vo Fortnite)
    float ang = std::atan2(dy, dx) * 180 / kPi + 90 + 180.0f / kSlots;
    while (ang < 0) ang += 360; while (ang >= 360) ang -= 360;
    return (int)(ang / (360.0f / kSlots)) % kSlots;
}
int tabHit(POINT screen) {
    const float lx = (screen.x - g_pos.x) / g_s, ly = (screen.y - g_pos.y) / g_s;
    if (ly < 40 || ly > 74) return -1;
    const int n = tabCount();
    std::vector<float> wv(n);
    for (int i = 0; i < n; ++i) wv[i] = textWidth(tabName(i), g_fTab) + 28;
    int a = 0, b = n;
    auto sum = [&](int x, int y) { float s = 0; for (int i = x; i < y; ++i) s += wv[i] + 6; return s; };
    while (sum(a, b) > kW - 40) { if (g_tab - a > b - 1 - g_tab) ++a; else --b; }
    float x = kCX - (sum(a, b) - 6) / 2;
    for (int i = a; i < b; ++i) { if (lx >= x && lx <= x + wv[i]) return i; x += wv[i] + 6; }
    return -1;
}

void hide() {
    KillTimer(g_wnd, 1);
    ShowWindow(g_wnd, SW_HIDE);
    g_open = g_closing = false;
    g_heldVk = 0;
}
void beginClose(bool restoreFocus) {
    if (!g_open || g_closing) return;
    g_closing = true; g_tClose = now();
    if (restoreFocus && g_prevFg && IsWindow(g_prevFg)) SetForegroundWindow(g_prevFg);
}
void playSlot(int slot) {
    const int pi = padAt(slot);
    if (pi < 0) return;
    sb::play(g_view.pads[pi].id);
    beginClose(true);
}

void tick() {
    const double t = now();
    if (g_closing && t - g_tClose > 0.14) { hide(); return; }
    if (!g_closing) {
        POINT c; GetCursorPos(&c);
        g_hover = hitTest(c);
        if (g_hover >= 0 && padAt(g_hover) < 0) g_hover = -1;
        // podržať a pustiť
        if (g_heldVk && !(GetAsyncKeyState((int)g_heldVk) & 0x8000)) {
            if (t - g_heldSince > 0.28) { if (g_hover >= 0) playSlot(g_hover); else if (g_hover == -2) { sb::stopAll(); beginClose(true); } else beginClose(true); }
            g_heldVk = 0;   // krátke ťuknutie: ostane otvorené
        }
    }
    render();
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_TIMER: if (wp == 1) tick(); return 0;
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE && g_open && !g_closing) beginClose(false); return 0;
    case WM_LBUTTONDOWN: {
        POINT c; GetCursorPos(&c);
        const int th = tabHit(c);
        if (th >= 0) { setTab(th); return 0; }
        const int hh = hitTest(c);
        if (hh == -2) { sb::stopAll(); beginClose(true); }
        else if (hh >= 0) playSlot(hh);
        return 0; }
    case WM_RBUTTONDOWN: beginClose(true); return 0;
    case WM_MOUSEWHEEL: setPage(g_page + (GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1)); return 0;
    case WM_KEYDOWN:
        if (g_openVk && wp == g_openVk) return 0;   // kláves skratky (podržaný) nie je príkaz
        if (wp == VK_ESCAPE) beginClose(true);
        else if (wp == 'Q' || wp == VK_LEFT) setTab(g_tab - 1);
        else if (wp == 'E' || wp == VK_RIGHT || wp == VK_TAB) setTab(g_tab + 1);
        else if (wp == VK_UP || wp == VK_PRIOR) setPage(g_page - 1);
        else if (wp == VK_DOWN || wp == VK_NEXT) setPage(g_page + 1);
        else if (wp >= '1' && wp <= '8') playSlot((int)wp - '1');
        else if (wp >= VK_NUMPAD1 && wp <= VK_NUMPAD8) playSlot((int)wp - VK_NUMPAD1);
        return 0;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
    }
    return DefWindowProcW(h, m, wp, lp);
}

UINT monitorDpi(HMONITOR mon) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn fn = [] { HMODULE m = LoadLibraryW(L"shcore.dll"); return m ? (Fn)GetProcAddress(m, "GetDpiForMonitor") : (Fn)nullptr; }();
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y))) return x;
    return 96;
}

bool ensureSurface(int pw, int ph) {
    if (g_mem && pw == g_pw && ph == g_ph) return true;
    if (g_mem) { SelectObject(g_mem, g_oldBmp); DeleteObject(g_dib); DeleteDC(g_mem); g_mem = nullptr; }
    HDC screen = GetDC(nullptr);
    g_mem = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = pw; bi.bmiHeader.biHeight = -ph; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_dib = CreateDIBSection(g_mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!g_dib) return false;
    g_oldBmp = (HBITMAP)SelectObject(g_mem, g_dib);
    g_pw = pw; g_ph = ph;
    return true;
}

bool ensureD2D() {
    if (g_rt) return true;
    if (!g_d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d))) return false;
    if (!g_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dw)))) return false;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(g_d2d->CreateDCRenderTarget(&props, &g_rt))) return false;
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g_br);
    auto fmt = [](float size, DWRITE_FONT_WEIGHT w, IDWriteTextFormat** out, bool wrap) {
        g_dw->CreateTextFormat(L"Segoe UI Variable Display", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"sk-sk", out);
        if (!*out) g_dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"sk-sk", out);
        if (!*out) return;
        (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        (*out)->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_FAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        (*out)->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* ell = nullptr;
        if (SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(*out, &ell))) {
            DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            (*out)->SetTrimming(&tr, ell); ell->Release();
        }
    };
    fmt(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &g_fName, true);
    fmt(11.5f, DWRITE_FONT_WEIGHT_MEDIUM, &g_fSmall, false);
    fmt(19.0f, DWRITE_FONT_WEIGHT_BOLD, &g_fCenter, true);
    fmt(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &g_fTab, false);
    fmt(12.0f, DWRITE_FONT_WEIGHT_MEDIUM, &g_fHint, false);
    return g_fName && g_fSmall && g_fCenter && g_fTab && g_fHint;
}

void open(UINT heldVk) {
    if (!g_wnd || !ensureD2D()) return;
    g_view = sb::view();
    if (g_tab >= tabCount()) g_tab = 0;
    rebuildList();
    if (g_page >= pageCount()) g_page = 0;
    // monitor s hrou (popredné okno), inak s kurzorom
    g_prevFg = GetForegroundWindow();
    HMONITOR mon = g_prevFg ? MonitorFromWindow(g_prevFg, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (!mon) { POINT c; GetCursorPos(&c); mon = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST); }
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    g_s = monitorDpi(mon) / 96.0f;
    const int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (kH * g_s > mh * 0.95f) g_s = mh * 0.95f / kH;   // malé obrazovky
    const int pw = (int)std::ceil(kW * g_s), ph = (int)std::ceil(kH * g_s);
    if (!ensureSurface(pw, ph)) return;
    g_pos.x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - pw) / 2;
    g_pos.y = mi.rcMonitor.top + (mh - ph) / 2;
    SetWindowPos(g_wnd, HWND_TOPMOST, g_pos.x, g_pos.y, pw, ph, SWP_NOACTIVATE);
    SetCursorPos(g_pos.x + (int)(kCX * g_s), g_pos.y + (int)(kCY * g_s));
    for (auto& a : g_hAnim) a = 0;
    g_centerAnim = 0; g_hover = -1;
    g_open = true; g_closing = false;
    g_tOpen = g_last = now(); g_tPage = 0; g_pageDir = 0;
    g_heldVk = g_openVk = heldVk; g_heldSince = now();
    render();
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);
    SetFocus(g_wnd);
    SetTimer(g_wnd, 1, 15, nullptr);
}

} // namespace

void init(HINSTANCE inst) {
    g_inst = inst;
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"HearMeOutWheel";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"HearMeOutWheel", L"HearMeOut – koleso zvukov",
                            WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
}

void toggle(UINT heldVk) {
    if (g_open && !g_closing) { beginClose(true); return; }
    if (g_closing) hide();
    open(heldVk);
}

bool isOpen() { return g_open; }

void shutdown() {
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(g_fName); rel(g_fSmall); rel(g_fCenter); rel(g_fTab); rel(g_fHint);
    rel(g_br); rel(g_rt); rel(g_dw); rel(g_d2d);
    if (g_mem) { SelectObject(g_mem, g_oldBmp); DeleteObject(g_dib); DeleteDC(g_mem); g_mem = nullptr; }
}

} // namespace ov
