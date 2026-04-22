#include "process_wait.h"
#include "../security/obf.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cwctype>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {
    std::string ToLowerAscii(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<char>(std::tolower((unsigned char)c)));
        return out;
    }
}

namespace ProcessWait {

std::wstring HostProcessFor(const std::string& productId) {
    // NOTE: keep this list in sync with the per-product entries in the
    // admin panel. Empty == "no host process gating required". When
    // adding a new title, lowercase the product id and pick the EXE
    // name that the user sees in Task Manager (the same string the
    // external module passes to g::memory.initialize).
    const std::string id = ToLowerAscii(productId);

    if (id == OBF_S("cs2"))    return L"cs2.exe";
    if (id == OBF_S("rust"))   return L"RustClient.exe";
    if (id == OBF_S("tarkov")) return L"EscapeFromTarkov.exe";
    if (id == OBF_S("rivals")) return L"Marvel-Win64-Shipping.exe";

    return L"";
}

bool IsRunning(const std::wstring& exeName) {
    if (exeName.empty()) return true; // no gating requested

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool WaitForProcess(const std::wstring& exeName,
                    const std::function<bool()>& cancel,
                    const std::function<void(int)>& tickCallback)
{
    if (exeName.empty()) return true;

    const auto start = std::chrono::steady_clock::now();
    int lastReportedSec = -1;

    while (true) {
        if (cancel && cancel()) return false;

        if (IsRunning(exeName)) return true;

        const auto now     = std::chrono::steady_clock::now();
        const int  elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - start).count());

        // Throttle UI updates to once per second so we don't thrash
        // std::string assignment on the render thread (status text is
        // a plain global; assignment is racy but acceptable for a
        // cosmetic countdown).
        if (tickCallback && elapsed != lastReportedSec) {
            tickCallback(elapsed);
            lastReportedSec = elapsed;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace ProcessWait
