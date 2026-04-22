#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// ImGui headers (Assume these are included in the project)
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"

// Backend interfaces
#include "api/api.h"
#include "api/session.h"
#include "hwid.h"
#include "memory/runpe.h"
#include "vmdetect.h"
#include "image_loader.h"
#include "security/obf.h"
#include "security/handoff.h"
#include "security/driver_bringup.h"
#include "util/process_wait.h"
#include "util/diaglog.h"

// ─────────────────────────────────────────────────────────────────────────────
// D3D11 plumbing
// ─────────────────────────────────────────────────────────────────────────────
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static HWND                     g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ─────────────────────────────────────────────────────────────────────────────
// Application state
// ─────────────────────────────────────────────────────────────────────────────
enum class AppState {
    Login,
    Products,
    Injecting
};

static AppState           g_state = AppState::Login;
static AppState           g_lastState = AppState::Login;
static float              g_screenFade = 1.0f;        // 0 → 1 fade-in on screen change
static Api::AuthResponse  g_authInfo;
static std::string        g_statusMessage = "";
static bool               g_statusCopied = false;      // true for ~1.5s after user copies the status
static double             g_statusCopiedTime = 0.0;    // ImGui::GetTime() at copy moment
static char               g_username[64] = "";
static char               g_password[64] = "";
static bool               g_rememberMe = false;
static std::string        g_hwid = "";

// Injection runs on a background thread so the UI stays responsive.
static std::atomic<bool>  g_injecting{ false };
static std::atomic<bool>  g_injectionSucceeded{ false };
static std::atomic<bool>  g_injectionFailed{ false };   // asset/RunPE failure (not HWID mismatch)
static std::atomic<bool>  g_shouldExit{ false };
static std::atomic<uint64_t> g_autoCloseAt{ 0 };

// HWID mismatch state
static bool        g_hwidMismatch = false;
static bool        g_hwidResetSubmitted = false;
static std::string g_hwidResetMessage = "";

// Global Popups
static std::string g_popupMessage = "";
static std::string g_popupTitle = "";
static ImVec4      g_popupColor = ImVec4(1,1,1,1);
static bool        g_popupOpen = false;

static void ShowPopup(const std::string& title, const std::string& msg, ImVec4 color) {
    g_popupTitle = title;
    g_popupMessage = msg;
    g_popupColor = color;
    g_popupOpen = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// UI constants (logical pixels @ 96 DPI; multiply by g_uiScale at runtime)
// ─────────────────────────────────────────────────────────────────────────────
static const int   WND_W_BASE         = 560;
static const int   WND_H_BASE         = 520;
static const float TITLE_BAR_H_BASE   = 42.0f;
static const float WINDOW_BORDER_PX   = 1.5f;   // rainbow stroke thickness
static const int   CTRL_BTNS_WIDTH_BASE = 48;   // exclude region for native drag (close btn only)
static const float WND_CORNER_RADIUS_BASE = 14.0f;

// Runtime DPI scale factor (1.0 at 96 DPI). Set once at startup.
static float       g_uiScale          = 1.0f;
static inline float SX(float v) { return v * g_uiScale; }
static inline int   SXi(int v)  { return (int)(v * g_uiScale); }

#define TITLE_BAR_H        SX(TITLE_BAR_H_BASE)
#define WND_CORNER_RADIUS  SX(WND_CORNER_RADIUS_BASE)
#define CTRL_BTNS_WIDTH    SXi(CTRL_BTNS_WIDTH_BASE)

// Color palette (designed to feel modern + Scootware purple)
static const ImVec4 COL_BG          = ImVec4(0.055f, 0.055f, 0.075f, 1.00f);
static const ImVec4 COL_PANEL       = ImVec4(0.090f, 0.090f, 0.115f, 1.00f);
static const ImVec4 COL_PANEL_ALT   = ImVec4(0.115f, 0.115f, 0.145f, 1.00f);
static const ImVec4 COL_BORDER      = ImVec4(0.180f, 0.130f, 0.260f, 1.00f);
static const ImVec4 COL_PURPLE      = ImVec4(0.659f, 0.333f, 0.969f, 1.00f); // #A855F7
static const ImVec4 COL_PURPLE_HOV  = ImVec4(0.753f, 0.516f, 0.988f, 1.00f); // #C084FC
static const ImVec4 COL_PURPLE_ACT  = ImVec4(0.541f, 0.235f, 0.831f, 1.00f);
static const ImVec4 COL_TEXT        = ImVec4(0.957f, 0.957f, 0.965f, 1.00f);
static const ImVec4 COL_TEXT_DIM    = ImVec4(0.620f, 0.620f, 0.660f, 1.00f);
static const ImVec4 COL_TEXT_FAINT  = ImVec4(0.420f, 0.420f, 0.460f, 1.00f);
static const ImVec4 COL_SUCCESS     = ImVec4(0.180f, 0.800f, 0.443f, 1.00f);
static const ImVec4 COL_DANGER      = ImVec4(0.937f, 0.267f, 0.267f, 1.00f);
static const ImVec4 COL_WARNING     = ImVec4(0.980f, 0.612f, 0.118f, 1.00f);

// Fonts
static ImFont* g_fontBody    = nullptr;
static ImFont* g_fontSmall   = nullptr;
static ImFont* g_fontTitle   = nullptr;
static ImFont* g_fontHero    = nullptr;
static ImFont* g_fontIcons   = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline ImVec4 MulAlpha(ImVec4 c, float a) { c.w *= a; return c; }

static inline ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// Maps t in [0,1] to a point along a rounded rectangle perimeter (clockwise from top-left after the top-left arc).
// a → b is the bounding box, r is the corner radius.
static ImVec2 RoundedPerimeterPoint(ImVec2 a, ImVec2 b, float r, float t) {
    float w = b.x - a.x;
    float h = b.y - a.y;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;

    float straightTop    = w - 2.0f * r;
    float straightRight  = h - 2.0f * r;
    float straightBottom = w - 2.0f * r;
    float straightLeft   = h - 2.0f * r;
    float arc            = 0.5f * 3.14159265358979f * r;
    float perim          = straightTop + arc + straightRight + arc + straightBottom + arc + straightLeft + arc;

    float d = t * perim;

    auto onArc = [&](float cx, float cy, float startAngle, float dist) {
        float a0 = startAngle + (dist / r);
        return ImVec2(cx + cosf(a0) * r, cy + sinf(a0) * r);
    };

    // Top straight (left-to-right starting after top-left arc)
    if (d <= straightTop) return ImVec2(a.x + r + d, a.y);
    d -= straightTop;
    // Top-right arc (-pi/2 → 0)
    if (d <= arc) return onArc(b.x - r, a.y + r, -3.14159265358979f * 0.5f, d);
    d -= arc;
    // Right straight (top-to-bottom)
    if (d <= straightRight) return ImVec2(b.x, a.y + r + d);
    d -= straightRight;
    // Bottom-right arc (0 → pi/2)
    if (d <= arc) return onArc(b.x - r, b.y - r, 0.0f, d);
    d -= arc;
    // Bottom straight (right-to-left)
    if (d <= straightBottom) return ImVec2(b.x - r - d, b.y);
    d -= straightBottom;
    // Bottom-left arc (pi/2 → pi)
    if (d <= arc) return onArc(a.x + r, b.y - r, 3.14159265358979f * 0.5f, d);
    d -= arc;
    // Left straight (bottom-to-top)
    if (d <= straightLeft) return ImVec2(a.x, b.y - r - d);
    d -= straightLeft;
    // Top-left arc (pi → 3pi/2)
    return onArc(a.x + r, a.y + r, 3.14159265358979f, d);
}

// Continuous animated rainbow stroke around the (rounded) window border.
static void DrawRainbowBorder(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax, float thickness, float phase) {
    const int segments = 320;
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / segments;
        float t1 = (float)(i + 1) / segments;
        ImVec2 p0 = RoundedPerimeterPoint(pmin, pmax, WND_CORNER_RADIUS, t0);
        ImVec2 p1 = RoundedPerimeterPoint(pmin, pmax, WND_CORNER_RADIUS, t1);
        float hue = fmodf(t0 + phase, 1.0f);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.00f, r, g, b);
        ImU32 col = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
        dl->AddLine(p0, p1, col, thickness);
    }
}

// A bigger, glow-aware accent button. Returns true on click.
static bool AccentButton(const char* label, ImVec2 size, bool disabled = false) {
    ImGuiID id = ImGui::GetID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushStyleColor(ImGuiCol_Button,        disabled ? ImVec4(0.18f, 0.18f, 0.22f, 1.00f) : COL_PURPLE);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, disabled ? ImVec4(0.18f, 0.18f, 0.22f, 1.00f) : COL_PURPLE_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  disabled ? ImVec4(0.18f, 0.18f, 0.22f, 1.00f) : COL_PURPLE_ACT);
    ImGui::PushStyleColor(ImGuiCol_Text,          disabled ? COL_TEXT_FAINT : COL_TEXT);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

    if (disabled) ImGui::BeginDisabled();
    bool clicked = ImGui::Button(label, size);
    if (disabled) ImGui::EndDisabled();

    // Glow under hover
    if (!disabled && ImGui::IsItemHovered()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = pos;
        ImVec2 b = ImVec2(pos.x + size.x, pos.y + size.y);
        float t = (sinf((float)ImGui::GetTime() * 4.0f) + 1.0f) * 0.5f;
        ImU32 glow = ImGui::GetColorU32(ImVec4(COL_PURPLE.x, COL_PURPLE.y, COL_PURPLE.z, 0.20f + 0.20f * t));
        for (int i = 1; i <= 6; ++i) {
            dl->AddRect(ImVec2(a.x - i, a.y - i), ImVec2(b.x + i, b.y + i), glow, 10.0f, 0, 1.0f);
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

// Subtle ghost button (used for Logout, secondary actions).
static bool GhostButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Text,          COL_TEXT_DIM);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

// Status pill, drawn inline using DrawList for a custom rounded badge.
static void StatusPill(const char* text, ImVec4 color) {
    ImVec2 pad(10, 4);
    ImVec2 txtSize = ImGui::CalcTextSize(text);
    ImVec2 size(txtSize.x + pad.x * 2, txtSize.y + pad.y * 2);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg   = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.18f));
    ImU32 brd  = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.55f));
    ImU32 txtc = ImGui::GetColorU32(color);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg,  9.0f);
    dl->AddRect      (pos, ImVec2(pos.x + size.x, pos.y + size.y), brd, 9.0f, 0, 1.0f);
    dl->AddText      (ImVec2(pos.x + pad.x, pos.y + pad.y), txtc, text);
    ImGui::Dummy(size);
}

// Animated arc spinner.
static void DrawSpinner(float radius, float thickness, ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 center(p.x + radius, p.y + radius);
    float t = (float)ImGui::GetTime();
    int   num_segments = 36;
    float start = t * 4.0f;
    float arc   = 1.5f * IM_PI;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PathClear();
    for (int i = 0; i <= num_segments; i++) {
        float a = start + (i / (float)num_segments) * arc;
        dl->PathLineTo(ImVec2(center.x + cosf(a) * radius,
                              center.y + sinf(a) * radius));
    }
    dl->PathStroke(color, false, thickness);
    ImGui::Dummy(ImVec2(radius * 2 + 2, radius * 2 + 2));
}

static void CenterText(const char* text, ImFont* font = nullptr, ImVec4 color = COL_TEXT) {
    if (font) ImGui::PushFont(font);
    float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - w) * 0.5f);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

// Forward declarations
static std::string ProductImagePath(const std::string& productId);

// Reset transient state when navigating between screens.
static void GotoState(AppState s) {
    if (g_state != s) {
        g_lastState = g_state;
        g_state = s;
        g_screenFade = 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────
static void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = WND_CORNER_RADIUS_BASE; // Will be scaled later, but actually let's just use WND_CORNER_RADIUS later, wait, this is before scale. So WND_CORNER_RADIUS_BASE.
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 8.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 8.0f;
    style.TabRounding       = 8.0f;

    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(12, 9);
    style.ItemSpacing       = ImVec2(10, 10);
    style.ItemInnerSpacing  = ImVec2(8, 6);
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 8.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]            = COL_BG;
    c[ImGuiCol_ChildBg]             = COL_PANEL;
    c[ImGuiCol_PopupBg]             = COL_PANEL_ALT;
    c[ImGuiCol_Border]              = COL_BORDER;
    c[ImGuiCol_BorderShadow]        = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]             = ImVec4(0.130f, 0.130f, 0.165f, 1.00f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.180f, 0.150f, 0.230f, 1.00f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.230f, 0.170f, 0.300f, 1.00f);
    c[ImGuiCol_TitleBg]             = COL_PANEL;
    c[ImGuiCol_TitleBgActive]       = COL_PANEL;
    c[ImGuiCol_TitleBgCollapsed]    = COL_PANEL;
    c[ImGuiCol_MenuBarBg]           = COL_PANEL;
    c[ImGuiCol_ScrollbarBg]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]       = ImVec4(0.30f, 0.20f, 0.40f, 0.60f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.40f, 0.25f, 0.50f, 0.80f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.30f, 0.60f, 1.00f);
    c[ImGuiCol_CheckMark]           = COL_PURPLE;
    c[ImGuiCol_SliderGrab]          = COL_PURPLE;
    c[ImGuiCol_SliderGrabActive]    = COL_PURPLE_HOV;
    c[ImGuiCol_Button]              = COL_PURPLE;
    c[ImGuiCol_ButtonHovered]       = COL_PURPLE_HOV;
    c[ImGuiCol_ButtonActive]        = COL_PURPLE_ACT;
    c[ImGuiCol_Header]              = ImVec4(0.180f, 0.120f, 0.260f, 0.80f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(0.250f, 0.160f, 0.340f, 0.90f);
    c[ImGuiCol_HeaderActive]        = ImVec4(0.320f, 0.200f, 0.420f, 1.00f);
    c[ImGuiCol_Separator]           = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_SeparatorHovered]    = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_SeparatorActive]     = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_Text]                = COL_TEXT;
    c[ImGuiCol_TextDisabled]        = COL_TEXT_FAINT;
    c[ImGuiCol_NavHighlight]        = COL_PURPLE_HOV;
}

// ─────────────────────────────────────────────────────────────────────────────
// Title bar (custom; native drag handled via WM_NCHITTEST)
// ─────────────────────────────────────────────────────────────────────────────
static void DrawTitleBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 winPos  = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();

    ImVec2 a = winPos;
    ImVec2 b = ImVec2(winPos.x + winSize.x, winPos.y + TITLE_BAR_H);
    // Rounded top corners only (bottom is flush with the body).
    dl->AddRectFilled(a, b, ImGui::GetColorU32(COL_PANEL),
                      WND_CORNER_RADIUS, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.05f)), 1.0f);

    // Brand mark — purple rounded square with "S"
    const float logoSize = 26.0f;
    ImVec2 logo    = ImVec2(winPos.x + 14, winPos.y + (TITLE_BAR_H - logoSize) * 0.5f);
    ImVec2 logoMax = ImVec2(logo.x + logoSize, logo.y + logoSize);
    dl->AddRectFilled(logo, logoMax, ImGui::GetColorU32(COL_PURPLE), 7.0f);
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImVec2 sSize = ImGui::CalcTextSize("S");
    dl->AddText(ImVec2(logo.x + (logoSize - sSize.x) * 0.5f,
                       logo.y + (logoSize - sSize.y) * 0.5f - 1),
                ImGui::GetColorU32(COL_TEXT), "S");
    if (g_fontTitle) ImGui::PopFont();

    // Brand text
    if (g_fontBody) ImGui::PushFont(g_fontBody);
    ImVec2 brandSize = ImGui::CalcTextSize("Scootware");
    dl->AddText(ImVec2(logo.x + logoSize + 10,
                       winPos.y + (TITLE_BAR_H - brandSize.y) * 0.5f),
                ImGui::GetColorU32(COL_TEXT), "Scootware");
    if (g_fontBody) ImGui::PopFont();

    // Window control buttons (right side) — close only
    const float btnW = 46.0f;
    const float btnH = TITLE_BAR_H;

    ImGui::SetCursorPos(ImVec2(winSize.x - btnW, 0));
    bool closeClicked = ImGui::InvisibleButton("##close", ImVec2(btnW, btnH));
    bool closeHovered = ImGui::IsItemHovered();
    bool closeActive  = ImGui::IsItemActive();
    
    if (closeClicked) {
        if (g_hwnd) ::PostMessage(g_hwnd, WM_CLOSE, 0, 0);
    }
    
    if (closeHovered || closeActive) {
        ImU32 col = ImGui::GetColorU32(closeActive ? ImVec4(0.737f, 0.180f, 0.180f, 1.00f) : ImVec4(0.937f, 0.267f, 0.267f, 0.85f));
        dl->AddRectFilled(ImVec2(winPos.x + winSize.x - btnW, winPos.y),
                          ImVec2(winPos.x + winSize.x, winPos.y + btnH),
                          col, WND_CORNER_RADIUS, ImDrawFlags_RoundCornersTopRight);
    }
    {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        ImVec2 cx = ImVec2(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f);
        float r = 6.0f;
        ImU32 col = ImGui::GetColorU32(COL_TEXT);
        dl->AddLine(ImVec2(cx.x - r, cx.y - r), ImVec2(cx.x + r, cx.y + r), col, 1.6f);
        dl->AddLine(ImVec2(cx.x + r, cx.y - r), ImVec2(cx.x - r, cx.y + r), col, 1.6f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Login screen
// ─────────────────────────────────────────────────────────────────────────────
static void DrawLoginScreen() {
    float avail = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(0, 2));

    // Hero / heading
    ImGui::Dummy(ImVec2(0, 4)); // Adjust spacing
    
    // Draw the "S" logo
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float logoSize = 64.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 logo = ImVec2(pos.x + (avail - logoSize) * 0.5f, pos.y);
    ImVec2 logoMax = ImVec2(logo.x + logoSize, logo.y + logoSize);
    dl->AddRectFilled(logo, logoMax, ImGui::GetColorU32(COL_PURPLE), 16.0f);
    if (g_fontHero) ImGui::PushFont(g_fontHero);
    ImVec2 sSize = ImGui::CalcTextSize("S");
    dl->AddText(ImVec2(logo.x + (logoSize - sSize.x) * 0.5f,
                       logo.y + (logoSize - sSize.y) * 0.5f - 4),
                ImGui::GetColorU32(COL_TEXT), "S");
    if (g_fontHero) ImGui::PopFont();
    
    ImGui::Dummy(ImVec2(0, logoSize + 8));

    if (g_fontHero) {
        ImGui::PushFont(g_fontHero);
        const char* h = "Login";
        float w = ImGui::CalcTextSize(h).x;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - w) * 0.5f);
        ImGui::TextColored(COL_TEXT, "%s", h);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, 10));

    // Form labels + fields
    if (g_fontSmall) ImGui::PushFont(g_fontSmall);
    ImGui::TextColored(COL_TEXT_DIM, "USERNAME");
    if (g_fontSmall) ImGui::PopFont();
    ImGui::SetNextItemWidth(avail);
    ImGui::InputTextWithHint("##user", "your username", g_username, IM_ARRAYSIZE(g_username));

    ImGui::Dummy(ImVec2(0, 2));

    if (g_fontSmall) ImGui::PushFont(g_fontSmall);
    ImGui::TextColored(COL_TEXT_DIM, "PASSWORD");
    if (g_fontSmall) ImGui::PopFont();
    ImGui::SetNextItemWidth(avail);
    bool enterPressed = ImGui::InputTextWithHint(
        "##pass", "••••••••", g_password, IM_ARRAYSIZE(g_password),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Dummy(ImVec2(0, 2));

    ImGui::Checkbox("Remember me", &g_rememberMe);

    ImGui::Dummy(ImVec2(0, 6));

    // Login button (spans full width)
    bool loginClicked = AccentButton("Sign In", ImVec2(avail, 44));
    if (loginClicked || enterPressed) {
        std::string userStr = g_username;
        std::string passStr = g_password;
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
        };
        trim(userStr);
        trim(passStr);

        // Blocks UI temporarily
        g_authInfo = Api::Login(userStr, passStr);

        if (g_authInfo.success) {
            if (g_rememberMe) {
                Session::SaveCredentials(userStr, passStr);
            }

            std::vector<Api::ProductAccess> activeSubs;
            for (const auto& p : g_authInfo.subscriptions) {
                if (p.hasAccess) {
                    activeSubs.push_back(p);
                }
            }
            g_authInfo.subscriptions = activeSubs;

            GotoState(AppState::Products);
            g_statusMessage = "";
            if (!g_authInfo.avatarUrl.empty()) {
                ImageLoader::Request("avatar", g_authInfo.avatarUrl);
            }
            for (const auto& p : g_authInfo.subscriptions) {
                ImageLoader::Request("prod:" + p.productId, ProductImagePath(p.productId));
            }
        } else {
            ShowPopup("Authentication Failed", g_authInfo.message, COL_DANGER);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Products screen
// ─────────────────────────────────────────────────────────────────────────────
static std::string LowerCase(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string FormatDateString(const std::string& isoDate) {
    if (isoDate.empty()) return "Never";
    int year, month, day;
    if (sscanf_s(isoDate.c_str(), "%d-%d-%dT", &year, &month, &day) == 3) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d", month, day, year);
        return std::string(buf);
    }
    return isoDate;
}

// Map productId → static asset path on scootware.us (matches the website).
static std::string ProductImagePath(const std::string& productId) {
    std::string id = LowerCase(productId);
    if (id == "tarkov") return "/images/products/tarkov.png";
    return "/images/products/" + id + ".jpg";
}

static void DrawAvatar(ImVec2 origin, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    float  r = size * 0.5f;

    ID3D11ShaderResourceView* srv = ImageLoader::Get("avatar");
    if (srv) {
        ImVec2 a = origin;
        ImVec2 b = ImVec2(origin.x + size, origin.y + size);
        dl->AddImageRounded((ImTextureID)(intptr_t)srv, a, b,
                            ImVec2(0, 0), ImVec2(1, 1),
                            ImGui::GetColorU32(ImVec4(1, 1, 1, 1)),
                            r, ImDrawFlags_RoundCornersAll);
    } else {
        // Placeholder: filled circle with the user's initial
        ImU32 bg = ImGui::GetColorU32(COL_PURPLE);
        dl->AddCircleFilled(center, r, bg, 48);
        char letter[2] = {
            (char)(g_username[0] ? std::toupper((unsigned char)g_username[0]) : '?'),
            0
        };
        if (g_fontTitle) ImGui::PushFont(g_fontTitle);
        ImVec2 sz = ImGui::CalcTextSize(letter);
        dl->AddText(ImVec2(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f),
                    ImGui::GetColorU32(COL_TEXT), letter);
        if (g_fontTitle) ImGui::PopFont();
    }
    // Subtle border ring
    dl->AddCircle(center, r, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.14f)), 48, 1.5f);
}

// Launches a product on a background thread (extracted from the inline lambda).
//
// Reporting contract: exactly ONE loader event is emitted per injection attempt,
// sent at the end of the cycle with the final outcome. This keeps the admin
// panel clean (one row per launch click) instead of the multi-row cascade that
// per-stage logging produces.
static void LaunchProduct(const std::string& productId) {
    if (g_injecting.exchange(true)) return;

    GotoState(AppState::Injecting);
    g_hwidMismatch       = false;
    g_hwidResetSubmitted = false;
    g_hwidResetMessage   = "";
    g_injectionFailed    = false;
    g_statusMessage      = "Streaming payload for " + productId + "...";

    std::string token = g_authInfo.token;
    std::string hwid  = g_hwid;

    std::thread([productId, token, hwid]() {
        // Outcome accumulator — updated as we progress, reported exactly once
        // at the very end of the injection cycle.
        std::string finalEventType = OBF_S("load_failed");
        std::string finalDetails   = OBF_S("unknown failure");

        // ── Cold-start gate ───────────────────────────────────────────────
        // If this product needs a host process (e.g. cs2.exe) and it isn't
        // running yet, stall here with a "Waiting for game process..."
        // status message instead of streaming the payload immediately.
        // Reasons we want to stall, not bail:
        //   * Streaming consumes a session-token use against the backend
        //     (rate limited and audit-logged) — wasting one on a launch
        //     that has nowhere to go is sloppy.
        //   * The injected runtime would just sit in its own 60s wait loop
        //     anyway (memory::initialize polls 120 × 500ms for cs2.exe),
        //     and we'd lose the ability to show the user *why* nothing is
        //     happening.
        //   * The handoff env vars (SW_S/SW_P/SW_E) are set right before
        //     RunPE and wiped right after; keeping that window tight is
        //     important.
        const std::wstring hostExe = ProcessWait::HostProcessFor(productId);
        if (!hostExe.empty() && !ProcessWait::IsRunning(hostExe)) {
            g_statusMessage = "Waiting for game process (" + productId + ")...";

            const bool ok = ProcessWait::WaitForProcess(
                hostExe,
                /*cancel*/ []() { return false; }, // window close kills the process
                /*tick  */ [productId](int seconds) {
                    // Cosmetic countdown so the user sees the loader is
                    // still alive and not hung.
                    g_statusMessage =
                        "Waiting for game process (" + productId + ")... "
                        + std::to_string(seconds) + "s";
                });

            if (!ok) {
                // Only reachable if WaitForProcess gets a cancel signal
                // — currently never, but kept symmetric for future use.
                g_statusMessage = "Cancelled — game process never started.";
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("cold-start cancelled before host appeared"), token);
                g_injecting = false;
                return;
            }

            g_statusMessage = "Game process detected — streaming payload...";
        }

        auto [hollowBuffer, hollowAllocSize] = Api::StreamAsset(productId, "hollow_exe", token, hwid);
        std::string customHostPath = "";

        if (!hollowBuffer.empty()) {
            char tempPath[MAX_PATH];
            if (GetTempPathA(MAX_PATH, tempPath)) {
                char tempFile[MAX_PATH];
                if (GetTempFileNameA(tempPath, "sct", 0, tempFile)) {
                    customHostPath = std::string(tempFile) + ".exe";
                    DeleteFileA(tempFile);
                    FILE* f = nullptr;
                    fopen_s(&f, customHostPath.c_str(), "wb");
                    if (f) {
                        fwrite(hollowBuffer.data(), 1, hollowBuffer.size(), f);
                        fclose(f);
                    } else {
                        customHostPath = "";
                    }
                }
            }
        }

        auto [exeBuffer, allocSize] = Api::StreamAsset(productId, "primary_exe", token, hwid);
        if (!exeBuffer.empty()) {
            // Stash session secrets in our env block for the host to inherit
            // via CreateProcess. Wiped immediately after so the parent's env
            // dump is clean even if a debugger snapshots us right after.
            std::string expiresAt;
            for (const auto& p : g_authInfo.subscriptions) {
                if (p.productId == productId) { expiresAt = p.expiresAt; break; }
            }

            // Hard-fail BEFORE we burn a stream + spawn the host if the
            // session token never made it back from /api/auth/login. Without
            // it Handoff::Set bails early, the env channel never publishes,
            // and the external boots into "no loader handoff" — which the
            // crack-attempt reporter then dutifully fires off, polluting the
            // admin panel with false positives every time the auth response
            // shape drifts. Surface it loudly here instead.
            if (token.empty()) {
                LDIAG_LINE("[inject] aborting — empty session token from Api::Login "
                           "(server probably returned no `sessionId`/`token` field). "
                           "Handoff would silently produce a no-handoff trip on the external.");
                g_statusMessage   = "Login session token missing — please re-login.";
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("aborted before spawn: empty session token from /api/auth/login"),
                    /*token*/ "");
                if (!customHostPath.empty()) DeleteFileA(customHostPath.c_str());
                g_injecting = false;
                return;
            }

            bool handoffOk = Handoff::Set(token, hwid, productId, expiresAt);
            if (!handoffOk) {
                LDIAG_LINE("[inject] aborting — Handoff::Set failed (no SW_S/SW_P "
                           "in env or shmem). Spawning would trip no-handoff on the external.");
                g_statusMessage   = "Internal error: handoff publish failed.";
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("aborted before spawn: Handoff::Set returned false"),
                    token);
                if (!customHostPath.empty()) DeleteFileA(customHostPath.c_str());
                g_injecting = false;
                return;
            }

            // ── Kernel driver bring-up ─────────────────────────────────────
            // Done HERE (loader, elevated, before the cheat is hollowed)
            // instead of in the cheat itself. The user already gave UAC
            // consent to launch the loader; the cheat shouldn't have to
            // ask for it a second time mid-render.
            //
            // The mapper EXE is hollowed into its OWN scootware.exe
            // instance (using the same hollow_exe binary we just streamed
            // for the cheat host, hence we hand the buffer in here so
            // Api::StreamAsset isn't burned a second time on the same
            // payload). Both processes share the same image name in the
            // task list, but the mapper exits as soon as the kernel side
            // is up and only the cheat persists.
            //
            // If this fails we abort the whole launch — there's no point
            // hollowing the cheat if the driver isn't going to be there
            // for it to talk to.
            g_statusMessage = "Bringing up kernel driver...";
            DriverBringup::Outcome drv = DriverBringup::Run(
                productId, token, hwid, hollowBuffer, hollowAllocSize);
            if (drv.result != DriverBringup::Result::Success) {
                LDIAG() << "[inject] aborting — DriverBringup::Run failed: "
                        << drv.details;
                g_statusMessage   = "Driver bring-up failed: " + drv.details;
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("aborted before spawn: driver bring-up failed — ")
                        + drv.details,
                    token);
                Handoff::Wipe();
                if (!customHostPath.empty()) DeleteFileA(customHostPath.c_str());
                g_injecting = false;
                return;
            }
            g_statusMessage = "Driver online — injecting cheat...";

            // Build the env block AFTER Handoff::Set so SW_S/SW_P/SW_E
            // are baked in. We pass it explicitly to RunPE so the child
            // gets the secrets via lpEnvironment instead of relying on
            // CreateProcess implicit inheritance — process hollowing's
            // CreateRemoteThread + LdrInitializeThunk dance can race
            // with implicit env inheritance and silently strip them.
            std::vector<char> childEnv = Handoff::BuildChildEnvironment();

            bool injected = RunPE::Execute(exeBuffer, allocSize,
                                            customHostPath, childEnv);

            // We deliberately leave the Local\SW_HANDOFF_<pid> shmem
            // mapping open across the auto-close window (~1.5s after
            // success) so a slow child still finds it. Wipe() runs in
            // the auto-close path immediately before exit; until then
            // the section is held open by this process.
            Handoff::Wipe();

            if (injected) {
                g_statusMessage      = "Injected successfully — closing loader...";
                g_injectionSucceeded = true;
                g_autoCloseAt        = (uint64_t)GetTickCount64() + 1500;
                finalEventType = OBF_S("load_success");
                finalDetails   = OBF_S("primary_exe injected via RunPE");
            } else {
                g_statusMessage   = "Injection failed (RunPE error).";
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("RunPE::Execute returned false");
            }
        } else {
            g_hwidMismatch = Api::LastStreamWasHwidMismatch();
            if (g_hwidMismatch) {
                g_statusMessage = "HWID mismatch — this account is bound to a different machine.";
                // HWID mismatch is ALSO logged server-side from the stream
                // endpoint (tamper-resistant). We skip client reporting here
                // to avoid duplicate rows in the admin panel.
                finalEventType = "";
            } else {
                g_statusMessage   = "Failed to stream asset from secure server.";
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("stream request failed (primary_exe unavailable)");
            }
        }

        // Single consolidated report for the whole injection cycle.
        if (!finalEventType.empty()) {
            Api::ReportLoaderEvent(finalEventType, productId, hwid, finalDetails, token);
        }

        g_injecting = false;
    }).detach();
}

// Draws a single product card at the given rect. All interactive items are
// placed at known positions inside the rect; the caller claims layout space
// via Dummy() afterwards so ImGui doesn't fire its boundary assertion.
static void DrawProductCard(int idx,
                            const Api::ProductAccess& prod,
                            ImVec2 cardMin, ImVec2 cardMax,
                            float cardRadius, float pad,
                            bool injectionLocked)
{
    ImGui::PushID(idx);
    const float cardW = cardMax.x - cardMin.x;
    const float cardH = cardMax.y - cardMin.y;
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    const bool disabled = !prod.hasAccess || injectionLocked;

    // Make the entire card an interactive button
    ImGui::SetCursorScreenPos(cardMin);
    bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
    bool hovered = ImGui::IsItemHovered() && !disabled;
    bool active  = ImGui::IsItemActive() && !disabled;

    if (clicked && !disabled) {
        LaunchProduct(prod.productId);
    }

    // ── Background ──
    const std::string key = "prod:" + prod.productId;
    int pw = 0, ph = 0;
    ID3D11ShaderResourceView* bg = ImageLoader::Get(key, &pw, &ph);

    // Apply color tint based on hover state
    ImVec4 baseColor = ImVec4(1, 1, 1, 1);
    if (hovered) baseColor = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
    if (active) baseColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    cdl->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(COL_PANEL), cardRadius);

    if (bg && pw > 0 && ph > 0) {
        float cardAR = cardW / cardH;
        float imgAR  = (float)pw / (float)ph;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (imgAR > cardAR) {
            float vis = cardAR / imgAR;
            uv0.x = (1.0f - vis) * 0.5f;
            uv1.x = 1.0f - uv0.x;
        } else {
            float vis = imgAR / cardAR;
            uv0.y = (1.0f - vis) * 0.5f;
            uv1.y = 1.0f - uv0.y;
        }
        cdl->AddImageRounded((ImTextureID)(intptr_t)bg, cardMin, cardMax, uv0, uv1,
                             ImGui::GetColorU32(baseColor),
                             cardRadius, ImDrawFlags_RoundCornersAll);
    }

    // Dark scrim + border
    cdl->AddRectFilled(cardMin, cardMax,
                       ImGui::GetColorU32(ImVec4(0, 0, 0, hovered ? 0.35f : 0.58f)),
                       cardRadius);
    
    // Accent Border on hover
    if (hovered) {
        cdl->AddRect(cardMin, cardMax,
                     ImGui::GetColorU32(COL_PURPLE_HOV),
                     cardRadius, 0, SX(2.0f));
        
        // Glow effect
        float t = (sinf((float)ImGui::GetTime() * 4.0f) + 1.0f) * 0.5f;
        ImU32 glow = ImGui::GetColorU32(ImVec4(COL_PURPLE.x, COL_PURPLE.y, COL_PURPLE.z, 0.20f + 0.20f * t));
        for (int i = 1; i <= 6; ++i) {
            cdl->AddRect(ImVec2(cardMin.x - i, cardMin.y - i), ImVec2(cardMax.x + i, cardMax.y + i), glow, cardRadius + i, 0, 1.0f);
        }
    } else {
        cdl->AddRect(cardMin, cardMax,
                     ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)),
                     cardRadius, 0, SX(1.5f));
    }

    // ── Top row: product name ──
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMin.y + pad));
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImGui::TextColored(COL_TEXT, "%s", prod.productId.c_str());
    if (g_fontTitle) ImGui::PopFont();

    // ── Sub line: expiration ──
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMin.y + pad + SX(30)));
    // Use g_fontBody instead of g_fontSmall to make it a bit bigger
    if (g_fontBody) ImGui::PushFont(g_fontBody);
    if (prod.hasAccess) {
        ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 0.92f),
                           "Expires %s",
                           FormatDateString(prod.expiresAt).c_str());
    } else {
        ImGui::TextColored(COL_TEXT_DIM, "%s",
                           prod.expiresAt.empty() ? "No active subscription"
                                                  : FormatDateString(prod.expiresAt).c_str());
    }
    if (g_fontBody) ImGui::PopFont();

    // Launching text if injection locked
    if (injectionLocked && hovered) {
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMax.y - pad - SX(24)));
        if (g_fontBody) ImGui::PushFont(g_fontBody);
        ImGui::TextColored(COL_TEXT_DIM, "Launching...");
        if (g_fontBody) ImGui::PopFont();
    }

    ImGui::PopID();
}

static void DrawProductScreen() {
    const float availTotal = ImGui::GetContentRegionAvail().x;
    ImVec2  origin   = ImGui::GetCursorScreenPos();
    ImDrawList* dl   = ImGui::GetWindowDrawList();

    // ── Header row: avatar + username on the left, RED Logout in the top-right ──
    const float avatarSize = SX(48.0f);
    const float headerH    = avatarSize;
    DrawAvatar(origin, avatarSize);

    const float nameX = origin.x + avatarSize + SX(14.0f);
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImVec2 nameSz = ImGui::CalcTextSize(g_username[0] ? g_username : "user");
    float  nameY  = origin.y + (avatarSize - nameSz.y) * 0.5f;
    dl->AddText(ImVec2(nameX, nameY), ImGui::GetColorU32(COL_TEXT),
                g_username[0] ? g_username : "user");
    if (g_fontTitle) ImGui::PopFont();

    // Logout (red) — vertically aligned with the name
    const float logoutW = SX(96.0f);
    const float logoutH = SX(34.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + availTotal - logoutW,
                                     origin.y + (avatarSize - logoutH) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_DANGER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.40f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.74f, 0.18f, 0.18f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, SX(8.0f));
    if (ImGui::Button("Logout", ImVec2(logoutW, logoutH))) {
        Session::ClearCredentials();
        GotoState(AppState::Login);
        ShowPopup("Logged Out", "You have been successfully logged out.", COL_SUCCESS);
        g_password[0]   = '\0';
        g_rememberMe    = false;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // Claim the header strip in the layout, then advance below it.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(availTotal, headerH));
    ImGui::Dummy(ImVec2(0, SX(10.0f)));

    if (g_authInfo.subscriptions.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::BeginChild("##empty", ImVec2(availTotal, SX(140)),
                          ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        ImGui::Dummy(ImVec2(0, SX(38)));
        CenterText("No active subscriptions", g_fontTitle, COL_TEXT);
        ImGui::Dummy(ImVec2(0, SX(6)));
        CenterText("Visit scootware.cc to purchase access.", g_fontSmall, COL_TEXT_DIM);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    bool injectionLocked = g_injecting.load() || g_injectionSucceeded.load();
    const size_t total = g_authInfo.subscriptions.size();

    // ── Adaptive grid ──
    // Pick a column count that keeps each card readable but lets us fit
    // everything without scrolling on the default window size.
    const float CARD_RADIUS  = SX(12.0f);
    const float CARD_PAD     = SX(14.0f);
    const float CARD_GAP     = SX(10.0f);
    const float MIN_CARD_W   = SX(220.0f);

    int cols = (int)((availTotal + CARD_GAP) / (MIN_CARD_W + CARD_GAP));
    if (cols < 1) cols = 1;
    if (cols > (int)total) cols = (int)total;
    if (cols > 3) cols = 3;

    const float cardW   = (availTotal - (cols - 1) * CARD_GAP) / (float)cols;
    const int   rows    = (int)((total + cols - 1) / cols);

    // Card height: the more cards-per-row, the squarer; single column gets a wider strip.
    float cardH = (cols == 1) ? SX(110.0f) : SX(140.0f);

    // If the grid won't fit in the remaining space, shrink the card height a touch.
    float availH = ImGui::GetContentRegionAvail().y;
    float gridH  = rows * cardH + (rows - 1) * CARD_GAP;
    if (gridH > availH && rows > 0) {
        cardH = (availH - (rows - 1) * CARD_GAP) / (float)rows;
        if (cardH < SX(96.0f)) cardH = SX(96.0f);
        gridH = rows * cardH + (rows - 1) * CARD_GAP;
    }

    ImVec2 gridStart = ImGui::GetCursorScreenPos();

    // Reserve the whole grid up-front so SetCursorScreenPos overlays don't
    // try to extend the parent's content rect (which fires the assertion).
    ImGui::Dummy(ImVec2(availTotal, gridH));

    for (size_t i = 0; i < total; ++i) {
        const auto& prod = g_authInfo.subscriptions[i];
        int col = (int)(i % cols);
        int row = (int)(i / cols);

        ImVec2 cardMin(gridStart.x + col * (cardW + CARD_GAP),
                       gridStart.y + row * (cardH + CARD_GAP));
        ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

        DrawProductCard((int)i, prod, cardMin, cardMax,
                        CARD_RADIUS, CARD_PAD, injectionLocked);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Injecting screen
// ─────────────────────────────────────────────────────────────────────────────
static void DrawInjectingScreen() {
    float avail = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(0, 24));

    bool injectionLocked = g_injecting.load() || g_injectionSucceeded.load();
    bool succeeded       = g_injectionSucceeded.load();

    // Spinner / status icon
    float spinnerSize = 22.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - spinnerSize * 2) * 0.5f);
    if (succeeded) {
        // Static checkmark
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 c = ImVec2(p.x + spinnerSize, p.y + spinnerSize);
        ImU32 col = ImGui::GetColorU32(COL_SUCCESS);
        dl->AddCircle(c, spinnerSize, col, 0, 2.0f);
        dl->AddLine(ImVec2(c.x - 8, c.y),     ImVec2(c.x - 2, c.y + 6), col, 2.5f);
        dl->AddLine(ImVec2(c.x - 2, c.y + 6), ImVec2(c.x + 9, c.y - 6), col, 2.5f);
        ImGui::Dummy(ImVec2(spinnerSize * 2 + 2, spinnerSize * 2 + 2));
    } else if (g_hwidMismatch && !injectionLocked) {
        // Warning icon
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 c = ImVec2(p.x + spinnerSize, p.y + spinnerSize);
        ImU32 col = ImGui::GetColorU32(COL_WARNING);
        dl->AddCircle(c, spinnerSize, col, 0, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 3), col, 2.5f);
        dl->AddCircleFilled(ImVec2(c.x, c.y + 8), 1.6f, col);
        ImGui::Dummy(ImVec2(spinnerSize * 2 + 2, spinnerSize * 2 + 2));
    } else {
        DrawSpinner(spinnerSize, 3.0f, ImGui::GetColorU32(COL_PURPLE));
    }

    ImGui::Dummy(ImVec2(0, 14));

    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    const char* title = succeeded               ? "Launch successful"
                       : g_hwidMismatch         ? "Hardware mismatch"
                       : "Securing payload";
    {
        float w = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - w) * 0.5f);
        ImGui::TextColored(COL_TEXT, "%s", title);
    }
    if (g_fontTitle) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 6));

    // Status message panel
    //
    // The message can be anything from a short "Streaming payload..." to a
    // verbose error with an exit code baked in. We wrap it so long errors
    // stay inside the panel, and make the whole panel click-to-copy so
    // users can paste the full string when asking for support.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
    ImGui::BeginChild("##status", ImVec2(avail, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    {
        ImVec2 panelMin = ImGui::GetCursorScreenPos();
        float  wrapW    = ImGui::GetContentRegionAvail().x;

        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
        ImGui::TextUnformatted(g_statusMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        // Invisible hit-region over the text we just drew so the whole
        // block is clickable (and shows a tooltip) without having to use
        // a selectable, which would repaint the background.
        ImVec2 panelMax = ImVec2(panelMin.x + wrapW,
                                 ImGui::GetCursorScreenPos().y);
        ImGui::SetCursorScreenPos(panelMin);
        ImGui::InvisibleButton("##status_copy",
                               ImVec2(wrapW, panelMax.y - panelMin.y));
        if (ImGui::IsItemHovered() && !g_statusMessage.empty()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", g_statusCopied
                                        ? "Copied to clipboard"
                                        : "Click to copy");
        }
        if (ImGui::IsItemClicked() && !g_statusMessage.empty()) {
            ImGui::SetClipboardText(g_statusMessage.c_str());
            g_statusCopied     = true;
            g_statusCopiedTime = ImGui::GetTime();
        }
        // Auto-reset the "Copied" tooltip after a short while so the next
        // hover shows "Click to copy" again.
        if (g_statusCopied && ImGui::GetTime() - g_statusCopiedTime > 1.5) {
            g_statusCopied = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // HWID mismatch flow
    if (g_hwidMismatch) {
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(COL_WARNING.x, COL_WARNING.y, COL_WARNING.z, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(COL_WARNING.x, COL_WARNING.y, COL_WARNING.z, 0.45f));
        ImGui::BeginChild("##hwidpanel", ImVec2(avail, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

        if (g_fontBody) ImGui::PushFont(g_fontBody);
        ImGui::TextColored(COL_WARNING, "Bound to a different machine");
        if (g_fontBody) ImGui::PopFont();

        ImGui::TextWrapped(
            "If you changed hardware or this is a new PC, request a HWID reset. "
            "An admin will review and approve or deny it.");

        ImGui::Dummy(ImVec2(0, 4));

        if (g_hwidResetSubmitted) {
            if (g_hwidResetMessage.empty()) {
                ImGui::TextColored(COL_TEXT_DIM, "Submitting request...");
            } else {
                ImGui::TextWrapped("%s", g_hwidResetMessage.c_str());
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.40f, 0.05f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.50f, 0.10f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.30f, 0.03f, 1.00f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            if (ImGui::Button("Request HWID Reset",
                              ImVec2(ImGui::GetContentRegionAvail().x, 36))) {
                g_hwidResetSubmitted = true;
                Hwid::HardwareDetails details = Hwid::GetHardwareDetails();
                Api::HwidResetResponse r = Api::SubmitHwidResetRequest(g_authInfo.token, g_hwid, details);
                g_hwidResetMessage = r.message;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0, 10));

    // Bottom action
    //
    // Single-attempt policy: once the user clicks "Launch", the loader commits
    // to one injection cycle. No matter the outcome, we do NOT send them back
    // to the product library — they can only close the loader.
    //   * Success       → auto-close after 1500ms (set in LaunchProduct).
    //   * Still working → show "Working — please wait..." text (no button).
    //   * HWID mismatch → existing reset flow is shown above; this row offers
    //                     a Close button so the user can dismiss after reading
    //                     the admin-review message.
    //   * Asset/RunPE failure → user must click Close to dismiss. This gives
    //                     them time to read the error before the app exits.
    if (injectionLocked && !succeeded) {
        if (g_fontSmall) ImGui::PushFont(g_fontSmall);
        ImGui::TextColored(COL_TEXT_FAINT, "Working — please wait...");
        if (g_fontSmall) ImGui::PopFont();
    } else if (succeeded) {
        if (g_fontSmall) ImGui::PushFont(g_fontSmall);
        ImGui::TextColored(COL_TEXT_FAINT, "The loader will close automatically.");
        if (g_fontSmall) ImGui::PopFont();
    } else {
        // Injection attempt completed without success. Offer a single action:
        // dismiss the error / reset message and exit the loader.
        const char* label = g_hwidMismatch
            ? "Close Loader"
            : "Dismiss and Close";
        if (GhostButton(label, ImVec2(avail, 36))) {
            g_shouldExit = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward declare ImGui_ImplWin32_WndProcHandler
// ─────────────────────────────────────────────────────────────────────────────
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // ── VM / Sandbox detection ───────────────────────────────────────────
    {
        auto vmResult = VMDetect::Check();
        if (vmResult.isVM) {
            Api::ReportDetection(
                vmResult.isVMEnv,
                vmResult.isDebugger,
                /*hwid=*/ "",
                vmResult.triggers
            );
            return 0;
        }
    }

    // Generate hardware fingerprint used for HWID binding
    g_hwid = Hwid::GetHWID();
    if (g_hwid.empty()) {
        std::cout << OBF_S("[HWID] Warning: Failed to generate HWID, using fallback.\n");
        g_hwid = OBF_S("UNKNOWN");
    } else {
        std::cout << OBF_S("[HWID] ") << g_hwid << "\n";
    }

    // ── DPI awareness so we render crisply on any monitor ───────────────
    {
        // Per-monitor V2 if available (Win10 1703+); fall back gracefully.
        typedef BOOL (WINAPI *PFN_SetCtx)(DPI_AWARENESS_CONTEXT);
        HMODULE u32 = ::GetModuleHandleW(OBF_C(L"user32.dll"));
        if (auto setCtx = (PFN_SetCtx)::GetProcAddress(u32, OBF_A("SetProcessDpiAwarenessContext"))) {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else if (auto setCtxOld = (PFN_SetCtx)::GetProcAddress(u32, OBF_A("SetProcessDPIAware"))) {
            ((BOOL (WINAPI *)())setCtxOld)();
        }

        // Get DPI for the primary monitor (good enough — we don't move between displays often).
        UINT dpi = 96;
        typedef UINT (WINAPI *PFN_GetSysDpi)();
        if (auto getSysDpi = (PFN_GetSysDpi)::GetProcAddress(u32, OBF_A("GetDpiForSystem"))) {
            dpi = getSysDpi();
        } else {
            HDC hdc = ::GetDC(nullptr);
            if (hdc) { dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX); ::ReleaseDC(nullptr, hdc); }
        }
        g_uiScale = (float)dpi / 96.0f;
        if (g_uiScale < 0.75f) g_uiScale = 1.0f; // sanity floor
    }

    // Register window class
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ScootwareLoader", nullptr };
    ::RegisterClassExW(&wc);

    // Center window on primary monitor (in physical pixels)
    int wndW = SXi(WND_W_BASE);
    int wndH = SXi(WND_H_BASE);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - wndW) / 2;
    int y = (screenH - wndH) / 2;

    // Borderless: WS_POPUP only. WS_MINIMIZEBOX | WS_SYSMENU let the taskbar
    // animate the minimize/restore as expected.
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, L"Scootware Loader",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        x, y, wndW, wndH,
        nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Round all 4 corners (works on both Win10 and Win11).
    {
        // Try native DWM rounded corners first (Win11)
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
        int cornerPref = 2; // DWMWCP_ROUND
        ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

        // Use DwmExtendFrameIntoClientArea to allow transparent background for antialiased ImGui rounded corners
        MARGINS margins = { -1 };
        ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Fonts: Segoe UI for crisp Windows-native typography. Fall back to default.
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2010, 0x2BFF, 0 };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = true;

    g_fontBody  = io.Fonts->AddFontFromFileTTF(OBF_A("C:\\Windows\\Fonts\\segoeui.ttf"),  SX(17.0f), &cfg, ranges);
    g_fontSmall = io.Fonts->AddFontFromFileTTF(OBF_A("C:\\Windows\\Fonts\\segoeui.ttf"),  SX(14.0f), &cfg, ranges);
    g_fontTitle = io.Fonts->AddFontFromFileTTF(OBF_A("C:\\Windows\\Fonts\\segoeuib.ttf"), SX(22.0f), &cfg, ranges);
    g_fontHero  = io.Fonts->AddFontFromFileTTF(OBF_A("C:\\Windows\\Fonts\\segoeuib.ttf"), SX(30.0f), &cfg, ranges);
    if (!g_fontBody) {
        io.Fonts->AddFontDefault();
    }

    SetupImGuiStyle();
    ImGui::GetStyle().ScaleAllSizes(g_uiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Async image cache for product backgrounds + user avatar.
    ImageLoader::Init(g_pd3dDevice, g_pd3dDeviceContext);

    auto requestSessionImages = []() {
        if (!g_authInfo.avatarUrl.empty()) {
            ImageLoader::Request("avatar", g_authInfo.avatarUrl);
        }
        for (const auto& p : g_authInfo.subscriptions) {
            ImageLoader::Request("prod:" + p.productId, ProductImagePath(p.productId));
        }
    };

    // Auto-login if session exists
    Session::Credentials creds;
    if (Session::LoadCredentials(creds)) {
        g_statusMessage = "Auto-logging in...";
        g_authInfo = Api::Login(creds.username, creds.password);
        if (g_authInfo.success) {
            std::vector<Api::ProductAccess> activeSubs;
            for (const auto& p : g_authInfo.subscriptions) {
                if (p.hasAccess) {
                    activeSubs.push_back(p);
                }
            }
            g_authInfo.subscriptions = activeSubs;

            GotoState(AppState::Products);
            g_statusMessage = "";
            g_rememberMe = true;
            strcpy_s(g_username, creds.username.c_str());
            requestSessionImages();
        } else {
            ShowPopup("Session Expired", "Please login again.", COL_WARNING);
            Session::ClearCredentials();
        }
    }

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        uint64_t closeAt = g_autoCloseAt.load();
        if (closeAt != 0 && (uint64_t)GetTickCount64() >= closeAt) {
            g_shouldExit = true;
        }
        if (g_shouldExit.load()) {
            ::PostQuitMessage(0);
            done = true;
            break;
        }

        // Animate screen fade-in
        if (g_screenFade < 1.0f) {
            g_screenFade += io.DeltaTime * 5.0f; // ~200ms ease
            if (g_screenFade > 1.0f) g_screenFade = 1.0f;
        }

        // Upload any newly-decoded images to the GPU.
        ImageLoader::Pump();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Full-window fill
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings);

        DrawTitleBar();

        // Content region (below title bar) with padding, fades in on transition
        ImGui::SetCursorPos(ImVec2(0, TITLE_BAR_H));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_screenFade);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
        
        // Suppress scrollbar dynamically (especially on the compressed login screen)
        ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoBackground;
        if (g_state == AppState::Login || g_state == AppState::Injecting) {
            childFlags |= ImGuiWindowFlags_NoScrollbar;
        }

        ImGui::BeginChild("##content",
                          ImVec2(io.DisplaySize.x, io.DisplaySize.y - TITLE_BAR_H),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          childFlags);

        if      (g_state == AppState::Login)     DrawLoginScreen();
        else if (g_state == AppState::Products)  DrawProductScreen();
        else if (g_state == AppState::Injecting) DrawInjectingScreen();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        // Animated rainbow border on top of everything
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 a = ImVec2(WINDOW_BORDER_PX * 0.5f, WINDOW_BORDER_PX * 0.5f);
            ImVec2 b = ImVec2(io.DisplaySize.x - WINDOW_BORDER_PX * 0.5f,
                              io.DisplaySize.y - WINDOW_BORDER_PX * 0.5f);
            float phase = fmodf((float)ImGui::GetTime() * 0.10f, 1.0f);
            DrawRainbowBorder(dl, a, b, WINDOW_BORDER_PX, phase);
        }

        // Global Popup Handling
        if (g_popupOpen) {
            ImGui::OpenPopup("##GlobalPopup");
            g_popupOpen = false;
        }

        // Setup modal style to match GUI
        ImGui::PushStyleColor(ImGuiCol_PopupBg, COL_PANEL_ALT);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(g_popupColor.x, g_popupColor.y, g_popupColor.z, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, WND_CORNER_RADIUS);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(24), SX(24)));

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(SX(320), 0), ImVec2(SX(400), 1000));

        if (ImGui::BeginPopupModal("##GlobalPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            
            // "dismissal option outside the loader" (click dimmed background to dismiss)
            if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                ImGui::CloseCurrentPopup();
            }

            if (g_fontTitle) ImGui::PushFont(g_fontTitle);
            CenterText(g_popupTitle.c_str(), nullptr, g_popupColor);
            if (g_fontTitle) ImGui::PopFont();

            ImGui::Dummy(ImVec2(0, SX(10)));

            if (g_fontBody) ImGui::PushFont(g_fontBody);
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(COL_TEXT, "%s", g_popupMessage.c_str());
            ImGui::PopTextWrapPos();
            if (g_fontBody) ImGui::PopFont();

            ImGui::Dummy(ImVec2(0, SX(20)));

            if (GhostButton("Dismiss", ImVec2(ImGui::GetContentRegionAvail().x, SX(36)))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        ImGui::End();
        ImGui::PopStyleVar(2);

        // Render
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Transparent clear color
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync
    }

    // Cleanup
    ImageLoader::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// D3D11 boilerplate
// ─────────────────────────────────────────────────────────────────────────────

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_NCHITTEST: {
            // Borderless drag: top strip is HTCAPTION except where the
            // minimize / close buttons live (right-most CTRL_BTNS_WIDTH px).
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ::ScreenToClient(hWnd, &pt);
            RECT rc; ::GetClientRect(hWnd, &rc);
            if (pt.y >= 0 && pt.y < (LONG)TITLE_BAR_H) {
                if (pt.x >= 0 && pt.x < rc.right - CTRL_BTNS_WIDTH) {
                    return HTCAPTION;
                }
            }
            return HTCLIENT;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
