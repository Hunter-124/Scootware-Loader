#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <cmath>
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <bcrypt.h>

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
#include "security/loader_ipc.h"
#include "shared_memory_ipc.h"   // IPC_TOTAL_SIZE, IPC_MAGIC (from IPC-dependencies/)
#include "security/dcu.h"
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
    Spoofer,
    Settings,
    DcuWizard,
    Injecting
};

static AppState           g_state = AppState::Login;
static AppState           g_lastState = AppState::Login;
static float              g_screenFade = 1.0f;        // 0 → 1 fade-in on screen change
static Api::AuthResponse  g_authInfo;
// Thread-safe status message — written by background threads, read by UI thread.
// A single mutex guards the std::string. Writes heap-allocate a new string;
// reads take a local copy (~200 bytes max) so the caller can pass .c_str()
// to ImGui without worrying about the mutex lifetime.
static std::mutex         g_statusMutex;
static std::string        g_statusMessage;
static void SetStatus(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_statusMessage = msg;
}
static std::string GetStatus() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_statusMessage;
}
static bool StatusIsEmpty() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_statusMessage.empty();
}
static bool               g_statusCopied = false;      // true for ~1.5s after user copies the status
static double             g_statusCopiedTime = 0.0;    // ImGui::GetTime() at copy moment
static char               g_username[64] = "";
static char               g_password[64] = "";
static bool               g_rememberMe = false;
static std::string        g_hwid = "";
// DCU Wizard state
static std::atomic<bool>  g_dcuWizardActive{ false };
static std::atomic<bool>  g_dcuWizardDone{ false };
static std::atomic<bool>  g_dcuInstallResult{ false };  // true=installed, false=cancelled
static DcuStep            g_dcuCurrentStep = DcuStep::Begin;
static std::string        g_dcuStatusText;
static float              g_dcuProgress = 0.0f;
static bool               g_dcuUserWantsYes = false;    // set by UI thread
static bool               g_dcuUserWantsNo  = false;
static bool               g_dcuUserWantsRestart = false;
static std::string        g_dcuProductId;               // product that triggered DCU
static std::string        g_dcuToken;
static std::string        g_dcuHwid;

// Injection runs on a background thread so the UI stays responsive.
static std::atomic<bool>  g_injecting{ false };
static std::atomic<bool>  g_injectionSucceeded{ false };
static std::atomic<bool>  g_injectionFailed{ false };   // asset/RunPE failure (not HWID mismatch)
static std::atomic<bool>  g_shouldExit{ false };
static std::atomic<uint64_t> g_autoCloseAt{ 0 };

// Background thread cleanup — the UI loop awaits this before initiating
// auto-close, ensuring the background thread's ReportLoaderEvent + flag
// writes complete before D3D teardown.
static std::atomic<bool>  g_backgroundCleanupDone{ false };

// HWID mismatch state
static bool               g_hwidMismatch = false;
static bool               g_hwidResetSubmitted = false;   // request was sent to server
static std::atomic<bool>  g_hwidResetInFlight{ false };   // background submit in progress
static bool               g_hwidResetDeclined  = false;   // user explicitly chose not to send
static std::mutex         g_hwidResetMsgMutex;
static std::string        g_hwidResetMessage = "";
static void SetHwidResetMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_hwidResetMsgMutex);
    g_hwidResetMessage = msg;
}
static std::string GetHwidResetMessage() {
    std::lock_guard<std::mutex> lk(g_hwidResetMsgMutex);
    return g_hwidResetMessage;
}

// Global Popups
static std::string g_popupMessage = "";
static std::string g_popupTitle = "";
static ImVec4      g_popupColor = ImVec4(1,1,1,1);
static bool        g_popupOpen = false;

// ─────────────────────────────────────────────────────────────────────────────
// Spoofer screen state
// ─────────────────────────────────────────────────────────────────────────────
enum class SpooferConnState { Idle, Connecting, Connected, Error };
static SpooferConnState          g_spooferConn        = SpooferConnState::Idle;
static std::atomic<bool>         g_spooferOpRunning   { false };
static std::string               g_spooferOpMsg       = "";
static bool                      g_spooferOpSuccess   = false;
static LoaderIpc::HwidStatus     g_spooferStatus      = {};
static bool                      g_spooferStatusLoaded = false;

// Driver bring-up state (driven by the Settings "Load Driver" button and the
// auto-load-on-spoofer-open path). Also gates Settings so the user can't
// double-click the button.
enum class DriverLoadState { Idle, Loading, Ready, Failed };
static std::atomic<DriverLoadState> g_driverLoadState   { DriverLoadState::Idle };
static std::string                   g_driverLoadMsg     = "";
static std::mutex                    g_driverLoadMsgMtx;

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics screen state
// ─────────────────────────────────────────────────────────────────────────────
// Color-tile R/W validation: drives a small heap buffer through the kernel
// driver every frame and renders the written + read-back values side-by-side
// so a mismatch is instantly visible. Replaces the old latency benchmark.
struct ColorValState {
    bool      initialized      = false;
    uint64_t  buf_addr         = 0;       // VA of 4-byte test buffer in this proc
    uint8_t   write_rgb[3]     = {0,0,0}; // last target color we sent
    uint8_t   read_rgb[3]      = {0,0,0}; // last color the driver read back
    bool      last_write_ok    = false;
    bool      last_read_ok     = false;
    bool      last_match       = false;
    uint32_t  write_ok_count   = 0;
    uint32_t  write_fail_count = 0;
    uint32_t  read_ok_count    = 0;
    uint32_t  read_fail_count  = 0;
    uint32_t  match_count      = 0;
    uint32_t  mismatch_count   = 0;
    bool      paused           = false;
    float     cycle_seconds    = 3.5f;
};
static ColorValState g_colorVal;

// ── EFI Configuration State ──
static int  g_efiDseMethod       = 3;  // 0=None 1=AtBoot 2=SetVarHook 3=Auto (default)
static bool g_efiWaitForKeyPress = false;
static bool g_efiStateLoaded     = false;

// ── EFI HWID Spoofer State ──
static bool g_spooferModeEfi       = false;  // false=Temp(driver), true=Perm(EFI)
static char g_efiHwidUuid[40]      = {};     // "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
static char g_efiHwidSysSer[128]   = {};
static char g_efiHwidBbSer[128]    = {};
static char g_efiHwidCpuSer[128]   = {};
static bool g_efiHwidCaptured      = false;
static bool g_efiHwidApplyPending  = false;
static bool g_efiHwidConfigLoaded  = false;
static std::string           g_efiHwidOpMsg     = "";
static bool                  g_efiHwidOpSuccess = false;
static std::atomic<bool>     g_efiHwidOpRunning { false };

// ── Auto window height (set by DrawProductScreen, read by main loop) ──
static float g_neededWindowH = 0.0f;

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
static const float WINDOW_BORDER_PX   = 3.5f;   // rainbow stroke thickness (+2px total)
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
static void DrawRainbowBorder(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax, float r, float thickness, float phase) {
    float w = pmax.x - pmin.x;
    float h = pmax.y - pmin.y;
    float arc = 0.5f * IM_PI * r;
    float perim = 2.0f * (w - 2.0f * r) + 2.0f * (h - 2.0f * r) + 4.0f * arc;
    
    // Dynamically calculate segments to have roughly 2px per segment for high curve resolution
    int segments = (int)(perim / 2.0f);
    if (segments < 100) segments = 100;
    
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / segments;
        // Overlap the next point slightly (1.5 instead of 1.0) to completely hide AA cap gaps between segments
        float t1 = (float)(i + 1.5f) / segments; 
        ImVec2 p0 = RoundedPerimeterPoint(pmin, pmax, r, t0);
        ImVec2 p1 = RoundedPerimeterPoint(pmin, pmax, r, t1);
        float hue = fmodf(t0 + phase, 1.0f);
        float cr, cg, cb;
        ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.00f, cr, cg, cb);
        ImU32 col = ImGui::GetColorU32(ImVec4(cr, cg, cb, 1.0f));
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
        bool enterPressed = ImGui::InputTextWithHint("##pass", "••••••••", g_password, IM_ARRAYSIZE(g_password), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

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
                SetStatus("");
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
    g_hwidResetInFlight  = false;
    g_hwidResetDeclined  = false;
    SetHwidResetMessage("");
    g_injectionFailed    = false;
    SetStatus("Streaming payload for " + productId + "...");

    std::string token = g_authInfo.token;
    std::string hwid  = g_hwid;

    // ── DCU proactive check ──────────────────────────────────────────────
    // Before any streaming or driver bring-up, check whether the EfiGuard
    // boot chain (DCU) is installed and post-boot completed.  If not, show
    // the DCU wizard.  The wizard will either install + prompt restart, or
    // the user cancels → we abort the launch.
    {
        DcuReadinessResult dcu = DcuQueryReadiness();
        LDIAG() << "[launch] DCU readiness: state=" << (int)dcu.state
                << " flags=0x" << std::hex << dcu.flags << std::dec;

        if (dcu.state != DcuState::Ready) {
            LDIAG() << "[launch] DCU not ready — redirecting to DCU wizard";

            // Stash auth data for the wizard thread
            g_dcuProductId = productId;
            g_dcuToken     = token;
            g_dcuHwid      = hwid;

            // Reset wizard state flags
            g_dcuWizardDone     = false;
            g_dcuWizardActive   = false;
            g_dcuInstallResult  = false;
            g_dcuUserWantsYes   = false;
            g_dcuUserWantsNo    = false;
            g_dcuUserWantsRestart = false;
            g_dcuProgress       = 0.0f;
            g_dcuCurrentStep    = DcuStep::Begin;
            g_dcuStatusText     = "";

            // Release the injection lock so the user can try again after reboot
            g_injecting = false;

            // Switch to the DCU wizard screen
            GotoState(AppState::DcuWizard);

            // Report the load attempt as "blocked by DCU"
            Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                OBF_S("blocked by DCU — EfiGuard not installed or post-boot incomplete"),
                token);
            return;
        }

        LDIAG() << "[launch] DCU ready — proceeding with launch";
    }

    // Capture expiresAt NOW (on the calling thread) instead of reading
    // g_authInfo.subscriptions from the background thread.
    std::string expiresAt;
    for (const auto& p : g_authInfo.subscriptions) {
        if (p.productId == productId) { expiresAt = p.expiresAt; break; }
    }

    std::thread([productId, token, hwid, expiresAt]() {
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
            SetStatus("Waiting for game process (" + productId + ")...");

            const bool ok = ProcessWait::WaitForProcess(
                hostExe,
                /*cancel*/ []() { return false; }, // window close kills the process
                /*tick  */ [productId](int seconds) {
                    // Cosmetic countdown so the user sees the loader is
                    // still alive and not hung.
                    SetStatus("Waiting for game process (" + productId + ")... "
                            + std::to_string(seconds) + "s");
                        });

            if (!ok) {
                // Only reachable if WaitForProcess gets a cancel signal
                // — currently never, but kept symmetric for future use.
                SetStatus("Cancelled — game process never started.");
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("cold-start cancelled before host appeared"), token);
                g_injecting = false;
                return;
            }

            SetStatus("Game process detected — streaming payload...");
        }

        // hollow_exe is still streamed because the driver mapper hollows
        // itself into a fresh `scootware.exe` host (DriverBringup::Run
        // stages its own temp copy from these bytes). The kernel-inject
        // path no longer needs a second host for the cheat — the DLL is
        // mapped directly into the game process — so we keep the bytes in
        // memory only and never write them to disk on the loader side.
        auto [hollowBuffer, hollowAllocSize] = Api::StreamAsset(OBF_S("SHARED"), OBF_S("hollow_exe"), token, hwid);

        // ── Stream the per-product cheat DLL ──────────────────────────────
        //
        // Replaces the historical primary_exe -> RunPE process-hollow flow.
        // The driver maps the bytes directly into the game process via
        // CMD_INJECT_DLL (PT-injector), so the IP-protection model is the
        // same encrypted/HWID-bound transport already used for hollow_exe
        // / driver_loader — the plaintext lives only in this process's heap
        // and is wiped right after the driver finishes mapping it. There is
        // no on-disk artifact anywhere.
        auto [exeBuffer, allocSize] = Api::StreamAsset(productId, OBF_S("inject_dll"), token, hwid);
        (void)allocSize; // injection does not need the alloc-size hint runpe used

        if (!exeBuffer.empty()) {
            if (token.empty()) {
                LDIAG_LINE("[inject] aborting — empty session token from Api::Login "
                           "(server probably returned no `sessionId`/`token` field).");
                SetStatus("Login session token missing — please re-login.");
                g_injectionFailed = true;
                Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid,
                    OBF_S("aborted before inject: empty session token from /api/auth/login"),
                    /*token*/ "");
                g_injecting = false;
                return;
            }

            // ── Kernel driver bring-up (loader-owned IPC) ─────────────────
            //
            // The loader is now itself a recognized IPC target (its image
            // name "scootware-loader.exe" is in FINAL-DRV/driver.cpp's
            // target_names table). That lets us run the entire bring-up
            // handshake in-process, with no helper scootware.exe probe:
            //
            //   1. Init loader IPC (page-aligned .bss with IPC_MAGIC).
            //   2. Ping. If a driver answers, it's already attached to
            //      *us*, so ask it to shut down cleanly before we map a
            //      fresh one. Refusal = bail (don't double-load).
            //   3. Map driver via DriverBringup::Run (now probe-less).
            //   4. Re-ping to confirm the freshly-mapped driver actually
            //      attached to our IPC. Failure = bail.
            //   5. Resolve the game-process PID from the server-supplied
            //      InjectConfig.targetProcessName and issue CMD_INJECT_DLL
            //      pointing the driver at our in-memory DLL buffer.
            //   6. Wipe the DLL plaintext, burn IPC magic, schedule exit.
            //
            SetStatus("Bringing up kernel driver...");
            LoaderIpc::Init();

            // Step 2: pre-flight ping. 1.5 s budget — a healthy driver
            // answers in < 100 ms; the rest is margin for cold scheduler
            // jitter on first launch.
            const bool driverAlreadyUp = LoaderIpc::Ping(1500);
            if (driverAlreadyUp) {
                // Driver already attached to our loader IPC — reuse it. The
                // re-scan onto the cheat happens when the loader exits in
                // step 6 just like the freshly-mapped path, so there's no
                // need to tear down and re-map (which would otherwise burn
                // an extra vulnerable-driver load per launch).
                LDIAG_LINE("[inject] driver already attached — skipping bring-up");
                SetStatus("Driver already loaded — reusing.");
            }

          if (!driverAlreadyUp) {
            // Step 3: stream + hollow + map.
            DriverBringup::MapperConfig mapperCfg = Api::GetMapperConfig(OBF_S("SHARED"), token);
            LDIAG() << "[inject] mapper config — provider=" << mapperCfg.providerPublicId
                    << " scVer=" << mapperCfg.shellCodeVersion
                    << " requireDse=" << mapperCfg.requireDseSuccess
                    << " objName=" << mapperCfg.driverObjectName
                    << " regPath=" << mapperCfg.driverRegistryPath;

            DriverBringup::Outcome drv = DriverBringup::Run(
                OBF_S("SHARED"), token, hwid, hollowBuffer, hollowAllocSize,
                mapperCfg);
            if (drv.result != DriverBringup::Result::Success) {
                LDIAG() << "[inject] DriverBringup::Run failed: " << drv.details;
                SetStatus("Driver bring-up failed: " + drv.details);
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("driver bring-up failed — ") + drv.details;
                LoaderIpc::Release();
                if (!finalEventType.empty()) {
                    Api::ReportLoaderEvent(finalEventType, productId, hwid,
                                           finalDetails, token);
                }
                g_injecting = false;
                return;
            }

            // Step 4: confirm. The driver's discovery thread sleeps up to
            // 500 ms between scans when idle, so 6 s gives several scan
            // ticks plus a margin for the mapper's own teardown.
            SetStatus("Confirming driver attachment...");
            if (!LoaderIpc::Ping(6000)) {
                LDIAG_LINE("[inject] driver mapper succeeded but loader IPC "
                           "PING never came back — driver did not attach");
                SetStatus("Driver mapped but did not attach (IPC silent).");
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("driver mapped but loader IPC PING timed out — driver never attached");
                LoaderIpc::Release();
                if (!finalEventType.empty()) {
                    Api::ReportLoaderEvent(finalEventType, productId, hwid,
                                           finalDetails, token);
                }
                g_injecting = false;
                return;
            }
            LDIAG_LINE("[inject] driver attached to loader IPC — bring-up confirmed");
          } // end if (!driverAlreadyUp)

            // ── Step 5: kernel-injection delivery ─────────────────────────
            //
            // The driver is now attached to this loader by image name and is
            // servicing slot-0 commands on our in-image IPC buffer.  Ask the
            // server which game process the streamed DLL belongs in, resolve
            // it to a PID, and hand the driver our heap pointer; the
            // PT-injector reads the bytes cross-process via CR3 and maps
            // them into the target with the requested stealth alloc mode.
            //
            SetStatus("Fetching injection config...");
            Api::InjectConfig injCfg = Api::GetInjectConfig(productId, token);
            LDIAG() << "[inject] inject config — targetProcessName='"
                    << injCfg.targetProcessName
                    << "' allocMode=" << injCfg.allocMode;

            if (injCfg.targetProcessName.empty()) {
                LDIAG_LINE("[inject] aborting — server did not supply a "
                           "targetProcessName for this product (admin panel "
                           "field probably not set yet)");
                SetStatus("Server did not specify a target process.");
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("inject config has no targetProcessName — set it in Admin -> Products");
                ::SecureZeroMemory(exeBuffer.data(), exeBuffer.size());
                LoaderIpc::Release();
                if (!finalEventType.empty()) {
                    Api::ReportLoaderEvent(finalEventType, productId, hwid,
                                           finalDetails, token);
                }
                g_injecting = false;
                return;
            }

            // Resolve the PID. The product-level WaitForProcess above already
            // gated the user on the host being up, but the server-supplied
            // process name is the authoritative one for injection — and may
            // differ from the legacy hardcoded HostProcessFor() table. Do a
            // bounded poll so a slightly-late spawn does not race us.
            const std::wstring wideTarget(
                injCfg.targetProcessName.begin(), injCfg.targetProcessName.end());

            DWORD targetPid = 0;
            {
                const ULONGLONG deadline = ::GetTickCount64() + 10000;
                while (true) {
                    targetPid = ::ProcessWait::PidFor(wideTarget);
                    if (targetPid != 0) break;
                    if (::GetTickCount64() >= deadline) break;
                    ::Sleep(250);
                }
            }

            if (targetPid == 0) {
                LDIAG() << "[inject] aborting — could not find process '"
                        << injCfg.targetProcessName << "' within 10s window";
                SetStatus("Target process '" + injCfg.targetProcessName
                          + "' not running.");
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("target process not found: ")
                               + injCfg.targetProcessName;
                ::SecureZeroMemory(exeBuffer.data(), exeBuffer.size());
                LoaderIpc::Release();
                if (!finalEventType.empty()) {
                    Api::ReportLoaderEvent(finalEventType, productId, hwid,
                                           finalDetails, token);
                }
                g_injecting = false;
                return;
            }

            SetStatus("Injecting into " + injCfg.targetProcessName + "...");
            LDIAG() << "[inject] target='" << injCfg.targetProcessName
                    << "' pid=" << targetPid
                    << " dll_size=" << exeBuffer.size()
                    << " alloc_mode=" << injCfg.allocMode;

            uint64_t remoteBase = 0;
            const bool injectOk = LoaderIpc::InjectDll(
                targetPid,
                exeBuffer.data(),
                static_cast<uint32_t>(exeBuffer.size()),
                injCfg.allocMode,
                &remoteBase,
                /*timeout_ms*/ 15000);

            // Plaintext DLL bytes have been copied into the target by the
            // driver (via ExAllocatePool2 + read_process_memory); our local
            // copy is dead weight now and wiping it shrinks the window where
            // a memory-dumping AV could lift the unencrypted image off our
            // heap.
            ::SecureZeroMemory(exeBuffer.data(), exeBuffer.size());

            // Burn the loader IPC magic so the driver's next alive-check
            // sees magic=0 and tears down the loader session. The injected
            // DLL has no loader-owned IPC buffer of its own, so the driver
            // simply goes idle after teardown — that is the intended
            // post-injection state.
            LoaderIpc::Release();

            if (injectOk) {
                LDIAG() << "[inject] CMD_INJECT_DLL OK — remote_base=0x"
                        << std::hex << remoteBase << std::dec;
                SetStatus("Injected.");
                g_injectionSucceeded = true;
                // No env-handoff window to keep alive (no child process); the
                // injected DLL is already executing DllMain in the target.
                // Exit promptly so the loader window does not linger.
                g_autoCloseAt  = (uint64_t)GetTickCount64() + 500;
                finalEventType = OBF_S("load_success");
                finalDetails   = OBF_S("driver online, DLL mapped into ")
                               + injCfg.targetProcessName
                               + OBF_S(" via CMD_INJECT_DLL");
            } else {
                SetStatus("Injection failed (driver rejected CMD_INJECT_DLL).");
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S(
                    "CMD_INJECT_DLL returned failure — see %TEMP%\\scootware-diag.log "
                    "for driver-side debug output (provider, alloc, map, or execute stage)");
            }
        } else {
            g_hwidMismatch = Api::LastStreamWasHwidMismatch();
            if (g_hwidMismatch) {
                SetStatus("HWID mismatch — this account is bound to a different machine.");
                // HWID mismatch is ALSO logged server-side from the stream
                // endpoint (tamper-resistant). We skip client reporting here
                // to avoid duplicate rows in the admin panel.
                // The reset request is NOT auto-submitted; the user chooses
                // via the HWID mismatch panel (see DrawProductScreen).
                finalEventType = "";
            } else {
                SetStatus("Failed to stream asset from secure server.");
                g_injectionFailed = true;
                finalEventType = OBF_S("load_failed");
                finalDetails   = OBF_S("stream request failed (inject_dll unavailable)");
            }
        }

        // Single consolidated report for the whole injection cycle.
        if (!finalEventType.empty()) {
            Api::ReportLoaderEvent(finalEventType, productId, hwid, finalDetails, token);
        }

        g_injecting = false;
            // Signal the UI loop that background cleanup (ReportLoaderEvent,
            // flag writes) is complete.  The auto-close path in WinMain awaits
            // this before initiating D3D teardown and process exit.
            g_backgroundCleanupDone.store(true, std::memory_order_release);
    }).detach();
}

// Forward declarations for spoofer helpers (defined after DrawProductScreen)
// ── EFI HWID helpers ─────────────────────────────────────────────────────────

static void EfiHwidFormatUuid(const uint8_t uuid[16], char out[40]) {
    snprintf(out, 40,
        "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        uuid[0],uuid[1],uuid[2],uuid[3],
        uuid[4],uuid[5],
        uuid[6],uuid[7],
        uuid[8],uuid[9],
        uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
}

// Returns true when str is a valid "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" UUID.
static bool EfiHwidParseUuid(const char* str, uint8_t out[16]) {
    if (!str || strlen(str) != 36) return false;
    unsigned b[16] = {};
    int n = sscanf_s(str,
        "%2X%2X%2X%2X-%2X%2X-%2X%2X-%2X%2X-%2X%2X%2X%2X%2X%2X",
        &b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&b[6],&b[7],
        &b[8],&b[9],&b[10],&b[11],&b[12],&b[13],&b[14],&b[15]);
    if (n != 16) return false;
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(b[i]);
    return true;
}

// Load EFI HWID fields from the ESP config (background thread).
static void EfiHwidLoad() {
    if (g_efiHwidOpRunning.exchange(true)) return;
    g_efiHwidOpMsg     = "";
    g_efiHwidOpSuccess = false;
    std::thread([]() {
        DcuEfiHwidSpoof spoof = {};
        bool ok = DcuReadEfiHwidConfig(spoof);
        if (ok) {
            EfiHwidFormatUuid(spoof.uuid, g_efiHwidUuid);
            memcpy(g_efiHwidSysSer, spoof.systemSerial,    128);
            memcpy(g_efiHwidBbSer,  spoof.baseboardSerial, 128);
            memcpy(g_efiHwidCpuSer, spoof.processorSerial, 128);
            g_efiHwidCaptured     = spoof.captured;
            g_efiHwidApplyPending = spoof.applyPending;
            g_efiHwidConfigLoaded = true;
            g_efiHwidOpSuccess    = true;
            g_efiHwidOpMsg        = "";
        } else {
            // Config absent or invalid — clear fields so the user starts fresh
            memset(g_efiHwidUuid,   0, sizeof(g_efiHwidUuid));
            memset(g_efiHwidSysSer, 0, sizeof(g_efiHwidSysSer));
            memset(g_efiHwidBbSer,  0, sizeof(g_efiHwidBbSer));
            memset(g_efiHwidCpuSer, 0, sizeof(g_efiHwidCpuSer));
            g_efiHwidCaptured     = false;
            g_efiHwidApplyPending = false;
            g_efiHwidConfigLoaded = true;  // mark loaded even on miss (first-run empty state)
            g_efiHwidOpMsg        = "No existing EFI config — fields cleared for first-time setup.";
            g_efiHwidOpSuccess    = false;
        }
        g_efiHwidOpRunning = false;
    }).detach();
}

// Apply EFI HWID fields to the ESP config (background thread).
static void EfiHwidApply() {
    if (g_efiHwidOpRunning.exchange(true)) return;
    g_efiHwidOpMsg     = "";
    g_efiHwidOpSuccess = false;

    // Capture current field values before handing off to thread
    DcuEfiHwidSpoof spoof = {};
    EfiHwidParseUuid(g_efiHwidUuid, spoof.uuid);
    memcpy(spoof.systemSerial,    g_efiHwidSysSer, 128);
    memcpy(spoof.baseboardSerial, g_efiHwidBbSer,  128);
    memcpy(spoof.processorSerial, g_efiHwidCpuSer, 128);

    std::thread([spoof]() {
        bool ok = DcuWriteEfiHwidConfig(spoof);
        if (ok) {
            g_efiHwidApplyPending = true;
            g_efiHwidOpSuccess    = true;
            g_efiHwidOpMsg        = "Written to ESP. Reboot to apply permanent HWID spoof.";
        } else {
            g_efiHwidOpMsg = "Failed to write ESP config. Is the Compatibility Module installed?";
        }
        g_efiHwidOpRunning = false;
    }).detach();
}

static void SpooferConnect();

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
        // SPOOFER opens its own UI screen instead of launching an injection.
        std::string pidLower = LowerCase(prod.productId);
        if (pidLower == "spoofer") {
            g_spooferConn         = SpooferConnState::Idle;
            g_spooferStatusLoaded = false;
            g_spooferOpMsg        = "";
            GotoState(AppState::Spoofer);
            SpooferConnect();
        } else {
            LaunchProduct(prod.productId);
        }
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
        // Settings occupies the last grid slot. totalWithBench drives layout.
        const size_t totalWithBench = total + 1;

    const float CARD_RADIUS  = SX(12.0f);
    const float CARD_PAD     = SX(14.0f);
    const float CARD_GAP     = SX(10.0f);
    const float MIN_CARD_W   = SX(220.0f);

    int cols = (int)((availTotal + CARD_GAP) / (MIN_CARD_W + CARD_GAP));
    if (cols < 1) cols = 1;
        if (cols > (int)totalWithBench) cols = (int)totalWithBench;
    if (cols > 3) cols = 3;

    const float cardW = (availTotal - (cols - 1) * CARD_GAP) / (float)cols;
        const int   rows  = (cols > 0) ? (int)((totalWithBench + cols - 1) / cols) : 0;

    float cardH = (cols == 1) ? SX(110.0f) : SX(140.0f);

    float availH = ImGui::GetContentRegionAvail().y;
    float gridH  = rows * cardH + (rows - 1) * CARD_GAP;
    if (gridH > availH && rows > 0) {
        cardH = (availH - (rows - 1) * CARD_GAP) / (float)rows;
        if (cardH < SX(96.0f)) cardH = SX(96.0f);
        gridH = rows * cardH + (rows - 1) * CARD_GAP;
    }

    ImVec2 gridStart = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(availTotal, gridH));

    // ── Product cards ──
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

        // ── Settings tile (last slot in the grid) ──
    {
            int   bi  = (int)total;
            int   col = bi % cols;
            int   row = bi / cols;

            // Check if settings starts a new row alone — if so, span full width
            bool settingsAlone = (bi % cols == 0);
            float tileW = settingsAlone ? availTotal : cardW;
            float tileH = settingsAlone ? SX(120.0f) : cardH;

            ImVec2 tMin(settingsAlone
                            ? ImVec2(gridStart.x, gridStart.y + row * (cardH + CARD_GAP))
                            : ImVec2(gridStart.x + col * (cardW + CARD_GAP),
                                     gridStart.y + row * (cardH + CARD_GAP)));
            ImVec2 tMax(tMin.x + tileW, tMin.y + tileH);

            ImGui::PushID("##settings_tile");
            ImGui::SetCursorScreenPos(tMin);
            bool clicked = ImGui::InvisibleButton("##settings_tile_btn", ImVec2(tileW, tileH));
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();

            if (clicked) GotoState(AppState::Settings);

            ImDrawList* sdl = ImGui::GetWindowDrawList();

            // Background image from website assets — same style as product cards
            // Alternative endpoints for easy cycling:
            // ImageLoader::Get("settings_bg") from: "/api/assets/backgrounds/settings"
            //                                    or: "/api/assets/alt/settings"
            int pw = 0, ph = 0;
            ID3D11ShaderResourceView* bg = ImageLoader::Get("settings_bg", &pw, &ph);

            ImVec4 baseColor = ImVec4(1, 1, 1, 1);
            if (hovered) baseColor = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            if (active)  baseColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

            sdl->AddRectFilled(tMin, tMax, ImGui::GetColorU32(COL_PANEL), CARD_RADIUS);

            if (bg && pw > 0 && ph > 0) {
                    float cardAR = tileW / tileH;
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
                sdl->AddImageRounded((ImTextureID)(intptr_t)bg, tMin, tMax, uv0, uv1,
                                     ImGui::GetColorU32(baseColor),
                                     CARD_RADIUS, ImDrawFlags_RoundCornersAll);
            }

            // Dark scrim — same as product cards
            sdl->AddRectFilled(tMin, tMax,
                               ImGui::GetColorU32(ImVec4(0, 0, 0, hovered ? 0.35f : 0.58f)),
                               CARD_RADIUS);

        // Border
        if (hovered) {
                sdl->AddRect(tMin, tMax, ImGui::GetColorU32(COL_PURPLE_HOV),
                         CARD_RADIUS, 0, SX(2.0f));
            float t = (sinf((float)ImGui::GetTime() * 4.0f) + 1.0f) * 0.5f;
            ImU32 glow = ImGui::GetColorU32(ImVec4(COL_PURPLE.x, COL_PURPLE.y,
                                                        COL_PURPLE.z, 0.20f + 0.20f * t));
                for (int gi = 1; gi <= 6; ++gi)
                    sdl->AddRect(ImVec2(tMin.x - gi, tMin.y - gi),
                             ImVec2(tMax.x + gi, tMax.y + gi),
                             glow, CARD_RADIUS + gi, 0, 1.0f);
        } else {
                sdl->AddRect(tMin, tMax, ImGui::GetColorU32(ImVec4(1,1,1,0.10f)),
                         CARD_RADIUS, 0, SX(1.5f));
        }

        // Gear icon drawn with lines (cog wheel approximation)
            float useW = settingsAlone ? availTotal : cardW;
            ImVec2 center(tMin.x + useW * 0.5f, tMin.y + tileH * 0.5f - SX(10.0f));
        float  gr     = SX(14.0f);   // outer radius
        float  ir     = SX(7.0f);    // inner (hub) radius
        int    teeth  = 8;
        ImU32  iconCol = ImGui::GetColorU32(hovered ? COL_PURPLE_HOV : COL_TEXT_FAINT);
        // Draw teeth as alternating inner/outer points around the circle
        ImVector<ImVec2> pts;
        for (int k = 0; k < teeth * 2; ++k) {
            float angle = ((float)k / (teeth * 2)) * 2.0f * IM_PI - IM_PI / (teeth * 2);
            float r     = (k % 2 == 0) ? gr : gr * 0.72f;
            pts.push_back(ImVec2(center.x + cosf(angle) * r,
                                 center.y + sinf(angle) * r));
        }
            sdl->AddPolyline(pts.Data, pts.Size, iconCol, ImDrawFlags_Closed, SX(1.8f));
            sdl->AddCircle(center, ir, iconCol, 0, SX(1.8f));

        // Label
            float  labelY = tMin.y + tileH * 0.5f + SX(10.0f);
        if (g_fontBody) ImGui::PushFont(g_fontBody);
            ImVec2 labelSz = ImGui::CalcTextSize("Settings");
            sdl->AddText(ImVec2(tMin.x + (useW - labelSz.x) * 0.5f, labelY),
                     ImGui::GetColorU32(hovered ? COL_TEXT : COL_TEXT_DIM),
                         "Settings");
        if (g_fontBody) ImGui::PopFont();


        ImGui::PopID();
    }

        // ── Auto window height: compute needed content height ──
        {
              // Use uncompressed grid dimensions (cardH might have been shrunk by availH check above)
              float cardHOrig = (cols == 1) ? SX(110.0f) : SX(140.0f);
              int rowsCalc = (int)((totalWithBench + cols - 1) / cols);
              float gridHCalc = rowsCalc * cardHOrig + (rowsCalc - 1) * CARD_GAP;
              float contentH = SX(16.0f) + headerH + SX(10.0f) + gridHCalc + SX(16.0f);
            g_neededWindowH = TITLE_BAR_H + contentH;
        }
}

// ─────────────────────────────────────────────────────────────────────────────
// Spoofer screen
// ─────────────────────────────────────────────────────────────────────────────

// Synchronously bring up the kernel driver via the standard hollowed mapper.
// Caller must already be on a worker thread; this blocks for several seconds
// on the streaming + mapping work. Returns true once a fresh driver has
// answered LoaderIpc::Ping. Safe to call when a driver is already up — the
// pre-flight Ping short-circuits and reports success.
static bool BringUpDriverStandalone(std::string& outDetails) {
    auto setMsg = [](const std::string& s) {
        std::lock_guard<std::mutex> lk(g_driverLoadMsgMtx);
        g_driverLoadMsg = s;
    };

    LoaderIpc::Init();
    if (LoaderIpc::Ping(1500)) {
        outDetails = "driver already attached";
        return true;
    }

    const std::string token = g_authInfo.token;
    const std::string hwid  = g_hwid;
    if (token.empty()) {
        outDetails = "not logged in (no session token)";
        return false;
    }

    setMsg("Streaming hollow host...");
    auto [hollowBuffer, hollowAllocSize] =
        Api::StreamAsset(OBF_S("SHARED"), OBF_S("hollow_exe"), token, hwid);
    if (hollowBuffer.empty()) {
        outDetails = "hollow_exe stream failed";
        return false;
    }

    setMsg("Fetching mapper configuration...");
    DriverBringup::MapperConfig mapperCfg = Api::GetMapperConfig(OBF_S("SHARED"), token);

    setMsg("Mapping kernel driver...");
    DriverBringup::Outcome drv = DriverBringup::Run(
        OBF_S("SHARED"), token, hwid, hollowBuffer, hollowAllocSize, mapperCfg);
    if (drv.result != DriverBringup::Result::Success) {
        outDetails = "driver bring-up failed — " + drv.details;
        Api::ReportLoaderEvent(OBF_S("load_failed"), OBF_S("SHARED"), hwid,
                               outDetails, token);
        return false;
    }

    setMsg("Confirming driver attachment...");
    if (!LoaderIpc::Ping(6000)) {
        outDetails = "driver mapped but loader IPC PING timed out";
        Api::ReportLoaderEvent(OBF_S("load_failed"), OBF_S("SHARED"), hwid,
                               outDetails, token);
        return false;
    }

    outDetails = "driver loaded";
    return true;
}

// Settings "Load Driver" button entry point. Spawns a worker so the UI keeps
// rendering while the bring-up runs.
static void LoadDriverAsync() {
    DriverLoadState expected = DriverLoadState::Idle;
    if (!g_driverLoadState.compare_exchange_strong(expected, DriverLoadState::Loading) &&
        expected != DriverLoadState::Failed && expected != DriverLoadState::Ready) {
        return; // already loading
    }
    g_driverLoadState.store(DriverLoadState::Loading);
    {
        std::lock_guard<std::mutex> lk(g_driverLoadMsgMtx);
        g_driverLoadMsg = "Starting driver bring-up...";
    }
    std::thread([]() {
        std::string details;
        bool ok = BringUpDriverStandalone(details);
        {
            std::lock_guard<std::mutex> lk(g_driverLoadMsgMtx);
            g_driverLoadMsg = details;
        }
        g_driverLoadState.store(ok ? DriverLoadState::Ready : DriverLoadState::Failed);
    }).detach();
}

static void SpooferConnect() {
    if (g_spooferConn == SpooferConnState::Connecting) return;
    g_spooferConn = SpooferConnState::Connecting;
    std::thread([]() {
        LoaderIpc::Init();
        // Auto-load the driver if it isn't already attached. Without this the
        // user would land on the spoofer screen, see "Driver not responding",
        // and have to manually go elsewhere to bring the driver up.
        if (!LoaderIpc::Ping(1500)) {
            std::string details;
            if (BringUpDriverStandalone(details)) {
                g_driverLoadState.store(DriverLoadState::Ready);
            } else {
                g_driverLoadState.store(DriverLoadState::Failed);
                std::lock_guard<std::mutex> lk(g_driverLoadMsgMtx);
                g_driverLoadMsg = details;
            }
        }
        if (LoaderIpc::Ping(3000)) {
            // NOTE: deliberately NOT calling LoaderIpc::HwidQueryStatus here.
            // The driver-side HWID spoofer (FINAL-DRV/hwid_spoofer.cpp) is
            // currently disabled via HWID_SPOOFER_ENABLED=0 because its
            // SMBIOS extraction path bugchecks the box with
            // KMODE_EXCEPTION_NOT_HANDLED on the first auto-init triggered
            // by this exact call. Until the kernel-side HWID module is
            // fixed, we leave g_spooferStatus zero-initialised — the
            // spoofer panel will render the "Loaded? false" state, and the
            // user's explicit Save/Spoof/Restore/Reroll button presses will
            // still go through (and the driver will reply with
            // STATUS_IPC_ERROR cleanly instead of crashing).
            g_spooferStatusLoaded = false;
            g_spooferConn        = SpooferConnState::Connected;
        } else {
            g_spooferConn = SpooferConnState::Error;
        }
    }).detach();
}

static void SpooferRunOp(const char* label, std::function<bool()> fn) {
    if (g_spooferOpRunning.exchange(true)) return;
    g_spooferOpMsg     = "";
    g_spooferOpSuccess = false;
    std::string lbl    = label;
    std::thread([lbl, fn]() {
        bool ok = fn();
        if (ok) {
            g_spooferOpSuccess = true;
            g_spooferOpMsg     = lbl + ": OK";
            // Refresh status after the operation
            LoaderIpc::HwidQueryStatus(g_spooferStatus, 5000);
        } else {
            g_spooferOpMsg = lbl + ": driver returned an error";
        }
        g_spooferOpRunning = false;
    }).detach();
}

// Render one HWID field row: dimmed label at fixed column, value next to it.
static void SpooferRow(const char* label, const char* value) {
    if (g_fontSmall) ImGui::PushFont(g_fontSmall);
    ImGui::TextColored(COL_TEXT_DIM, "%-18s", label);
    if (g_fontSmall) ImGui::PopFont();
    ImGui::SameLine();
    std::string val = value && value[0] ? value : "(empty)";
    if (val.size() > 46) val = val.substr(0, 43) + "...";
    ImGui::TextColored(value && value[0] ? COL_TEXT : COL_TEXT_FAINT, "%s", val.c_str());
}

static void DrawSpooferScreen() {
    float avail = ImGui::GetContentRegionAvail().x;

    // ── Header row: back arrow + title + mode toggle ──
    {
        float btnW = SX(80.0f), btnH = SX(30.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.14f));
        ImGui::PushStyleColor(ImGuiCol_Text,          COL_TEXT_DIM);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (ImGui::Button("< Back", ImVec2(btnW, btnH))) {
            g_spooferConn         = SpooferConnState::Idle;
            g_spooferStatusLoaded = false;
            g_spooferOpMsg        = "";
            g_efiHwidConfigLoaded = false;
            g_efiHwidOpMsg        = "";
            g_spooferModeEfi      = false;
            GotoState(AppState::Products);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }
    ImGui::SameLine();
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImGui::TextColored(COL_TEXT, "HWID Spoofer");
    if (g_fontTitle) ImGui::PopFont();

    // Mode toggle — right-aligned in the header row
    {
        float toggleW = SX(200.0f), toggleH = SX(26.0f);
        float toggleX = avail - toggleW;
        if (toggleX > SX(200.0f)) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(toggleX);

            auto ModeBtn = [&](const char* label, bool active) -> bool {
                ImVec4 bg  = active ? COL_PURPLE : ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
                ImVec4 hov = active ? COL_PURPLE_HOV : ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
                ImVec4 act = active ? COL_PURPLE_ACT : ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
                ImVec4 brd = active ? ImVec4(COL_PURPLE_HOV.x, COL_PURPLE_HOV.y, COL_PURPLE_HOV.z, 0.95f)
                                    : ImVec4(1.0f, 1.0f, 1.0f, 0.20f);
                ImGui::PushStyleColor(ImGuiCol_Button,        bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? COL_TEXT : COL_TEXT_DIM);
                ImGui::PushStyleColor(ImGuiCol_Border, brd);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 1.6f : 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                ImVec2 btnSize(toggleW * 0.5f - SX(2.0f), toggleH);
                bool clicked = ImGui::Button(label, btnSize);

                // Extra outer stroke for stronger segmented-toggle border effect.
                ImVec2 bmin = ImGui::GetItemRectMin();
                ImVec2 bmax = ImGui::GetItemRectMax();
                ImU32 outer = ImGui::GetColorU32(active
                    ? ImVec4(COL_PURPLE_HOV.x, COL_PURPLE_HOV.y, COL_PURPLE_HOV.z, 0.70f)
                    : ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2(bmin.x - 1.0f, bmin.y - 1.0f),
                    ImVec2(bmax.x + 1.0f, bmax.y + 1.0f),
                    outer, 7.0f, 0, 1.0f);

                ImGui::PopStyleVar(3);
                ImGui::PopStyleColor(5);
                return clicked;
            };

            if (ModeBtn("Temp (Driver)", !g_spooferModeEfi)) {
                g_spooferModeEfi = false;
                // Reconnect if not already
                if (g_spooferConn == SpooferConnState::Idle ||
                    g_spooferConn == SpooferConnState::Error)
                    SpooferConnect();
            }
            ImGui::SameLine(0, SX(4.0f));
            if (ModeBtn("Perm (EFI)", g_spooferModeEfi)) {
                g_spooferModeEfi = true;
                // Lazy-load EFI config on first switch
                if (!g_efiHwidConfigLoaded && !g_efiHwidOpRunning.load())
                    EfiHwidLoad();
            }
        }
    }

    ImGui::Dummy(ImVec2(0, SX(8.0f)));

    // ── Auto-resize helper ──
    auto FinalizeSpooferHeight = []() {
        g_neededWindowH = TITLE_BAR_H + ImGui::GetCursorPosY() + SX(16.0f);
    };

    // ═══════════════════════════════════════════════════════════════════════
    // EFI (permanent) mode panel
    // ═══════════════════════════════════════════════════════════════════════
    if (g_spooferModeEfi) {
        const bool opRunning = g_efiHwidOpRunning.load();

        // ── Loading spinner ──
        if (opRunning && !g_efiHwidConfigLoaded) {
            ImGui::Dummy(ImVec2(0, SX(20.0f)));
            float spinR = 18.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - spinR * 2) * 0.5f);
            DrawSpinner(spinR, 3.0f, ImGui::GetColorU32(COL_PURPLE));
            ImGui::Dummy(ImVec2(0, SX(8.0f)));
            CenterText("Loading EFI config from ESP...", g_fontBody, COL_TEXT_DIM);
            FinalizeSpooferHeight();
            return;
        }

        // ── Capture status banner ──
        {
            ImVec4 capColor = g_efiHwidCaptured ? COL_SUCCESS : COL_WARNING;
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                ImVec4(capColor.x, capColor.y, capColor.z, 0.07f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(SX(12.0f), SX(8.0f)));
            ImGui::BeginChild("##efi_cap_status", ImVec2(avail, 0),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar);

            if (g_fontBody) ImGui::PushFont(g_fontBody);
            ImGui::TextColored(COL_TEXT_DIM, "HWID Capture:");
            if (g_fontBody) ImGui::PopFont();
            ImGui::SameLine();
            if (g_efiHwidCaptured) {
                StatusPill("CAPTURED", COL_SUCCESS);
                ImGui::SameLine(0, SX(10.0f));
                if (g_fontSmall) ImGui::PushFont(g_fontSmall);
                ImGui::TextColored(COL_TEXT_DIM,
                    "Module has captured real HWID. Safe to write spoofs.");
                if (g_fontSmall) ImGui::PopFont();
            } else {
                StatusPill("NOT YET", COL_WARNING);
                ImGui::SameLine(0, SX(10.0f));
                if (g_fontSmall) ImGui::PushFont(g_fontSmall);
                ImGui::TextColored(COL_WARNING,
                    "Boot once with the Compatibility Module to capture HWID first.");
                if (g_fontSmall) ImGui::PopFont();
            }

            if (g_efiHwidApplyPending) {
                ImGui::Dummy(ImVec2(0, SX(2.0f)));
                if (g_fontSmall) ImGui::PushFont(g_fontSmall);
                ImGui::TextColored(COL_PURPLE_HOV,
                    "Spoof already staged — reboot to apply.");
                if (g_fontSmall) ImGui::PopFont();
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, SX(6.0f)));

        // ── Field editor panel ──
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(SX(10.0f), SX(10.0f)));
        ImGui::BeginChild("##efi_hwid_fields", ImVec2(avail, 0),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
            ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar);

        ImGui::PushItemWidth(-1);

        auto FieldRow = [&](const char* label, const char* hint,
                            char* buf, int bufSz) {
            if (g_fontSmall) ImGui::PushFont(g_fontSmall);
            ImGui::TextColored(COL_TEXT_DIM, "%s", label);
            if (g_fontSmall) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_FrameBg,
                ImVec4(0.08f, 0.08f, 0.11f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            if (opRunning) ImGui::BeginDisabled();
            ImGui::InputText(hint, buf, bufSz);
            if (opRunning) ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        };

        FieldRow("System UUID",        "##efi_uuid",   g_efiHwidUuid,   sizeof(g_efiHwidUuid));
        ImGui::Dummy(ImVec2(0, SX(4.0f)));
        FieldRow("System Serial",      "##efi_sysser", g_efiHwidSysSer, sizeof(g_efiHwidSysSer));
        ImGui::Dummy(ImVec2(0, SX(4.0f)));
        FieldRow("Baseboard Serial",   "##efi_bbser",  g_efiHwidBbSer,  sizeof(g_efiHwidBbSer));
        ImGui::Dummy(ImVec2(0, SX(4.0f)));
        FieldRow("Processor Serial",   "##efi_cpuser", g_efiHwidCpuSer, sizeof(g_efiHwidCpuSer));

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, SX(8.0f)));

        // ── Action buttons ──
        const float halfW = (avail - SX(8.0f)) * 0.5f;

        if (AccentButton("Load from Config", ImVec2(halfW, SX(36.0f)), opRunning)) {
            g_efiHwidConfigLoaded = false;
            EfiHwidLoad();
        }
        ImGui::SameLine(0, SX(8.0f));
        if (AccentButton("Apply to ESP", ImVec2(halfW, SX(36.0f)),
                         opRunning || !g_efiHwidCaptured)) {
            EfiHwidApply();
        }

        // ── Operation result / progress ──
        if (!g_efiHwidOpMsg.empty() || opRunning) {
            ImGui::Dummy(ImVec2(0, SX(6.0f)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(SX(10.0f), SX(6.0f)));
            ImGui::BeginChild("##efi_op_result", ImVec2(avail, 0),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar);
            if (opRunning) {
                float spinR = 8.0f;
                DrawSpinner(spinR, 2.0f, ImGui::GetColorU32(COL_PURPLE));
                ImGui::SameLine();
                ImGui::TextColored(COL_TEXT_DIM, "Working...");
            } else {
                ImVec4 col = g_efiHwidOpSuccess ? COL_SUCCESS : COL_WARNING;
                ImGui::TextColored(col, "%s", g_efiHwidOpMsg.c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        FinalizeSpooferHeight();
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Driver (temporary) mode — original spoofer UI
    // ═══════════════════════════════════════════════════════════════════════

    // ── Connection status panel ──
    const bool connected  = g_spooferConn == SpooferConnState::Connected;
    const bool connecting = g_spooferConn == SpooferConnState::Connecting;
    const bool errored    = g_spooferConn == SpooferConnState::Error;

    if (!connected && !connecting) {
        // Show connect button / error
        ImVec4 infoBg = errored
            ? ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.08f)
            : ImVec4(COL_PANEL.x, COL_PANEL.y, COL_PANEL.z, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, infoBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(10.0f), SX(10.0f)));
        ImGui::BeginChild("##spoof_conn", ImVec2(avail, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        if (errored) {
            CenterText("Driver not responding", g_fontBody, COL_DANGER);
            ImGui::Dummy(ImVec2(0, SX(4.0f)));
            CenterText("Load the driver first via any game product.", g_fontSmall, COL_TEXT_DIM);
        } else {
            CenterText("Not connected to driver", g_fontBody, COL_TEXT_DIM);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, SX(8.0f)));
        if (AccentButton(errored ? "Retry Connection" : "Connect to Driver",
                         ImVec2(avail, SX(38.0f)))) {
            SpooferConnect();
        }
        FinalizeSpooferHeight();
        return;
    }

    if (connecting) {
        ImGui::Dummy(ImVec2(0, SX(20.0f)));
        float spinR = 18.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - spinR * 2) * 0.5f);
        DrawSpinner(spinR, 3.0f, ImGui::GetColorU32(COL_PURPLE));
        ImGui::Dummy(ImVec2(0, SX(8.0f)));
        CenterText("Connecting to driver...", g_fontBody, COL_TEXT_DIM);
        FinalizeSpooferHeight();
        return;
    }

    // ── Spoof status badge ──
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(12.0f), SX(8.0f)));
        ImGui::BeginChild("##spoof_status_hdr", ImVec2(avail, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        // Vertically center "Status:" against the taller pill (pill = text + 4px
        // vertical padding on each side, so nudge the label down by 4).
        if (g_fontBody) ImGui::PushFont(g_fontBody);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::TextColored(COL_TEXT_DIM, "Status:");
        if (g_fontBody) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);
        if (g_spooferStatus.spoofed) {
            StatusPill("SPOOFED", COL_WARNING);
        } else {
            StatusPill("PRISTINE", COL_SUCCESS);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, SX(6.0f)));

    // ── HWID data panel ──
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(10.0f), SX(8.0f)));
        ImGui::BeginChild("##hwid_data", ImVec2(avail, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginGroup();

        // Format UUID as hex string for display
        char uuidStr[40] = {};
        const auto& u = g_spooferStatus.smbios_uuid;
        snprintf(uuidStr, sizeof(uuidStr),
                 "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                 u[0],u[1],u[2],u[3], u[4],u[5], u[6],u[7],
                 u[8],u[9], u[10],u[11],u[12],u[13],u[14],u[15]);

        SpooferRow("SMBIOS UUID",      uuidStr);
        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        SpooferRow("System Serial",    g_spooferStatus.system_serial);
        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        SpooferRow("Baseboard Serial", g_spooferStatus.baseboard_serial);
        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        SpooferRow("Machine GUID",     g_spooferStatus.machine_guid);
        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        SpooferRow("MAC Address",      g_spooferStatus.mac_address);
        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        SpooferRow("Volume Serial",    g_spooferStatus.volume_serial);

        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, SX(8.0f)));

    // ── Operation buttons ──
    const bool opLocked = g_spooferOpRunning.load();
    const float btnW    = (avail - SX(8.0f) * 3.0f) / 4.0f;
    const float btnH    = SX(38.0f);

    // Save Original
    if (AccentButton("Save Original", ImVec2(btnW, btnH), opLocked)) {
        SpooferRunOp("Save", []() { return LoaderIpc::HwidSave(); });
    }
    ImGui::SameLine(0, SX(8.0f));

    // Spoof All
    ImGui::PushStyleColor(ImGuiCol_Button,
        g_spooferStatus.spoofed
            ? ImVec4(0.18f, 0.18f, 0.22f, 1.00f)
            : ImVec4(0.659f, 0.333f, 0.969f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.753f, 0.516f, 0.988f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.541f, 0.235f, 0.831f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    if (opLocked) ImGui::BeginDisabled();
    if (ImGui::Button("Spoof All##spoofbtn", ImVec2(btnW, btnH))) {
        SpooferRunOp("Spoof", []() {
            return LoaderIpc::HwidSpoof(0xFFFFFFFF, 0);
        });
    }
    if (opLocked) ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, SX(8.0f));

    // Restore
    if (AccentButton("Restore", ImVec2(btnW, btnH),
                     opLocked || !g_spooferStatus.spoofed)) {
        SpooferRunOp("Restore", []() { return LoaderIpc::HwidRestore(); });
    }
    ImGui::SameLine(0, SX(8.0f));

    // Reroll
    if (AccentButton("Reroll", ImVec2(btnW, btnH),
                     opLocked || !g_spooferStatus.spoofed)) {
        SpooferRunOp("Reroll", []() {
            return LoaderIpc::HwidReroll(0xFFFFFFFF);
        });
    }

    // ── Operation result ──
    if (!g_spooferOpMsg.empty() || opLocked) {
        ImGui::Dummy(ImVec2(0, SX(6.0f)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(10.0f), SX(6.0f)));
        ImGui::BeginChild("##spoof_op_result", ImVec2(avail, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        if (opLocked) {
            float spinR = 8.0f;
            DrawSpinner(spinR, 2.0f, ImGui::GetColorU32(COL_PURPLE));
            ImGui::SameLine();
            ImGui::TextColored(COL_TEXT_DIM, "Working...");
        } else {
            ImVec4 col = g_spooferOpSuccess ? COL_SUCCESS : COL_DANGER;
            ImGui::TextColored(col, "%s", g_spooferOpMsg.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    FinalizeSpooferHeight();
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings screen
// ─────────────────────────────────────────────────────────────────────────────

// HSV → RGB helper for the color-tile validation hue cycle.
static void HsvToRgbBytes(float h, float s, float v,
                          uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float fr = 0, fg = 0, fb = 0;
    if      (h <  60.0f) { fr = c; fg = x; fb = 0; }
    else if (h < 120.0f) { fr = x; fg = c; fb = 0; }
    else if (h < 180.0f) { fr = 0; fg = c; fb = x; }
    else if (h < 240.0f) { fr = 0; fg = x; fb = c; }
    else if (h < 300.0f) { fr = x; fg = 0; fb = c; }
    else                 { fr = c; fg = 0; fb = x; }
    r = (uint8_t)((fr + m) * 255.0f);
    g = (uint8_t)((fg + m) * 255.0f);
    b = (uint8_t)((fb + m) * 255.0f);
}

// Per-frame color-tile R/W validation step. Allocates the test buffer on
// first call, then writes the next hue into it via the driver and reads it
// back, updating g_colorVal. Cheap when the driver is up; a no-op when not.
static void RunColorValidationStep() {
    // Ensure the IPC channel is live. Init() is idempotent / cheap when
    // already armed, and re-arms the magic if a prior Release() burned it
    // (e.g. after a product injection handoff).
    LoaderIpc::Init();

    if (!g_colorVal.initialized) {
        void* alloc = VirtualAlloc(nullptr, 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (alloc) {
            g_colorVal.buf_addr = (uint64_t)alloc;
            memset(alloc, 0, 4);
        }
        g_colorVal.initialized = true;
    }
    if (!g_colorVal.buf_addr) return;

    if (g_colorVal.paused) {
        return;
    }

    float hue;
    {
        DWORD tick = GetTickCount();
        float cycle = fmodf((float)tick / 1000.0f, g_colorVal.cycle_seconds)
                    / g_colorVal.cycle_seconds;
        hue = cycle * 360.0f;
    }

    uint8_t tr, tg, tb;
    HsvToRgbBytes(hue, 1.0f, 1.0f, tr, tg, tb);
    uint8_t target[4]   = { tr, tg, tb, 0xFF };
    uint8_t readback[4] = { 0, 0, 0, 0 };

    g_colorVal.write_rgb[0] = tr;
    g_colorVal.write_rgb[1] = tg;
    g_colorVal.write_rgb[2] = tb;

    g_colorVal.last_write_ok =
        LoaderIpc::MemWrite(g_colorVal.buf_addr, target, 4, /*use_cr3*/ false, 250);
    if (g_colorVal.last_write_ok) g_colorVal.write_ok_count++;
    else                          g_colorVal.write_fail_count++;

    g_colorVal.last_read_ok =
        LoaderIpc::MemRead(g_colorVal.buf_addr, readback, 4, /*use_cr3*/ false, 250);
    if (g_colorVal.last_read_ok) {
        g_colorVal.read_ok_count++;
        g_colorVal.read_rgb[0] = readback[0];
        g_colorVal.read_rgb[1] = readback[1];
        g_colorVal.read_rgb[2] = readback[2];
        g_colorVal.last_match  = (readback[0] == tr &&
                                  readback[1] == tg &&
                                  readback[2] == tb);
        if (g_colorVal.last_match) g_colorVal.match_count++;
        else                       g_colorVal.mismatch_count++;
    } else {
        g_colorVal.read_fail_count++;
        g_colorVal.last_match = false;
    }
}

static void ResetColorValidationCounters() {
    g_colorVal.write_ok_count   = 0;
    g_colorVal.write_fail_count = 0;
    g_colorVal.read_ok_count    = 0;
    g_colorVal.read_fail_count  = 0;
    g_colorVal.match_count      = 0;
    g_colorVal.mismatch_count   = 0;
}

static void DrawSettingsScreen() {
    float availTotal = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Same auto-resize pattern the spoofer + products screens use: measure the
    // cursor at the end of layout and write g_neededWindowH so the host window
    // shrinks/grows to fit. No scrollbars anywhere.
    auto FinalizeSettingsHeight = []() {
        g_neededWindowH = TITLE_BAR_H + ImGui::GetCursorPosY() + SX(16.0f);
    };

    // Auto-detect driver state when the settings screen opens. Without this,
    // g_driverLoadState stays Idle (and the color test shows "NO DRIVER") even
    // when the driver is already loaded — e.g. if the user navigated here before
    // ever visiting the spoofer section. We kick off a one-shot background ping
    // each time the state is Idle; SpooferConnect() does the same thing.
    {
        static std::atomic<bool> s_probeInFlight { false };
        if (g_driverLoadState.load() == DriverLoadState::Idle &&
            !s_probeInFlight.exchange(true))
        {
            std::thread([]() {
                LoaderIpc::Init();
                if (LoaderIpc::Ping(2000)) {
                    g_driverLoadState.store(DriverLoadState::Ready);
                }
                s_probeInFlight.store(false);
            }).detach();
        }
    }

    // ── Header row ──
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.14f));
        ImGui::PushStyleColor(ImGuiCol_Text,          COL_TEXT_DIM);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (ImGui::Button("< Back", ImVec2(SX(80.0f), SX(30.0f)))) {
            GotoState(AppState::Products);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }
    ImGui::SameLine();
    
    // Header title with hero styling
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImGui::TextColored(COL_TEXT, "Configuration & Diagnostics");
    if (g_fontTitle) ImGui::PopFont();
    
    // Subtle separator line
    ImVec2 sepMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(availTotal, SX(12.0f)));
    dl->AddLine(sepMin, ImVec2(sepMin.x + availTotal, sepMin.y), ImGui::GetColorU32(ImVec4(1,1,1,0.05f)), 1.5f);

    ImGui::Dummy(ImVec2(0, SX(4.0f)));

    // ── Main Dashboard Layout (2 Columns) ──
    const float colGap = SX(16.0f);
    const float colWidth = (availTotal - colGap) * 0.5f;

    ImGui::BeginGroup(); // Left Column (Color-tile R/W diagnostic)

    if (g_fontBody) ImGui::PushFont(g_fontBody);
    ImGui::TextColored(COL_PURPLE_HOV, "Driver Validation");
    if (g_fontBody) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, SX(4.0f)));

    // Drive one R/W round-trip per frame so the tiles animate.
    if (g_driverLoadState.load() == DriverLoadState::Ready) {
        RunColorValidationStep();
    }

    // Tile panel: two stacked color squares (WRITTEN / READ BACK) with status.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL_ALT);
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(1,1,1,0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(12.0f), SX(12.0f)));
    ImGui::BeginChild("##colorval", ImVec2(colWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    {
        const float innerW  = ImGui::GetContentRegionAvail().x;
        const float tileGap = SX(10.0f);
        const float tileW   = (innerW - tileGap) * 0.5f;
        const float tileH   = SX(72.0f);

        ImDrawList* cdl = ImGui::GetWindowDrawList();
        const uint8_t* w = g_colorVal.write_rgb;
        const uint8_t* r = g_colorVal.read_rgb;

        // WRITTEN tile
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = ImVec2(p0.x + tileW, p0.y + tileH);
            cdl->AddRectFilled(p0, p1, IM_COL32(w[0], w[1], w[2], 255), 6.0f);
            cdl->AddRect      (p0, p1, IM_COL32(255,255,255, 60), 6.0f, 0, 1.0f);
            ImGui::Dummy(ImVec2(tileW, tileH));
        }
        ImGui::SameLine(0, tileGap);
        // READ tile
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = ImVec2(p0.x + tileW, p0.y + tileH);
            cdl->AddRectFilled(p0, p1, IM_COL32(r[0], r[1], r[2], 255), 6.0f);
            cdl->AddRect      (p0, p1, IM_COL32(255,255,255, 60), 6.0f, 0, 1.0f);
            ImGui::Dummy(ImVec2(tileW, tileH));
        }

        ImGui::Dummy(ImVec2(0, SX(2.0f)));
        if (g_fontSmall) ImGui::PushFont(g_fontSmall);
        ImGui::TextColored(COL_TEXT_DIM, "WRITTEN");
        ImGui::SameLine(tileW + tileGap + SX(2.0f));
        ImGui::TextColored(COL_TEXT_DIM, "READ BACK");
        if (g_fontSmall) ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, SX(8.0f)));

        if (g_fontSmall) ImGui::PushFont(g_fontSmall);
        char hex[64];

        // Pre-calculate status pill so it can be right-aligned on the Sent row.
        const bool driverReady = (g_driverLoadState.load() == DriverLoadState::Ready);
        const char* pillText  = !driverReady ? "NO DRIVER"
                              : g_colorVal.last_match ? "MATCH" : "MISMATCH";
        ImVec4      pillColor = !driverReady ? COL_DANGER
                              : g_colorVal.last_match ? COL_SUCCESS : COL_WARNING;
        const float pillPadX = 10.0f, pillPadY = 4.0f;
        float pillW = ImGui::CalcTextSize(pillText).x + pillPadX * 2.0f;
        float pillH = ImGui::CalcTextSize(pillText).y + pillPadY * 2.0f;

        // "Sent" row — pill right-aligned on the same line.
        snprintf(hex, sizeof(hex), "Sent  #%02X%02X%02X", w[0], w[1], w[2]);
        float rowY = ImGui::GetCursorPosY();
        ImGui::TextColored(COL_TEXT_DIM, "%s", hex);
        float nextLineY = ImGui::GetCursorPosY();

        float textH = ImGui::GetTextLineHeight();
        ImGui::SetCursorPos(ImVec2(innerW - pillW, rowY + (textH - pillH) * 0.5f));
        StatusPill(pillText, pillColor);

        // Restore cursor to the "Got" line.
        ImGui::SetCursorPosY(nextLineY);
        snprintf(hex, sizeof(hex), "Got   #%02X%02X%02X", r[0], r[1], r[2]);
        ImGui::TextColored(COL_TEXT_DIM, "%s", hex);

        ImGui::Dummy(ImVec2(0, SX(4.0f)));
        ImGui::TextColored(COL_TEXT_FAINT,
            "Writes: %u OK / %u FAIL",
            g_colorVal.write_ok_count, g_colorVal.write_fail_count);
        ImGui::TextColored(COL_TEXT_FAINT,
            "Reads:  %u OK / %u FAIL",
            g_colorVal.read_ok_count, g_colorVal.read_fail_count);
        ImGui::TextColored(COL_TEXT_FAINT,
            "Match:  %u  /  Mismatch: %u",
            g_colorVal.match_count, g_colorVal.mismatch_count);
        if (g_fontSmall) ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0, SX(8.0f)));

    if (GhostButton(g_colorVal.paused ? "Resume Animation" : "Pause Animation",
                    ImVec2(colWidth, SX(30.0f)))) {
        g_colorVal.paused = !g_colorVal.paused;
    }
    ImGui::Dummy(ImVec2(0, SX(4.0f)));
    if (GhostButton("Reset Counters", ImVec2(colWidth, SX(30.0f)))) {
        ResetColorValidationCounters();
    }
    ImGui::Dummy(ImVec2(0, SX(4.0f)));
    // ── Kernel driver bring-up (manual) ──
    {
        DriverLoadState dls = g_driverLoadState.load();
        const bool loading = (dls == DriverLoadState::Loading);
        const char* btnLabel =
            loading                       ? "Loading Driver..."
          : dls == DriverLoadState::Ready ? "Reload Driver"
          :                                 "Load Driver";
        if (AccentButton(btnLabel, ImVec2(colWidth, SX(38.0f)), loading)) {
            LoadDriverAsync();
        }
        std::string msg;
        {
            std::lock_guard<std::mutex> lk(g_driverLoadMsgMtx);
            msg = g_driverLoadMsg;
        }
        if (!msg.empty()) {
            ImGui::Dummy(ImVec2(0, SX(4.0f)));
            if (g_fontSmall) ImGui::PushFont(g_fontSmall);
            ImVec4 col =
                dls == DriverLoadState::Failed ? COL_DANGER
              : dls == DriverLoadState::Ready  ? COL_SUCCESS
              :                                  COL_TEXT_DIM;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colWidth);
            ImGui::TextColored(col, "%s", msg.c_str());
            ImGui::PopTextWrapPos();
            if (g_fontSmall) ImGui::PopFont();
        }
    }

    ImGui::EndGroup(); // End Left Column

    ImGui::SameLine(0, colGap);

    ImGui::BeginGroup(); // Right Column (EFI / DCU)

    if (g_fontBody) ImGui::PushFont(g_fontBody);
    ImGui::TextColored(COL_PURPLE_HOV, "System Protection");
    if (g_fontBody) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, SX(4.0f)));

    DcuReadinessResult dcuState = DcuQueryReadiness();
    bool dcuInstalled = (dcuState.state == DcuState::Ready || dcuState.state == DcuState::Failed || dcuState.state == DcuState::PendingReboot);

    if (!g_efiStateLoaded) {
        g_efiStateLoaded = true;
        DcuEfiSettings s;
        if (DcuReadEfiConfig(s)) {
            g_efiDseMethod = s.dseBypassMethod;
            g_efiWaitForKeyPress = s.waitForKeyPress;
        }
    }

    // ── EFI Configuration Panel ──
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL_ALT);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(SX(10.0f), SX(10.0f)));
    ImGui::BeginChild("##efi_config", ImVec2(colWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    {
        if (!dcuInstalled) {
            ImGui::TextColored(COL_TEXT_DIM, "Module not active.");
            ImGui::Dummy(ImVec2(0, SX(4.0f)));
            if (g_fontSmall) ImGui::PushFont(g_fontSmall);
            ImGui::TextColored(COL_TEXT_FAINT, "Install the Compatibility Module\nto unlock EFI configuration.");
            if (g_fontSmall) ImGui::PopFont();
        } else {
            ImGui::TextColored(COL_TEXT, "DSE Bypass Method");
            ImGui::Dummy(ImVec2(0, SX(4.0f)));
            ImGui::RadioButton("Automatic (recommended)", &g_efiDseMethod, 3);
            ImGui::RadioButton("SetVariable Hook", &g_efiDseMethod, 2);
            ImGui::RadioButton("Boot-time Patch", &g_efiDseMethod, 1);

            ImGui::Dummy(ImVec2(0, SX(8.0f)));
            ImGui::Checkbox("Print Debug on Boot", &g_efiWaitForKeyPress);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    static bool showRestoreConfirm = false;
    static bool isRestoring = false;

    if (showRestoreConfirm) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.08f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("##restore_confirm", ImVec2(colWidth, SX(148.0f)),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::Dummy(ImVec2(0, SX(6.0f)));
        CenterText("Remove EFI bootkit entirely?", g_fontBody, COL_DANGER);
        ImGui::Dummy(ImVec2(0, SX(8.0f)));
        float btnW = (colWidth - SX(30.0f)) * 0.5f;
        ImGui::SetCursorPosX(SX(10.0f));
        if (GhostButton("Cancel", ImVec2(btnW, SX(30.0f)))) {
            showRestoreConfirm = false;
        }
        ImGui::SameLine(0, SX(10.0f));
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_DANGER);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(COL_DANGER.x * 0.8f, COL_DANGER.y * 0.8f, COL_DANGER.z * 0.8f, 1.0f));
        if (ImGui::Button("Confirm", ImVec2(btnW, SX(30.0f)))) {
            showRestoreConfirm = false;
            isRestoring = true;
            std::thread([]() {
                DcuUninstall();
                isRestoring = false;
            }).detach();
            GotoState(AppState::Products);
        }
        ImGui::PopStyleColor(3);
        
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else {
        ImGui::Dummy(ImVec2(0, SX(8.0f)));

        if (dcuInstalled) {
            if (AccentButton("Apply Configuration to ESP", ImVec2(colWidth, SX(38.0f)))) {
                DcuEfiSettings s;
                s.dseBypassMethod = g_efiDseMethod;
                s.waitForKeyPress = g_efiWaitForKeyPress;
                DcuWriteEfiConfig(s);
                ShowPopup("Success", "EFI configuration applied. It will take effect on your next restart.", COL_SUCCESS);
            }
        } else {
            ImGui::Dummy(ImVec2(0, SX(38.0f))); // Placeholder
        }

        // ── Module State & Installation ──
        ImGui::Dummy(ImVec2(0, SX(16.0f)));

        auto queueDcuWizardInstall = []() {
            // Always carry current auth context into DCU install flow.
            g_dcuProductId = !g_authInfo.subscriptions.empty()
                ? g_authInfo.subscriptions[0].productId
                : OBF_S("SHARED");
            g_dcuToken     = g_authInfo.token;
            g_dcuHwid      = g_hwid;

            g_dcuWizardDone       = false;
            g_dcuWizardActive     = false;
            g_dcuInstallResult    = false;
            g_dcuUserWantsYes     = false;
            g_dcuUserWantsNo      = false;
            g_dcuUserWantsRestart = false;
            g_dcuProgress         = 0.0f;
            g_dcuCurrentStep      = DcuStep::Begin;
            g_dcuStatusText       = "";

            // Clear any stale HWID mismatch state from a prior install attempt
            // so the panel starts clean each time the wizard is (re-)opened.
            g_hwidMismatch       = false;
            g_hwidResetSubmitted = false;
            g_hwidResetInFlight  = false;
            g_hwidResetDeclined  = false;
            SetHwidResetMessage("");

            GotoState(AppState::DcuWizard);
        };

        if (isRestoring) {
            GhostButton("Restoring System...", ImVec2(colWidth, SX(38.0f)));
        } else if (!dcuInstalled) {
            // Module not installed — offer install instead of restore
            if (AccentButton("Install Compatibility Module", ImVec2(colWidth, SX(38.0f)))) {
                queueDcuWizardInstall();
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(COL_DANGER.x, COL_DANGER.y, COL_DANGER.z, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Text,          COL_DANGER);
            if (ImGui::Button("Restore System (Uninstall)", ImVec2(colWidth, SX(38.0f)))) {
                showRestoreConfirm = true;
            }
            ImGui::PopStyleColor(4);
        }

        if (dcuState.state == DcuState::Ready || dcuState.state == DcuState::Failed) {
            ImGui::Dummy(ImVec2(0, SX(8.0f)));
            if (GhostButton("Reinstall Module", ImVec2(colWidth, SX(38.0f)))) {
                queueDcuWizardInstall();
            }
        }
    }

    ImGui::EndGroup(); // End Right Column

    FinalizeSettingsHeight();
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// DCU Wizard screen
// ─────────────────────────────────────────────────────────────────────────────

// Progress callback — called from the worker thread.
static void DcuProgressCallback(DcuStep step, const char* status, float progress) {
    g_dcuCurrentStep = step;
    g_dcuStatusText  = status ? status : "";
    g_dcuProgress    = progress;
}

// Launches DCU install on a background thread.
static void LaunchDcuInstall() {
    if (g_dcuWizardActive.exchange(true)) return;
    g_dcuWizardDone  = false;
    g_dcuInstallResult = false;
    g_dcuProgress    = 0.0f;
    g_dcuCurrentStep = DcuStep::Begin;
    g_dcuStatusText  = "Starting DCU installation...";

    std::string productId = g_dcuProductId;
    std::string token     = g_dcuToken;
    std::string hwid      = g_dcuHwid;

    std::thread([productId, token, hwid]() {
        DcuState result = DcuInstall(productId, token, hwid, DcuProgressCallback);

        if (result == DcuState::PendingReboot) {
            g_dcuInstallResult = true;
            g_dcuStatusText    = "DCU installed successfully! Restart to complete setup.";
            g_dcuProgress      = 1.0f;
        } else {
            g_dcuInstallResult = false;
            if (g_dcuStatusText.empty())
                g_dcuStatusText = "DCU installation failed.";
                
            Api::ReportLoaderEvent(OBF_S("load_failed"), productId, hwid, g_dcuStatusText, token);

            if (Api::LastStreamWasHwidMismatch()) {
                // Surface the mismatch to the UI; the user chooses whether to
                // submit a reset request via the HWID mismatch panel.
                g_hwidMismatch = true;
                Api::ReportLoaderEvent(OBF_S("hwid_mismatch"), productId, hwid, "DCU stream blocked", token);
            }
        }

        g_dcuWizardDone  = true;
        g_dcuWizardActive = false;
    }).detach();
}

// Returns a human-readable label for each install step.
static const char* DcuStepLabel(DcuStep s) {
    switch (s) {
        case DcuStep::Begin:             return "Starting...";
        case DcuStep::DownloadAssets:    return "Downloading EfiGuard package";
        case DcuStep::CheckHvci:         return "Checking Memory Integrity (HVCI)";
        case DcuStep::StageEfiPayloads:  return "Staging EFI payloads to ESP";
        case DcuStep::RegisterBootEntry: return "Registering UEFI boot entry";
        case DcuStep::SchedulePostBoot:  return "Scheduling EfiDSEFix post-boot";
        case DcuStep::Done:              return "Complete";
        case DcuStep::Failed:            return "Failed";
    }
    return "";
}

static void DrawDcuWizardScreen() {
    float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(0, 16));

    // ── Hero icon ────────────────────────────────────────────────────────
    const float iconSize = 48.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 icon = ImVec2(pos.x + (avail - iconSize) * 0.5f, pos.y);
    dl->AddRectFilled(icon, ImVec2(icon.x + iconSize, icon.y + iconSize),
                      ImGui::GetColorU32(COL_PURPLE), 12.0f);
    if (g_fontHero) ImGui::PushFont(g_fontHero);
    ImVec2 sSz = ImGui::CalcTextSize("D");
    dl->AddText(ImVec2(icon.x + (iconSize - sSz.x) * 0.5f,
                       icon.y + (iconSize - sSz.y) * 0.5f - 2),
                ImGui::GetColorU32(COL_TEXT), "D");
    if (g_fontHero) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, iconSize + 12));

    // ── Title ────────────────────────────────────────────────────────────
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    const char* title = g_dcuWizardDone && g_dcuInstallResult
        ? "Installation Complete"
        : (g_dcuWizardDone && g_hwidMismatch)
        ? "Hardware Mismatch"
        : g_dcuWizardDone
        ? "Installation Failed"
        : "Driver Compatibility Utility";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - tw) * 0.5f);
    ImGui::TextColored(COL_TEXT, "%s", title);
    if (g_fontTitle) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 6));

    const bool installFinished = g_dcuWizardDone.load();

    if (!g_dcuWizardActive && !installFinished) {
        // ── Initial prompt: Yes / No ─────────────────────────────────────
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
            ImGui::BeginChild("##dcu_intro", ImVec2(avail, SX(90.0f)),
                              ImGuiChildFlags_Borders);
            ImGui::Dummy(ImVec2(0, SX(8.0f)));
            if (g_fontBody) ImGui::PushFont(g_fontBody);
            const char* msg =
                "This tool installs EfiGuard to disable PatchGuard and DSE, "
                "enabling unsigned kernel driver mapping. A system restart "
                "is required. Install now?";
            float wrapW = avail - ImGui::GetStyle().WindowPadding.x * 2.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
            ImGui::TextColored(COL_TEXT_DIM, "%s", msg);
            ImGui::PopTextWrapPos();
            if (g_fontBody) ImGui::PopFont();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, SX(10.0f)));

        // Yes / No buttons side by side
        float btnW = (avail - SX(10.0f)) * 0.5f;

        // "Yes, Install" — purple accent
        if (AccentButton("Yes, Install", ImVec2(btnW, SX(40.0f)))) {
            g_dcuUserWantsYes = true;
            g_dcuStatusText   = "Starting installation...";
            LaunchDcuInstall();
        }

        ImGui::SameLine(0, SX(10.0f));

        // "No, Cancel" — ghost button
        if (GhostButton("No, Cancel", ImVec2(btnW, SX(40.0f)))) {
            g_dcuUserWantsNo = true;
            g_dcuWizardDone  = true;
            g_dcuInstallResult = false;
            g_dcuStatusText  = "DCU installation cancelled.";
        }
    }

    if (g_dcuWizardActive || installFinished) {
        // ── Progress panel ───────────────────────────────────────────────
        ImGui::Dummy(ImVec2(0, SX(6.0f)));

        // Progress bar
        {
            float prog = g_dcuProgress;
            ImVec2 barMin = ImGui::GetCursorScreenPos();
            ImVec2 barMax = ImVec2(barMin.x + avail, barMin.y + SX(8.0f));

            // Background track
            dl->AddRectFilled(barMin, barMax,
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f)), 4.0f);
            // Fill
            if (prog > 0.0f) {
                ImVec2 fillMax = ImVec2(barMin.x + avail * prog, barMax.y);
                dl->AddRectFilled(barMin, fillMax,
                    ImGui::GetColorU32(prog >= 1.0f ? COL_SUCCESS : COL_PURPLE), 4.0f);
            }
            ImGui::Dummy(ImVec2(avail, SX(8.0f)));
        }

        ImGui::Dummy(ImVec2(0, SX(6.0f)));

        // Current step status
        DcuStep curStep = g_dcuCurrentStep;
        const int totalSteps = 6;
        for (int i = 0; i < totalSteps; ++i) {
            DcuStep s = static_cast<DcuStep>(i + 1); // steps 1-6
            bool active  = (s == curStep) && !installFinished;
            bool done    = (int)s < (int)curStep || (installFinished && g_dcuInstallResult);
            bool failed  = (s == curStep) && installFinished && !g_dcuInstallResult;

            ImVec4 col = done   ? COL_SUCCESS
                       : failed ? COL_DANGER
                       : active ? COL_PURPLE_HOV
                       : COL_TEXT_FAINT;

            ImGui::PushStyleColor(ImGuiCol_Text, col);

            // Icon: checkmark, spinner, or bullet
            if (done) {
                ImGui::TextColored(col, "[✓]");
            } else if (active) {
                float spinR = 6.0f;
                DrawSpinner(spinR, 2.0f, ImGui::GetColorU32(col));
                ImGui::SameLine();
                ImGui::TextColored(col, " ");
            } else if (failed) {
                ImGui::TextColored(col, "[✗]");
            } else {
                ImGui::TextColored(col, "[ ]");
            }

            ImGui::SameLine();
            ImGui::TextColored(col, "%s", DcuStepLabel(s));

            // If this is the current active step, show detail text
            if (active && !g_dcuStatusText.empty()) {
                ImGui::SameLine();
                if (g_fontSmall) ImGui::PushFont(g_fontSmall);
                ImGui::TextColored(COL_TEXT_DIM, "— %s", g_dcuStatusText.c_str());
                if (g_fontSmall) ImGui::PopFont();
            }

            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, SX(3.0f)));
        }

        // ── Result actions ───────────────────────────────────────────────
        if (installFinished) {
            ImGui::Dummy(ImVec2(0, SX(10.0f)));

            if (g_dcuInstallResult) {
                // Success: ask to restart
                if (g_fontBody) ImGui::PushFont(g_fontBody);
                const char* restartMsg =
                    "EfiGuard will load on next boot and disable DSE.\n"
                    "Restart now to complete setup.";
                float wrapW = avail - ImGui::GetStyle().WindowPadding.x * 2.0f;
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
                ImGui::TextColored(COL_TEXT_DIM, "%s", restartMsg);
                ImGui::PopTextWrapPos();
                if (g_fontBody) ImGui::PopFont();

                ImGui::Dummy(ImVec2(0, SX(8.0f)));

                float btnW = (avail - SX(10.0f)) * 0.5f;

                if (AccentButton("Restart Now", ImVec2(btnW, SX(40.0f)))) {
                    g_dcuUserWantsRestart = true;
                    // Initiate shutdown
                    HANDLE hToken;
                    TOKEN_PRIVILEGES tkp;
                    if (::OpenProcessToken(::GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
                    {
                        ::LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege",
                            &tkp.Privileges[0].Luid);
                        tkp.PrivilegeCount = 1;
                        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                        ::AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, 0);
                        ::CloseHandle(hToken);
                    }
                    ::ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG,
                        SHTDN_REASON_MAJOR_APPLICATION);
                }

                ImGui::SameLine(0, SX(10.0f));

                if (GhostButton("Restart Later", ImVec2(btnW, SX(40.0f)))) {
                    // Go back to products — user will reboot manually
                    GotoState(AppState::Products);
                }
            } else {
                // Failure: show generic error only for non-HWID failures
                if (!g_dcuStatusText.empty() && !g_hwidMismatch) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
                    ImGui::BeginChild("##dcu_error", ImVec2(avail, SX(60.0f)),
                                      ImGuiChildFlags_Borders);
                    if (g_fontBody) ImGui::PushFont(g_fontBody);
                    float wrapW = avail - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
                    ImGui::TextColored(COL_DANGER, "%s", g_dcuStatusText.c_str());
                    ImGui::PopTextWrapPos();
                    if (g_fontBody) ImGui::PopFont();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                // HWID mismatch panel — shown when the DCU asset stream was
                // blocked because this machine's HWID doesn't match the account.
                if (g_hwidMismatch) {
                    ImGui::Dummy(ImVec2(0, SX(8.0f)));

                    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                          ImVec4(COL_WARNING.x, COL_WARNING.y, COL_WARNING.z, 0.08f));
                    ImGui::PushStyleColor(ImGuiCol_Border,
                                          ImVec4(COL_WARNING.x, COL_WARNING.y, COL_WARNING.z, 0.45f));
                    ImGui::BeginChild("##dcu_hwidpanel", ImVec2(avail, 0),
                                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

                    if (g_fontBody) ImGui::PushFont(g_fontBody);
                    ImGui::TextColored(COL_WARNING, "Bound to a different machine");
                    if (g_fontBody) ImGui::PopFont();

                    ImGui::TextWrapped(
                        "If you changed hardware or this is a new PC, request a HWID reset. "
                        "An admin will review and approve or deny it. "
                        "If this isn't your account, you can decline and close the loader.");

                    ImGui::Dummy(ImVec2(0, 4));

                    if (g_hwidResetSubmitted) {
                        bool inFlight = g_hwidResetInFlight.load();
                        std::string msg = GetHwidResetMessage();
                        if (inFlight || msg.empty()) {
                            ImGui::TextColored(COL_TEXT_DIM, "Submitting request...");
                        } else {
                            ImGui::TextWrapped("%s", msg.c_str());
                        }
                    } else if (g_hwidResetDeclined) {
                        ImGui::TextColored(COL_TEXT_DIM,
                            "No reset request sent. You can go back to the product list.");
                    } else {
                        float fullW = ImGui::GetContentRegionAvail().x;
                        float gap   = 8.0f;
                        float halfW = (fullW - gap) * 0.5f;

                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.40f, 0.05f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.50f, 0.10f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.30f, 0.03f, 1.00f));
                        if (ImGui::Button("Request HWID Reset", ImVec2(halfW, 36))) {
                            g_hwidResetSubmitted = true;
                            g_hwidResetInFlight  = true;
                            std::string tok = g_authInfo.token;
                            std::string hw  = g_hwid;
                            std::thread([tok, hw]() {
                                Hwid::HardwareDetails details = Hwid::GetHardwareDetails();
                                Api::HwidResetResponse r = Api::SubmitHwidResetRequest(tok, hw, details);
                                SetHwidResetMessage(r.message);
                                g_hwidResetInFlight = false;
                            }).detach();
                        }
                        ImGui::PopStyleColor(3);

                        ImGui::SameLine(0.0f, gap);

                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.20f, 0.24f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.33f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.16f, 0.19f, 1.00f));
                        if (ImGui::Button("Don't Send Request", ImVec2(halfW, 36))) {
                            g_hwidResetDeclined = true;
                        }
                        ImGui::PopStyleColor(3);

                        ImGui::PopStyleVar();
                    }

                    ImGui::EndChild();
                    ImGui::PopStyleColor(2);
                }

                ImGui::Dummy(ImVec2(0, SX(8.0f)));

                // Retry is only useful for non-HWID failures. For HWID mismatch,
                // only offer "Back" — retrying the install won't succeed until
                // the admin approves a HWID reset.
                float btnW = g_hwidMismatch
                    ? avail
                    : (avail - SX(10.0f)) * 0.5f;

                if (!g_hwidMismatch) {
                    if (AccentButton("Retry", ImVec2(btnW, SX(36.0f)))) {
                        // Reset wizard + HWID state so the panel starts clean
                        g_dcuWizardDone      = false;
                        g_dcuWizardActive    = false;
                        g_dcuUserWantsYes    = false;
                        g_dcuUserWantsNo     = false;
                        g_hwidMismatch       = false;
                        g_hwidResetSubmitted = false;
                        g_hwidResetInFlight  = false;
                        g_hwidResetDeclined  = false;
                        SetHwidResetMessage("");
                    }
                    ImGui::SameLine(0, SX(10.0f));
                }
                if (GhostButton("Back", ImVec2(btnW, SX(36.0f)))) {
                    GotoState(AppState::Products);
                }
            }
        }
    }

    // Allow the host window to grow when the HWID panel is added — same
    // pattern as DrawInjectingScreen / DrawProductScreen.
    g_neededWindowH = TITLE_BAR_H + ImGui::GetCursorPosY() + SX(16.0f);
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
    // Height is calculated from the wrapped text size so the panel is
    // compact for short messages and only expands when text wraps.
    // Click-to-copy is only active when there is an error to report.
    {
        bool isError = g_injectionFailed.load() || g_hwidMismatch;
        std::string statusMsg = GetStatus();

        const ImGuiStyle& style = ImGui::GetStyle();
        float innerW  = avail - style.WindowPadding.x * 2.0f;
        float textH   = statusMsg.empty()
                          ? ImGui::GetTextLineHeight()
                          : ImGui::CalcTextSize(statusMsg.c_str(),
                                                nullptr, false, innerW).y;
        float childH  = textH + style.WindowPadding.y * 2.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
        ImGui::BeginChild("##status", ImVec2(avail, childH),
                          ImGuiChildFlags_Borders);
        {
            ImVec2 panelMin = ImGui::GetCursorScreenPos();
            float  wrapW    = ImGui::GetContentRegionAvail().x;

            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
            ImGui::TextUnformatted(statusMsg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            // Invisible hit-region for click-to-copy — only when an error
            // is shown so the option isn't presented during normal loading.
            if (isError) {
                ImVec2 panelMax = ImVec2(panelMin.x + wrapW,
                                         ImGui::GetCursorScreenPos().y);
                ImGui::SetCursorScreenPos(panelMin);
                ImGui::InvisibleButton("##status_copy",
                                       ImVec2(wrapW, panelMax.y - panelMin.y));
                if (ImGui::IsItemHovered() && !statusMsg.empty()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", g_statusCopied
                                                ? "Copied to clipboard"
                                                : "Click to copy");
                }
                if (ImGui::IsItemClicked() && !statusMsg.empty()) {
                    ImGui::SetClipboardText(statusMsg.c_str());
                    g_statusCopied     = true;
                    g_statusCopiedTime = ImGui::GetTime();
                }
            }
            // Auto-reset the "Copied" state after a short while.
            if (g_statusCopied && ImGui::GetTime() - g_statusCopiedTime > 1.5) {
                g_statusCopied = false;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

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
            "An admin will review and approve or deny it. "
            "If this isn't your account, you can decline and close the loader.");

        ImGui::Dummy(ImVec2(0, 4));

        if (g_hwidResetSubmitted) {
            bool inFlight = g_hwidResetInFlight.load();
            std::string msg = GetHwidResetMessage();
            if (inFlight || msg.empty()) {
                ImGui::TextColored(COL_TEXT_DIM, "Submitting request...");
            } else {
                ImGui::TextWrapped("%s", msg.c_str());
            }
        } else if (g_hwidResetDeclined) {
            ImGui::TextColored(COL_TEXT_DIM,
                "No reset request sent. You can close the loader.");
        } else {
            // Two-button row: Yes (request) / No (decline)
            float fullW = ImGui::GetContentRegionAvail().x;
            float gap   = 8.0f;
            float halfW = (fullW - gap) * 0.5f;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

            // Yes — Request reset (orange)
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.40f, 0.05f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.50f, 0.10f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.30f, 0.03f, 1.00f));
            if (ImGui::Button("Request HWID Reset", ImVec2(halfW, 36))) {
                g_hwidResetSubmitted = true;
                g_hwidResetInFlight  = true;
                std::string tok = g_authInfo.token;
                std::string hw  = g_hwid;
                std::thread([tok, hw]() {
                    Hwid::HardwareDetails details = Hwid::GetHardwareDetails();
                    Api::HwidResetResponse r = Api::SubmitHwidResetRequest(tok, hw, details);
                    SetHwidResetMessage(r.message);
                    g_hwidResetInFlight = false;
                }).detach();
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.0f, gap);

            // No — Decline (neutral)
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.20f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.33f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.16f, 0.19f, 1.00f));
            if (ImGui::Button("Don't Send Request", ImVec2(halfW, 36))) {
                g_hwidResetDeclined = true;
            }
            ImGui::PopStyleColor(3);

            ImGui::PopStyleVar();
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

    // Resize the OS window to fit actual content — same pattern as DrawProductScreen.
    // Without this, the HWID mismatch panel + buttons extend below the window
    // bottom and get clipped (NoScrollbar flag prevents the user from scrolling
    // down to see them).
    g_neededWindowH = TITLE_BAR_H + ImGui::GetCursorPosY() + SX(16.0f);
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

    // Request global backgrounds immediately
    // Backgrounds removed in favor of neutral grey
    // Alternative endpoints for easy cycling:
    // ImageLoader::Request("login_bg", "/api/assets/backgrounds/login");
    // ImageLoader::Request("products_bg", "/api/assets/backgrounds/products");
    // ImageLoader::Request("spoofer_bg", "/api/assets/backgrounds/spoofer");
    ImageLoader::Request("settings_bg", "https://scootware.us/images/hero-bg.png");

    // Auto-login if session exists
    Session::Credentials creds;
    if (Session::LoadCredentials(creds)) {
        SetStatus("Auto-logging in...");
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
                SetStatus("");
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
                // Only exit once the background injection thread has finished
                // its cleanup (ReportLoaderEvent + flag writes).  Without this
                // guard the UI loop can tear down D3D while the detached thread
                // is still writing to g_injecting or making network calls.
                if (g_backgroundCleanupDone.load(std::memory_order_acquire)) {
                    g_shouldExit = true;
                }
        }
        if (g_shouldExit.load()) {
            Handoff::Wipe();
            ::PostQuitMessage(0);
            done = true;
            break;
        }

        // Animate screen fade-in
        if (g_screenFade < 1.0f) {
            g_screenFade += io.DeltaTime * 5.0f; // ~200ms ease
            if (g_screenFade > 1.0f) g_screenFade = 1.0f;
        }

            // Auto-resize window to fit content (triggered by DrawProductScreen setting g_neededWindowH)
            {
                static float lastWindowH = 0.0f;
                float needH = g_neededWindowH;
                if (needH > 0.0f && fabsf(needH - lastWindowH) > 0.5f && g_hwnd) {
                    int wndW = SXi(WND_W_BASE);
                    // Clamp to reasonable range
                    if (needH < SX(300.0f)) needH = SX(300.0f);
                    if (needH > SX(900.0f)) needH = SX(900.0f);
                    RECT rc; ::GetWindowRect(g_hwnd, &rc);
                    int clientH = (int)ceilf(needH);
                    if (rc.bottom - rc.top != clientH) {
                        ::SetWindowPos(g_hwnd, nullptr, rc.left, rc.top, wndW, clientH,
                                       SWP_NOZORDER);
                        lastWindowH = needH;
                    }
                }
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

        // Draw background - neutral grey for all pages (instead of local images)
        // Alternative: Load from API endpoints for custom backgrounds
        // const char* bgKey = nullptr;
        // if (g_state == AppState::Login) bgKey = "login_bg";
        // else if (g_state == AppState::Products) bgKey = "products_bg";
        // else if (g_state == AppState::Spoofer) bgKey = "spoofer_bg";
        // else if (g_state == AppState::Settings) bgKey = "settings_bg";
        //
        // API endpoints for easy cycling:
        // /api/assets/backgrounds/login
        // /api/assets/backgrounds/products
        // /api/assets/backgrounds/spoofer
        // /api/assets/backgrounds/settings

        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 winPos = ImGui::GetWindowPos();
            // Inset by half the stroke thickness so arc centres align with the
            // rainbow border's inner edge arc centres, eliminating corner bleed.
            const float half_border = WINDOW_BORDER_PX * 0.5f;
            ImVec2 pMin = ImVec2(winPos.x + half_border, winPos.y + half_border);
            ImVec2 pMax = ImVec2(winPos.x + io.DisplaySize.x - half_border,
                                 winPos.y + io.DisplaySize.y - half_border);
            dl->AddRectFilled(pMin, pMax, ImGui::GetColorU32(COL_PANEL),
                              WND_CORNER_RADIUS - half_border, ImDrawFlags_RoundCornersAll);
        }

        DrawTitleBar();

        // Content region (below title bar) with padding, fades in on transition
        ImGui::SetCursorPos(ImVec2(0, TITLE_BAR_H));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_screenFade);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
        
        // Suppress scrollbar dynamically (especially on the compressed login screen)
        ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoBackground;
        if (g_state == AppState::Login    || g_state == AppState::Injecting ||
                g_state == AppState::Spoofer  ||
            g_state == AppState::DcuWizard) {
            childFlags |= ImGuiWindowFlags_NoScrollbar;
        }

        ImGui::BeginChild("##content",
                          ImVec2(io.DisplaySize.x, io.DisplaySize.y - TITLE_BAR_H),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          childFlags);

        if      (g_state == AppState::Login)      DrawLoginScreen();
        else if (g_state == AppState::Products)   DrawProductScreen();
        else if (g_state == AppState::Spoofer)    DrawSpooferScreen();
            else if (g_state == AppState::Settings)   DrawSettingsScreen();
        else if (g_state == AppState::Injecting)  DrawInjectingScreen();
        else if (g_state == AppState::DcuWizard)  DrawDcuWizardScreen();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        // Animated rainbow border on top of everything.
        // Path centered ON the window edge (0,0)→(W,H): outer half clips at display
        // boundary so the solid colour starts right at the pixel edge; inner edge sits
        // at half_thick inward, matching the background fill exactly.
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 a = ImVec2(0.0f, 0.0f);
            ImVec2 b = ImVec2(io.DisplaySize.x, io.DisplaySize.y);
            
            float phase = fmodf((float)ImGui::GetTime() * 0.10f, 1.0f);
            DrawRainbowBorder(dl, a, b, WND_CORNER_RADIUS, WINDOW_BORDER_PX, phase);
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
