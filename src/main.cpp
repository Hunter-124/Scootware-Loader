#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <atomic>

// ImGui headers (Assume these are included in the project)
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Backend interfaces
#include "api/api.h"
#include "api/session.h"
#include "hwid.h"
#include "memory/runpe.h"
#include "vmdetect.h"

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Application State
enum class AppState {
    Login,
    Products,
    Injecting
};

AppState g_state = AppState::Login;
Api::AuthResponse g_authInfo;
std::string g_statusMessage = "";
char g_username[64] = "";
char g_password[64] = "";
bool g_rememberMe = false;
std::string g_hwid = "";

// Injection runs on a background thread so the UI stays responsive.
static std::atomic<bool> g_injecting{ false };

// HWID mismatch state
bool g_hwidMismatch = false;
bool g_hwidResetSubmitted = false;
std::string g_hwidResetMessage = "";

// Theme setup - Scootware Purple
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Modern flat style
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(12, 8);

    ImVec4* colors = style.Colors;
    
    // Deep purple / black theme base
    colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_Border]         = ImVec4(0.25f, 0.10f, 0.35f, 1.00f);
    colors[ImGuiCol_BorderShadow]   = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.35f, 0.20f, 0.45f, 1.00f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.15f, 0.08f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.25f, 0.10f, 0.40f, 1.00f);
    
    // Primary Purple (#A855F7 is roughly 168, 85, 247)
    colors[ImGuiCol_Button]         = ImVec4(0.66f, 0.33f, 0.97f, 0.80f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.66f, 0.33f, 0.97f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.55f, 0.25f, 0.85f, 1.00f);
    
    colors[ImGuiCol_CheckMark]      = ImVec4(0.66f, 0.33f, 0.97f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.10f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.35f, 0.20f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.45f, 0.25f, 0.55f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
}

void DrawLoginScreen() {
    ImGui::Text("Scootware Login");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputText("Username", g_username, IM_ARRAYSIZE(g_username));
    ImGui::InputText("Password", g_password, IM_ARRAYSIZE(g_password), ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    ImGui::Checkbox("Remember Me", &g_rememberMe);

    ImGui::Spacing();
    
    if (ImGui::Button("Login", ImVec2(ImGui::GetContentRegionAvail().x, 40))) {
        // Trim inputs
        std::string userStr = g_username;
        std::string passStr = g_password;
        
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
        };
        
        trim(userStr);
        trim(passStr);

        g_statusMessage = "Authenticating...";
        g_authInfo = Api::Login(userStr, passStr);
        
        if (g_authInfo.success) {
            if (g_rememberMe) {
                Session::SaveCredentials(userStr, passStr);
            }
            g_state = AppState::Products;
            g_statusMessage = "";
        } else {
            g_statusMessage = g_authInfo.message;
        }
    }

    if (!g_statusMessage.empty()) {
        if (g_authInfo.success) ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", g_statusMessage.c_str());
        else ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", g_statusMessage.c_str());
    }
}

void DrawProductScreen() {
    ImGui::Text("Available Products");
    ImGui::Separator();
    ImGui::Spacing();

    if (g_authInfo.subscriptions.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.3f, 1.0f), "No active subscriptions found on your account.");
        return;
    }

    for (const auto& prod : g_authInfo.subscriptions) {
        // Style adjustments based on access
        if (!prod.hasAccess) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }

        ImGui::PushID(prod.productId.c_str());
        
        // Product Tile
        if (ImGui::Button(prod.productId.c_str(), ImVec2(150, 50))) {
            if (prod.hasAccess) {
                g_state = AppState::Injecting;
                g_hwidMismatch = false;
                g_hwidResetSubmitted = false;
                g_hwidResetMessage = "";
                g_statusMessage = "Streaming payload for " + prod.productId + "...";

                if (!g_injecting.exchange(true)) {
                    std::string productId = prod.productId;
                    std::string token     = g_authInfo.token;
                    std::string hwid      = g_hwid;
                    std::thread([productId, token, hwid]() {
                        // Attempt to fetch hollow executable first
                        auto [hollowBuffer, hollowAllocSize] = Api::StreamAsset(productId, "hollow_exe", token, hwid);
                        std::string customHostPath = "";
                        
                        if (!hollowBuffer.empty()) {
                            char tempPath[MAX_PATH];
                            if (GetTempPathA(MAX_PATH, tempPath)) {
                                char tempFile[MAX_PATH];
                                if (GetTempFileNameA(tempPath, "sct", 0, tempFile)) {
                                    // Change extension to .exe
                                    customHostPath = std::string(tempFile) + ".exe";
                                    DeleteFileA(tempFile); // Remove the .tmp file created by GetTempFileName
                                    
                                    FILE* f = nullptr;
                                    fopen_s(&f, customHostPath.c_str(), "wb");
                                    if (f) {
                                        fwrite(hollowBuffer.data(), 1, hollowBuffer.size(), f);
                                        fclose(f);
                                    } else {
                                        customHostPath = ""; // Fallback if write fails
                                    }
                                }
                            }
                        }

                        auto [exeBuffer, allocSize] = Api::StreamAsset(productId, "primary_exe", token, hwid);
                        if (!exeBuffer.empty()) {
                            if (RunPE::Execute(exeBuffer, allocSize, customHostPath)) {
                                g_statusMessage = "Injected successfully! You can close this loader.";
                            } else {
                                g_statusMessage = "Injection failed (RunPE Error).";
                            }
                        } else {
                            g_hwidMismatch = Api::LastStreamWasHwidMismatch();
                            if (g_hwidMismatch) {
                                g_statusMessage = "HWID mismatch — this account is bound to a different machine.";
                            } else {
                                g_statusMessage = "Failed to stream asset from secure server.";
                            }
                        }
                        g_injecting = false;
                    }).detach();
                }
            }
        }
        
        ImGui::PopID();
        
        ImGui::SameLine();
        
        // Display EXPIRES info next to the tile
        ImGui::BeginGroup();
        if (prod.hasAccess) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "ACTIVE (%s)", prod.tier.c_str());
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Expires: %s", prod.expiresAt.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "LOCKED");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", prod.expiresAt.c_str());
        }
        ImGui::EndGroup();

        if (!prod.hasAccess) {
            ImGui::PopStyleColor(4);
        }

        ImGui::Spacing();
    }
    
    ImGui::Separator();
    if (ImGui::Button("Logout", ImVec2(100, 30))) {
        Session::ClearCredentials();
        g_state = AppState::Login;
        g_statusMessage = "Logged out.";
        g_password[0] = '\0';
        g_rememberMe = false;
    }
}

void DrawInjectingScreen() {
    ImGui::Text("Scootware Security");
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextWrapped("%s", g_statusMessage.c_str());
    
    ImGui::Spacing();

    // ── HWID mismatch: offer a reset request ──────────────────────────────
    if (g_hwidMismatch) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
            "Your account is bound to a different machine.");
        ImGui::TextWrapped(
            "If you changed hardware or are on a new PC, you can request a\n"
            "HWID reset. An admin will review and approve or deny it.");

        ImGui::Spacing();

        if (g_hwidResetSubmitted) {
            if (g_hwidResetMessage.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Submitting...");
            } else {
                ImGui::TextWrapped("%s", g_hwidResetMessage.c_str());
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.40f, 0.05f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.50f, 0.10f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.30f, 0.03f, 1.00f));

            if (ImGui::Button("Request HWID Reset", ImVec2(ImGui::GetContentRegionAvail().x, 36))) {
                g_hwidResetSubmitted = true;
                Hwid::HardwareDetails details = Hwid::GetHardwareDetails();
                Api::HwidResetResponse r = Api::SubmitHwidResetRequest(g_authInfo.token, g_hwid, details);
                g_hwidResetMessage = r.message;
            }

            ImGui::PopStyleColor(3);
        }

        ImGui::Spacing();
    }
    // ─────────────────────────────────────────────────────────────────────

    if (ImGui::Button("Return to Products", ImVec2(ImGui::GetContentRegionAvail().x, 40))) {
        g_state = AppState::Products;
        g_hwidMismatch = false;
        g_hwidResetSubmitted = false;
        g_hwidResetMessage = "";
    }
}

// Forward declare ImGui_ImplWin32_WndProcHandler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);



int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // [DEAD CODE] --child self-hollowing removed; scootware.exe is now the dedicated host.
    // if (pCmdLine && wcsstr(pCmdLine, L"--child")) {
    //     while (true) Sleep(10000);
    //     return 0;
    // }

    // ── VM / Sandbox detection ───────────────────────────────────────────
    {
        auto vmResult = VMDetect::Check();
        if (vmResult.isVM) {
            // Report to backend before exiting so admins see the detection in logs.
            // HWID may not be generated yet — pass empty string; the backend will
            // attempt a lookup by IP if available.
            Api::ReportDetection(
                vmResult.isVMEnv,
                vmResult.isDebugger,
                /*hwid=*/ "",
                vmResult.triggers
            );
            // Silent exit — don't reveal detection reason to analyst
            return 0;
        }
    }
    // ─────────────────────────────────────────────────────────────────────

    // Generate hardware fingerprint used for HWID binding
    g_hwid = Hwid::GetHWID();
    if (g_hwid.empty()) {
        std::cout << "[HWID] Warning: Failed to generate HWID, using fallback.\n";
        g_hwid = "UNKNOWN";
    } else {
        std::cout << "[HWID] " << g_hwid << "\n";
    }

    // Register window class
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ScootwareLoader", nullptr };
    ::RegisterClassExW(&wc);
    
    // Create rendering window
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Scootware Loader", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, 450, 400, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr; // Disable ini config loading

    SetupImGuiStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Auto-login if session exists
    Session::Credentials creds;
    if (Session::LoadCredentials(creds)) {
        g_statusMessage = "Auto-logging in...";
        g_authInfo = Api::Login(creds.username, creds.password);
        if (g_authInfo.success) {
            g_state = AppState::Products;
            g_statusMessage = "";
            g_rememberMe = true;
            strcpy_s(g_username, creds.username.c_str());
            // We don't necessarily need to fill g_password here as it's not used once logged in
        } else {
            g_statusMessage = "Session expired. Please login again.";
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

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // Ensure ImGui takes entire window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Scootware Loader", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // App Routing
        if (g_state == AppState::Login)         DrawLoginScreen();
        else if (g_state == AppState::Products) DrawProductScreen();
        else if (g_state == AppState::Injecting) DrawInjectingScreen();

        ImGui::End();

        // Render
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.08f, 0.08f, 0.10f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// ---------------------------------------------------------
// Boilerplate DX11 Implementation Below
// ---------------------------------------------------------

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
