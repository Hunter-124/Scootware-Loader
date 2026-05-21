#pragma once
//
// obf.h
//
// Convenience layer over skCrypter.h for compile-time string obfuscation.
//
// All `_X*` macros expand to a lambda whose internal `skCrypter` is
// constructed at compile time with the encrypted bytes baked into the binary.
// The `decrypt()` call is the FIRST runtime touch — strings never appear in
// `strings`-style scans, IDA / Ghidra string windows, or any static analysis.
//
// USAGE
// -----
//   OBF_S("scootware.us")        -> std::string  (decrypted at call site)
//   OBF_W(L"ntdll.dll")          -> std::wstring (decrypted at call site)
//   OBF_A("kernel32.dll")        -> const char*  TEMPORARY lifetime — use only
//                                                within the same full expression
//   OBF_C(L"VBoxGuest")          -> const wchar_t* TEMPORARY lifetime
//
// Always prefer `OBF_S` / `OBF_W` when you need to pass the value to something
// that copies it (std::string parameter, std::wstring assignment, etc.).
// Reserve the pointer-returning variants for direct WinAPI calls.
//
// IMPORTANT
// ---------
//   * Release builds only. MSVC Debug emits the strings in plaintext because
//     `constexpr` is not honored when optimizations are off. This is fine —
//     debug builds aren't shipped.
//   * Each invocation rebuilds the lambda's local skCrypter on the stack —
//     the encrypted bytes live in .rdata, the decrypted copy never persists
//     beyond the expression.
//

#include "skCrypter.h"
#include <string>

// std::string — decrypted copy returned by value (safe to store).
#define OBF_S(str) ([&]() -> std::string { \
    auto _enc = skCrypt(str);              \
    std::string _out(_enc.decrypt());      \
    _enc.clear();                          \
    return _out;                           \
}())

// std::wstring — decrypted copy returned by value (safe to store).
#define OBF_W(str) ([&]() -> std::wstring { \
    auto _enc = skCrypt(str);               \
    std::wstring _out(_enc.decrypt());      \
    _enc.clear();                           \
    return _out;                            \
}())

// Raw const char* — pointer is valid ONLY for the lifetime of the
// enclosing full-expression. Suitable for inline WinAPI calls:
//   GetProcAddress(h, OBF_A("NtQueryInformationProcess"));
#define OBF_A(str) (skCrypt(str).decrypt())

// Raw const wchar_t* — same lifetime caveat as OBF_A.
#define OBF_C(str) (skCrypt(str).decrypt())

// ────────────────────────────────────────────────────────────────────────
// Anti-RE control-flow noise. Forces the optimizer to keep an opaque
// branch around obfuscated code, frustrating naive decompiler pattern
// matching. Cheap at runtime (single CPUID-derived comparison).
// ────────────────────────────────────────────────────────────────────────
#if defined(_MSC_VER)
    #include <intrin.h>
    namespace obf_detail {
        __forceinline volatile int _opaque() {
            int r[4] = {};
            __cpuid(r, 0);
            return r[0] & 0x1; // value depends on the host CPU
        }
    }
    // Branch the compiler can't fold away; both arms are kept.
    #define OBF_NOISE() do { if (obf_detail::_opaque() == 0xDEADBEEF) { __nop(); } } while (0)
#else
    #define OBF_NOISE() ((void)0)
#endif
