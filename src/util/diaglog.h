#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// diaglog — release-mode diagnostic logger (loader side)
//
// The loader is a windowed app, so the std::cout chatter that runpe.cpp /
// handoff.cpp emit is invisible in shipped builds (no console attached).
// This header writes the same messages to %TEMP%\scootware-diag.log so they
// land in the SAME file the external uses; matching the external's logger
// keeps cause-and-effect on a single timeline.
//
// Output is also forwarded to OutputDebugStringA so DbgView shows it live.
// ─────────────────────────────────────────────────────────────────────────────

#include <windows.h>
#include <string>
#include <string_view>
#include <cstdio>
#include <mutex>
#include <sstream>

namespace LoaderDiag {

    inline std::mutex& Mutex() {
        static std::mutex m;
        return m;
    }

    inline std::string& PathCache() {
        static std::string p;
        return p;
    }

    inline const char* FilePath() {
        auto& cache = PathCache();
        if (!cache.empty()) return cache.c_str();

        char temp[MAX_PATH] = {};
        DWORD n = ::GetTempPathA(MAX_PATH, temp);
        if (n == 0 || n > MAX_PATH) {
            cache = "scootware-diag.log";
            return cache.c_str();
        }
        cache.assign(temp, n);
        if (cache.back() != '\\' && cache.back() != '/') cache += '\\';
        cache += "scootware-diag.log";
        return cache.c_str();
    }

    inline void WriteLine(std::string_view line) {
        std::lock_guard<std::mutex> lock(Mutex());

        ::OutputDebugStringA("[scootware-loader] ");
        std::string nul_terminated{ line };
        ::OutputDebugStringA(nul_terminated.c_str());
        ::OutputDebugStringA("\n");

        FILE* f = nullptr;
        if (fopen_s(&f, FilePath(), "ab") != 0 || !f) return;

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u  loader-pid=%lu  tid=%lu  %.*s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            ::GetCurrentProcessId(), ::GetCurrentThreadId(),
            static_cast<int>(line.size()), line.data());
        std::fclose(f);
    }

    // Stream-style helper so existing std::cout-style callsites can be
    // ported with minimal churn:
    //   LDIAG() << "[RunPE] base = 0x" << std::hex << base;
    struct Stream {
        std::ostringstream os;
        ~Stream() { WriteLine(os.str()); }
        template<typename T>
        Stream& operator<<(T&& v) { os << std::forward<T>(v); return *this; }
        Stream& operator<<(std::ostream& (*manip)(std::ostream&)) { os << manip; return *this; }
        Stream& operator<<(std::ios_base& (*manip)(std::ios_base&)) { os << manip; return *this; }
    };

    inline void Banner(const char* tag) {
        std::lock_guard<std::mutex> lock(Mutex());
        FILE* f = nullptr;
        if (fopen_s(&f, FilePath(), "ab") != 0 || !f) return;
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        std::fprintf(f,
            "\n"
            "==========================================================\n"
            " %04u-%02u-%02u %02u:%02u:%02u.%03u  %s  pid=%lu\n"
            "==========================================================\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            tag ? tag : "scootware-loader",
            ::GetCurrentProcessId());
        std::fclose(f);
    }
}

#define LDIAG()        ::LoaderDiag::Stream{}
#define LDIAG_LINE(s)  ::LoaderDiag::WriteLine(s)
