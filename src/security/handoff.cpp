#include "handoff.h"
#include "obf.h"
#include "../util/diaglog.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

namespace {

    // HMAC-SHA256 raw bytes via Windows CNG. Mirrors the helper in api.cpp
    // so we don't pull in any new crypto.
    std::vector<uint8_t> HmacSha256(const uint8_t* key, size_t keyLen,
                                    const uint8_t* data, size_t dataLen) {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD cbHash = 0, cbHashObj = 0, cbData = 0;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
            return {};

        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                              (PUCHAR)&cbHashObj, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                              (PUCHAR)&cbHash, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }

        std::vector<BYTE> hashObj(cbHashObj), hash(cbHash);
        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), cbHashObj,
                             (PUCHAR)key, (ULONG)keyLen, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptHashData(hHash, (PUCHAR)data, (ULONG)dataLen, 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptFinishHash(hHash, hash.data(), cbHash, 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }

        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::vector<uint8_t>(hash.begin(), hash.end());
    }

    // Tiny RFC4648 base64 encoder. Output is fixed-alphabet (no URL-safe
    // variant) to match what the external expects.
    std::string Base64(const uint8_t* data, size_t len) {
        static const char tbl[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t v = (uint32_t)data[i] << 16;
            if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
            if (i + 2 < len) v |= (uint32_t)data[i + 2];

            out.push_back(tbl[(v >> 18) & 0x3F]);
            out.push_back(tbl[(v >> 12) & 0x3F]);
            out.push_back(i + 1 < len ? tbl[(v >> 6) & 0x3F] : '=');
            out.push_back(i + 2 < len ? tbl[v & 0x3F]        : '=');
        }
        return out;
    }

    // ── Shared-memory fallback channel ──────────────────────────────────
    //
    // We hold the file mapping handle in a process-static so Wipe() can
    // close it after the child has had a chance to read. The mapping is
    // named Local\SW_HANDOFF_<loader_pid> so the child can find it via
    // its parent PID without needing any extra env var.
    HANDLE g_shmemHandle = nullptr;
    void*  g_shmemView   = nullptr;

    constexpr DWORD kShmemSize = 4096;

    // On-disk layout inside the file mapping. Magic guards against another
    // process at the same name returning garbage; version lets us evolve
    // the schema without confusing older externals.
    //
    //   uint32_t magic = 'SWHD'
    //   uint32_t version = 1
    //   uint32_t sw_s_len
    //   uint32_t sw_p_len
    //   uint32_t sw_e_len
    //   char     payload[sw_s_len + sw_p_len + sw_e_len]   (no separators)
    //
    // The wrapped SW_S can be a few hundred bytes; the whole blob fits
    // comfortably in 4 KiB.
    constexpr uint32_t kShmemMagic   = 0x44485753; // 'SWHD' little-endian
    constexpr uint32_t kShmemVersion = 1;

    void CloseShmem() {
        if (g_shmemView) {
            // Best-effort scrub before unmap so a coredump of the loader
            // doesn't preserve the wrapped blob.
            SecureZeroMemory(g_shmemView, kShmemSize);
            UnmapViewOfFile(g_shmemView);
            g_shmemView = nullptr;
        }
        if (g_shmemHandle) {
            CloseHandle(g_shmemHandle);
            g_shmemHandle = nullptr;
        }
    }

    bool PublishShmem(const std::string& sw_s,
                      const std::string& sw_p,
                      const std::string& sw_e) {
        // Always start clean — Set() can theoretically be called twice
        // (e.g. retry path) and we want the freshest blob.
        CloseShmem();

        char name[64];
        // Local\ namespace == per-session, so a different RDP session of
        // the same machine cannot snoop on us.
        std::snprintf(name, sizeof(name),
                      OBF_A("Local\\SW_HANDOFF_%lu"),
                      (unsigned long)GetCurrentProcessId());

        // PAGE_READWRITE + a unique per-PID name. We never need exec or
        // cross-session visibility, so the protections are tight.
        g_shmemHandle = CreateFileMappingA(
            INVALID_HANDLE_VALUE,  // backed by the system paging file
            nullptr,
            PAGE_READWRITE,
            0,
            kShmemSize,
            name);

        if (!g_shmemHandle) return false;

        // ERROR_ALREADY_EXISTS is fine on its own (we'd just be reusing
        // an existing mapping), but catching it lets us decide whether to
        // bail. In practice the loader runs once per launch attempt so
        // this should never trigger.
        DWORD lastErr = GetLastError();
        if (lastErr == ERROR_ALREADY_EXISTS) {
            // Reuse the existing mapping rather than fail outright.
        }

        g_shmemView = MapViewOfFile(g_shmemHandle, FILE_MAP_WRITE, 0, 0, 0);
        if (!g_shmemView) {
            CloseHandle(g_shmemHandle);
            g_shmemHandle = nullptr;
            return false;
        }

        // Zero the section first so a half-written blob from a previous
        // call can never be read by the child.
        SecureZeroMemory(g_shmemView, kShmemSize);

        uint8_t* p = (uint8_t*)g_shmemView;
        uint32_t header[5] = {
            kShmemMagic,
            kShmemVersion,
            (uint32_t)sw_s.size(),
            (uint32_t)sw_p.size(),
            (uint32_t)sw_e.size(),
        };
        const size_t headerSize = sizeof(header);
        const size_t payloadSize = sw_s.size() + sw_p.size() + sw_e.size();

        if (headerSize + payloadSize > kShmemSize) {
            // Pathologically large token — refuse rather than truncate.
            CloseShmem();
            return false;
        }

        std::memcpy(p, header, headerSize);
        size_t off = headerSize;
        std::memcpy(p + off, sw_s.data(), sw_s.size()); off += sw_s.size();
        std::memcpy(p + off, sw_p.data(), sw_p.size()); off += sw_p.size();
        std::memcpy(p + off, sw_e.data(), sw_e.size());

        // Write barrier so the child's MapViewOfFile + memcpy can't
        // observe a partially-written header. The Win32 API guarantees
        // FlushViewOfFile semantics for shared sections backed by the
        // pagefile, so this is mostly defensive against future codegen.
        FlushViewOfFile(g_shmemView, headerSize + payloadSize);
        return true;
    }
}

namespace Handoff {

    bool Set(const std::string& token,
             const std::string& hwid,
             const std::string& productId,
             const std::string& expiresAt)
    {
        LDIAG() << "[Handoff::Set] token len=" << token.size()
                << " hwid len=" << hwid.size()
                << " productId='" << productId << "'"
                << " expiresAt='" << expiresAt << "'";
        if (token.empty() || hwid.empty()) {
            LDIAG_LINE("[Handoff::Set] FAILED: empty token or hwid");
            return false;
        }

        // Derive a 32-byte XOR pad from HMAC-SHA256(hwid, "SW_S_KEY_v1").
        // Any client without the matching HWID gets garbage on decode.
        std::string keyLabel = OBF_S("SW_S_KEY_v1");
        std::vector<uint8_t> pad = HmacSha256(
            (const uint8_t*)hwid.data(), hwid.size(),
            (const uint8_t*)keyLabel.data(), keyLabel.size());
        if (pad.size() < 32) return false;

        // For tokens longer than 32 bytes, extend the pad by re-HMAC'ing the
        // previous pad block. Standard counter-mode KDF construction.
        std::vector<uint8_t> wrapped(token.size());
        for (size_t i = 0; i < token.size(); ++i) {
            if (i && (i % 32) == 0) {
                pad = HmacSha256((const uint8_t*)hwid.data(), hwid.size(),
                                 pad.data(), pad.size());
                if (pad.size() < 32) return false;
            }
            wrapped[i] = (uint8_t)token[i] ^ pad[i % 32];
        }

        std::string encoded = Base64(wrapped.data(), wrapped.size());
        LDIAG() << "[Handoff::Set] encoded SW_S length=" << encoded.size();

        // Channel 1 — env vars. The loader's own env block (which we'll
        // also serialize in BuildChildEnvironment for explicit pass-through
        // to CreateProcess).
        BOOL es = SetEnvironmentVariableA(OBF_A("SW_S"), encoded.c_str());
        BOOL ep = SetEnvironmentVariableA(OBF_A("SW_P"), productId.c_str());
        BOOL ee = SetEnvironmentVariableA(OBF_A("SW_E"), expiresAt.c_str());
        LDIAG() << "[Handoff::Set] SetEnvironmentVariableA SW_S=" << (es?"OK":"FAIL")
                << " SW_P=" << (ep?"OK":"FAIL")
                << " SW_E=" << (ee?"OK":"FAIL");

        // Channel 2 — Local\SW_HANDOFF_<pid> file mapping. Survives any
        // env-inheritance quirk during process hollowing.
        bool shmemOk = PublishShmem(encoded, productId, expiresAt);
        LDIAG() << "[Handoff::Set] shmem publish " << (shmemOk?"OK":"FAILED")
                << " (mapping name = Local\\SW_HANDOFF_" << ::GetCurrentProcessId() << ")";
        if (!shmemOk) {
            // Non-fatal: env-var channel still works in the common case.
            // Logged via OutputDebugString so we can spot it in DbgView
            // without leaking onto the loader UI.
            OutputDebugStringA(OBF_A("[handoff] shmem publish failed; relying on env channel only\n"));
        }

        // Best-effort scrub of the local pad/wrap buffers.
        SecureZeroMemory(pad.data(),     pad.size());
        SecureZeroMemory(wrapped.data(), wrapped.size());
        SecureZeroMemory(&encoded[0],    encoded.size());
        return true;
    }

    std::vector<char> BuildChildEnvironment() {
        // GetEnvironmentStringsA returns a sequence of "KEY=VAL\0KEY=VAL\0...\0"
        // ending in a double NUL. We need to copy this into our own buffer
        // because the spec requires the caller (us) to own the lifetime of
        // the lpEnvironment block until CreateProcess returns.
        LPCH envStrings = GetEnvironmentStringsA();
        if (!envStrings) {
            LDIAG_LINE("[Handoff::BuildChildEnvironment] GetEnvironmentStringsA returned NULL");
            return {};
        }

        // Walk to the terminating double-NUL to compute total size.
        size_t bytes = 0;
        bool sawSwS = false, sawSwP = false, sawSwE = false;
        const char* p = envStrings;
        while (true) {
            size_t entryLen = std::strlen(p);
            if (entryLen == 0) {
                // Hit the double-NUL terminator.
                bytes += 1; // include the final NUL
                break;
            }
            // Sanity check: confirm the SW_* secrets actually made it into
            // the env block we're about to hand to CreateProcess.
            if (entryLen >= 4 && std::strncmp(p, "SW_S=", 5) == 0) sawSwS = true;
            if (entryLen >= 4 && std::strncmp(p, "SW_P=", 5) == 0) sawSwP = true;
            if (entryLen >= 4 && std::strncmp(p, "SW_E=", 5) == 0) sawSwE = true;
            bytes += entryLen + 1; // include intra-entry NUL
            p += entryLen + 1;
        }

        LDIAG() << "[Handoff::BuildChildEnvironment] env block size=" << bytes
                << " bytes, SW_S " << (sawSwS?"present":"MISSING")
                << ", SW_P " << (sawSwP?"present":"MISSING")
                << ", SW_E " << (sawSwE?"present":"MISSING");

        std::vector<char> out(bytes);
        std::memcpy(out.data(), envStrings, bytes);
        FreeEnvironmentStringsA(envStrings);
        return out;
    }

    void Wipe() {
        // Env vars come down immediately — they're a memory-snapshot risk
        // even on the loader side (somebody could WriteProcessMemory us
        // and dump GetEnvironmentStrings).
        SetEnvironmentVariableA(OBF_A("SW_S"), nullptr);
        SetEnvironmentVariableA(OBF_A("SW_P"), nullptr);
        SetEnvironmentVariableA(OBF_A("SW_E"), nullptr);

        // We deliberately do NOT close the shmem section here. The child
        // may not have read it yet (slow machines — first session::bootstrap()
        // run can be several seconds in if console::initialize, input::initialize,
        // or asset disk I/O is sluggish). The mapping is per-PID, scoped
        // to the loader's session, and disappears the moment the loader
        // process exits (which the auto-close path triggers ~1.5s after
        // a successful inject anyway). Leaving it open for that brief
        // window costs nothing and removes the only timing-related
        // failure mode of the fallback channel.
    }
}
