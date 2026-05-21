#include "dcu.h"
#include "../api/api.h"
#include "../util/diaglog.h"
#include "obf.h"
#include "TrustedInstallerIntegrator.h"

#include <windows.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "bcrypt.lib")

#include <filesystem>
#include <fstream>
#include <Protocol/ScootwareConfig.h>

#pragma comment(lib, "ntdll.lib")

extern "C" NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

namespace fs = std::filesystem;

// ── Internal constants ──────────────────────────────────────────────────────

static const wchar_t* REG_BASE         = L"SOFTWARE\\Scootware\\DriverCompatibility";
static const wchar_t* MARKER_DIR       = L"C:\\ProgramData\\Scootware\\DCU";
static const wchar_t* MARKER_FILE      = L"C:\\ProgramData\\Scootware\\DCU\\ready.sig";
static const wchar_t* ESP_REL_DIR      = L"EFI\\Boot";
static const wchar_t* ESP_EFI_DXE      = L"ScootwareCompatDxe.efi";   // Must match EFIGUARD_DRIVER_FILENAME in Loader.c
static const wchar_t* ESP_EFI_LOADER   = L"Loader.efi";
static const wchar_t* ESP_EFI_DSEFIX   = L"EfiDSEFix.exe";

constexpr uint32_t DCU_CURRENT_VERSION = 1;

// Marker file binary layout (64 bytes)
#pragma pack(push, 1)
struct DcuMarker {
    uint32_t magic              = 0x55434421;   // "!DCU"
    uint32_t version            = 0;
    uint64_t installTimestamp   = 0;
    uint64_t postBootTimestamp  = 0;
    uint32_t efiMethod          = 0;
    uint32_t flags              = 0;
    uint8_t  sha256[32]         = {};
};
#pragma pack(pop)

// ── Registry helpers ────────────────────────────────────────────────────────

static HKEY OpenDcuKey(bool writable) {
    HKEY hk = nullptr;
    REGSAM access = KEY_READ | (writable ? KEY_WRITE : 0) | KEY_WOW64_64KEY;
    LONG ret = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, REG_BASE, 0, nullptr,
        REG_OPTION_NON_VOLATILE, access, nullptr, &hk, nullptr);
    if (ret != ERROR_SUCCESS) {
        LDIAG() << "[dcu] RegCreateKeyExW failed: 0x" << std::hex << ret;
        return nullptr;
    }
    return hk;
}

static bool RegReadDword(HKEY hk, const wchar_t* name, DWORD& out) {
    DWORD type = 0, size = sizeof(out);
    LONG ret = ::RegQueryValueExW(hk, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&out), &size);
    return (ret == ERROR_SUCCESS && type == REG_DWORD);
}

static bool RegReadQword(HKEY hk, const wchar_t* name, uint64_t& out) {
    DWORD type = 0, size = sizeof(out);
    LONG ret = ::RegQueryValueExW(hk, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&out), &size);
    return (ret == ERROR_SUCCESS && type == REG_QWORD);
}

static bool RegWriteDword(HKEY hk, const wchar_t* name, DWORD val) {
    return ::RegSetValueExW(hk, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&val), sizeof(val)) == ERROR_SUCCESS;
}

static bool RegWriteQword(HKEY hk, const wchar_t* name, uint64_t val) {
    return ::RegSetValueExW(hk, name, 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&val), sizeof(val)) == ERROR_SUCCESS;
}

static bool RegWriteString(HKEY hk, const wchar_t* name, const std::wstring& val) {
    return ::RegSetValueExW(hk, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(val.c_str()),
        static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

static bool RegReadString(HKEY hk, const wchar_t* name, std::wstring& out) {
    DWORD type = 0, size = 0;
    if (::RegQueryValueExW(hk, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size == 0)
        return false;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (::RegQueryValueExW(hk, name, nullptr, &type,
        reinterpret_cast<BYTE*>(buf.data()), &size) != ERROR_SUCCESS)
        return false;
    out.assign(buf.data());
    return true;
}

// Lossy wide → narrow for diag log lines (ASCII paths/GUIDs only — no MBCS
// round-trip needed). Use only for log messages, never for filesystem ops.
static std::string ToNarrowForLog(const wchar_t* s) {
    std::string out;
    if (!s) return out;
    for (; *s; ++s) {
        out.push_back(*s < 0x80 ? static_cast<char>(*s) : '?');
    }
    return out;
}
static std::string ToNarrowForLog(const std::wstring& s) {
    return ToNarrowForLog(s.c_str());
}

// ── Process-output capture helpers ─────────────────────────────────────────

// Run a command line and capture stdout+stderr. Returns true on CreateProcess
// success. exitCode and output are populated. Used for shelling out to bcdedit
// / mountvol while keeping the console hidden.
static bool RunCaptureOutput(const std::wstring& cmdLine,
                             DWORD& exitCode,
                             std::string& output,
                             DWORD timeoutMs = 5000) {
    output.clear();
    exitCode = 0xFFFFFFFFu;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!::CreatePipe(&hRead, &hWrite, &sa, 0))
        return false;
    ::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = {};
    std::wstring mutableCmd = cmdLine;
    BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(hWrite);
    if (!ok) {
        ::CloseHandle(hRead);
        return false;
    }

    char buf[1024];
    DWORD bytesRead = 0;
    while (::ReadFile(hRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buf, bytesRead);
    }
    ::CloseHandle(hRead);

    ::WaitForSingleObject(pi.hProcess, timeoutMs);
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return true;
}

// Read a single property line from `bcdedit /enum {ident}`. Returns true if the
// property is present (even if empty). out is the trimmed value text.
static bool BcdReadProperty(const std::wstring& ident,
                            const std::string& propName,
                            std::string& out) {
    out.clear();
    DWORD ec = 0;
    std::string text;
    if (!RunCaptureOutput(L"bcdedit /enum " + ident, ec, text) || ec != 0)
        return false;

    // bcdedit prints lines as "<name><spaces><value>". We look for the
    // property name at the start of a line, then take everything after the
    // run of spaces that follows.
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;

        if (line.size() < propName.size()) continue;
        if (line.compare(0, propName.size(), propName) != 0) continue;
        // Next char must be space (not part of a longer property name)
        if (line.size() > propName.size() && line[propName.size()] != ' ') continue;

        size_t v = propName.size();
        while (v < line.size() && line[v] == ' ') ++v;
        std::string value = line.substr(v);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
            value.pop_back();
        out = std::move(value);
        return true;
    }
    return false;
}

// ── Pre-install snapshot of state we (or our HVCI bypass) may modify ───────
//
// Stored under HKLM\SOFTWARE\Scootware\DriverCompatibility\Snapshot.
// Read back by DcuUninstall to restore state symmetrically. Each "Present"
// flag distinguishes "value was absent" from "value was present and X".
//
// Currently snapshots:
//   - DeviceGuard\HypervisorEnforcedCodeIntegrity\Enabled  (HVCI bypass path)
//   - DeviceGuard\HypervisorEnforcedCodeIntegrity\Policy\Enabled
//   - The created firmware boot-entry GUID (after bcdedit /copy succeeds)
//
// Also captures BCD audit fields (debug, loadoptions, hypervisorlaunchtype) so
// we can warn at install time and diagnose-from-logs at uninstall time, even
// though the install path doesn't modify them.
static const wchar_t* SNAPSHOT_SUBKEY =
    L"SOFTWARE\\Scootware\\DriverCompatibility\\Snapshot";

static HKEY OpenSnapshotKey(bool writable) {
    HKEY hk = nullptr;
    REGSAM access = KEY_READ | (writable ? KEY_WRITE : 0) | KEY_WOW64_64KEY;
    LONG ret = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, SNAPSHOT_SUBKEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, access, nullptr, &hk, nullptr);
    return (ret == ERROR_SUCCESS) ? hk : nullptr;
}

// Read a HVCI registry DWORD into (present, value). present=false means the
// value (or its parent key) was absent.
static void ReadHvciDword(const wchar_t* subKey, const wchar_t* valueName,
                          bool& present, DWORD& value) {
    present = false;
    value = 0;
    HKEY hk = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0,
        KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return;
    DWORD type = 0, size = sizeof(value);
    if (::RegQueryValueExW(hk, valueName, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        present = true;
    }
    ::RegCloseKey(hk);
}

// Captures current state to the snapshot subkey. Idempotent — overwriting a
// previous snapshot is fine because install always runs from a known starting
// point on subsequent retries (uninstall must precede a fresh install).
static void SnapshotPreInstallState() {
    HKEY hk = OpenSnapshotKey(true);
    if (!hk) {
        LDIAG() << "[dcu] WARN: failed to open snapshot subkey for write";
        return;
    }

    bool hvciPresent = false; DWORD hvciVal = 0;
    ReadHvciDword(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
                  L"HypervisorEnforcedCodeIntegrity",
                  L"Enabled", hvciPresent, hvciVal);
    RegWriteDword(hk, L"HvciEnabledPresent", hvciPresent ? 1 : 0);
    RegWriteDword(hk, L"HvciEnabled", hvciVal);

    bool polPresent = false; DWORD polVal = 0;
    ReadHvciDword(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
                  L"HypervisorEnforcedCodeIntegrity\\Policy",
                  L"Enabled", polPresent, polVal);
    RegWriteDword(hk, L"HvciPolicyEnabledPresent", polPresent ? 1 : 0);
    RegWriteDword(hk, L"HvciPolicyEnabled", polVal);

    // Audit-only BCD fields. Stored as "<absent>" or the literal value text.
    auto snapshotBcd = [&](const char* prop, const wchar_t* regName) {
        std::string val;
        bool present = BcdReadProperty(L"{current}", prop, val);
        std::wstring w;
        if (!present) {
            w = L"<absent>";
        } else {
            w.assign(val.begin(), val.end());
        }
        RegWriteString(hk, regName, w);
    };
    snapshotBcd("debug", L"BcdDebug");
    snapshotBcd("loadoptions", L"BcdLoadOptions");
    snapshotBcd("hypervisorlaunchtype", L"BcdHypervisorLaunchType");

    ::RegCloseKey(hk);
    LDIAG() << "[dcu] pre-install snapshot written";
}

// Save the firmware boot-entry GUID created by `bcdedit /copy {bootmgr}` so
// uninstall can target the exact entry instead of parsing bcdedit text output.
// guid is the literal "{xxxxxxxx-...}" string produced by bcdedit.
static void SaveFirmwareEntryGuid(const std::wstring& guid) {
    HKEY hk = OpenSnapshotKey(true);
    if (!hk) return;
    RegWriteString(hk, L"FirmwareEntryGuid", guid);
    ::RegCloseKey(hk);
}

static bool LoadFirmwareEntryGuid(std::wstring& guid) {
    HKEY hk = OpenSnapshotKey(false);
    if (!hk) return false;
    bool ok = RegReadString(hk, L"FirmwareEntryGuid", guid) && !guid.empty();
    ::RegCloseKey(hk);
    return ok;
}

// ── Marker file helpers ────────────────────────────────────────────────────

static bool MarkerExists() {
    return ::GetFileAttributesW(MARKER_FILE) != INVALID_FILE_ATTRIBUTES;
}

static bool ReadMarker(DcuMarker& out) {
    HANDLE h = ::CreateFileW(MARKER_FILE, GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD got = 0;
    bool ok = ::ReadFile(h, &out, sizeof(out), &got, nullptr) &&
              got == sizeof(out) && out.magic == 0x55434421;
    ::CloseHandle(h);
    return ok;
}

static bool WriteMarker(const DcuMarker& m) {
    // Ensure directory exists
    ::CreateDirectoryW(L"C:\\ProgramData\\Scootware", nullptr);
    ::CreateDirectoryW(MARKER_DIR, nullptr);

    HANDLE h = ::CreateFileW(MARKER_FILE, GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = ::WriteFile(h, &m, sizeof(m), &written, nullptr) &&
              written == sizeof(m);
    ::CloseHandle(h);

    // Set ACL: BUILTIN\Users can read
    // (simplified — production should use proper SDDL)
    return ok;
}

static bool DeleteMarker() {
    return ::DeleteFileW(MARKER_FILE) || ::GetLastError() == ERROR_FILE_NOT_FOUND;
}

// ── SHA-256 helper ─────────────────────────────────────────────────────────

static bool Sha256File(const std::wstring& path, uint8_t(&out)[32]) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;

    if (BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg,
        BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        DWORD hashObjLen = 0, hashLen = 0;
        ::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hashObjLen), sizeof(hashObjLen), &hashLen, 0);
        ::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &hashLen, 0);

        std::vector<uint8_t> hashObj(hashObjLen);
        std::vector<uint8_t> hashBuf(hashLen);

        if (BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash,
            hashObj.data(), (ULONG)hashObj.size(), nullptr, 0, 0))) {
            uint8_t buf[65536];
            DWORD got = 0;
            while (::ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
                ::BCryptHashData(hash, buf, got, 0);
            }
            ::BCryptFinishHash(hash, hashBuf.data(), (ULONG)hashBuf.size(), 0);
            ::BCryptDestroyHash(hash);
            memcpy(out, hashBuf.data(), 32);
            ok = true;
        }
        ::BCryptCloseAlgorithmProvider(alg, 0);
    }
    ::CloseHandle(h);
    return ok;
}

// ── ESP mount helpers ──────────────────────────────────────────────────────

// Mounts the EFI System Partition to the given drive letter using mountvol.
// Returns true if mounted, false on error.
static bool MountEsp(wchar_t drive) {
    std::wstring cmd = L"mountvol ";
    cmd.push_back(drive);
    cmd += L": /S";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!::CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    ::WaitForSingleObject(pi.hProcess, 10000);
    DWORD ec = 0;
    ::GetExitCodeProcess(pi.hProcess, &ec);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

// mountvol exits 0 on success
    return ec == 0;
}

// ── EFI Config Helper ──────────────────────────────────────────────────────

bool DcuWriteEfiConfig(const DcuEfiSettings& settings) {
    if (!MountEsp(L'X')) {
        return false;
    }

    // Prepare EFI/Boot directory
    std::wstring espDir = L"X:\\EFI\\Boot";
    ::CreateDirectoryW(L"X:\\EFI", nullptr);
    ::CreateDirectoryW(espDir.c_str(), nullptr);

    std::wstring cfgPath = espDir + L"\\scootware.cfg";

    SCOOTWARE_EFI_CONFIG cfg = {};
    cfg.Magic = SCOOTWARE_CFG_MAGIC;
    cfg.Version = SCOOTWARE_CFG_VERSION;
    cfg.Flags = SCOOTWARE_CFG_FLAG_FIRST_RUN;
    cfg.DseBypassMethod = settings.dseBypassMethod;
    cfg.WaitForKeyPress = settings.waitForKeyPress;

    RTL_OSVERSIONINFOW osInfo = { sizeof(osInfo) };
    if (RtlGetVersion(&osInfo) == 0) { // STATUS_SUCCESS
        cfg.OsMajorVersion = osInfo.dwMajorVersion;
        cfg.OsBuildNumber = osInfo.dwBuildNumber;
    }

    wcsncpy_s((wchar_t*)cfg.BootmgfwPath, 256, settings.bootmgfwPath, _TRUNCATE);

    cfg.Checksum = ScootwConfigChecksum(&cfg);

    bool ok = false;
    HANDLE h = ::CreateFileW(cfgPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = ::WriteFile(h, &cfg, sizeof(cfg), &written, nullptr) && written == sizeof(cfg);
        ::CloseHandle(h);
    }

    system("mountvol X: /D");
    return ok;
}

bool DcuReadEfiConfig(DcuEfiSettings& settings) {
    if (!MountEsp(L'X')) {
        return false;
    }

    std::wstring cfgPath = L"X:\\EFI\\Boot\\scootware.cfg";
    bool ok = false;

    HANDLE h = ::CreateFileW(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        SCOOTWARE_EFI_CONFIG cfg;
        DWORD bytesRead = 0;
        if (::ReadFile(h, &cfg, sizeof(cfg), &bytesRead, nullptr) && bytesRead == sizeof(cfg)) {
            if (cfg.Magic == SCOOTWARE_CFG_MAGIC && cfg.Version == SCOOTWARE_CFG_VERSION) {
                if (cfg.Checksum == ScootwConfigChecksum(&cfg)) {
                    settings.dseBypassMethod = cfg.DseBypassMethod;
                    settings.waitForKeyPress = cfg.WaitForKeyPress;
                    wcsncpy_s(settings.bootmgfwPath, 256, (const wchar_t*)cfg.BootmgfwPath, _TRUNCATE);
                    ok = true;
                }
            }
        }
        ::CloseHandle(h);
    }

    system("mountvol X: /D");
    return ok;
}

// ── EFI HWID Spoof helpers ──────────────────────────────────────────────────

// Shared helper: mount ESP, read and validate the config struct.
// Returns true and populates out on success. Caller owns the unmount.
static bool ReadRawEfiConfig(SCOOTWARE_EFI_CONFIG& out) {
    HANDLE h = ::CreateFileW(L"X:\\EFI\\Boot\\scootware.cfg",
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD got = 0;
    bool ok = ::ReadFile(h, &out, sizeof(out), &got, nullptr) &&
              got == sizeof(out) &&
              out.Magic == SCOOTWARE_CFG_MAGIC &&
              out.Version == SCOOTWARE_CFG_VERSION &&
              out.Checksum == ScootwConfigChecksum(&out);
    ::CloseHandle(h);
    return ok;
}

// Shared helper: write cfg to ESP (checksum is recomputed before write).
// ESP must already be mounted at X:.
static bool WriteRawEfiConfig(SCOOTWARE_EFI_CONFIG& cfg) {
    cfg.Checksum = ScootwConfigChecksum(&cfg);

    HANDLE h = ::CreateFileW(L"X:\\EFI\\Boot\\scootware.cfg",
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = ::WriteFile(h, &cfg, sizeof(cfg), &written, nullptr) &&
              written == sizeof(cfg);
    ::CloseHandle(h);
    return ok;
}

bool DcuReadEfiHwidConfig(DcuEfiHwidSpoof& out) {
    if (!MountEsp(L'X')) return false;

    SCOOTWARE_EFI_CONFIG cfg;
    bool ok = ReadRawEfiConfig(cfg);
    if (ok) {
        out.captured      = (cfg.HwIdCaptured != 0);
        out.applyPending  = (cfg.HwIdApply    != 0);
        memcpy(out.uuid, cfg.HwIdSpoofUUID, 16);
        memcpy(out.systemSerial,    cfg.HwIdSpoofSystemSerial,    128);
        memcpy(out.baseboardSerial, cfg.HwIdSpoofBaseboardSerial, 128);
        memcpy(out.processorSerial, cfg.HwIdSpoofProcessorSerial, 128);
        // Ensure null-termination for safety
        out.systemSerial[127]    = '\0';
        out.baseboardSerial[127] = '\0';
        out.processorSerial[127] = '\0';
    }

    system("mountvol X: /D");
    return ok;
}

bool DcuWriteEfiHwidConfig(const DcuEfiHwidSpoof& spoof) {
    if (!MountEsp(L'X')) return false;

    // Read the existing config to preserve DSE and other settings.
    // If reading fails (file absent or invalid), start from a clean default.
    SCOOTWARE_EFI_CONFIG cfg = {};
    if (!ReadRawEfiConfig(cfg)) {
        // Build a minimal valid config
        cfg.Magic   = SCOOTWARE_CFG_MAGIC;
        cfg.Version = SCOOTWARE_CFG_VERSION;
        cfg.Flags   = SCOOTWARE_CFG_FLAG_FIRST_RUN;
        cfg.DseBypassMethod = 3; // Auto
        RTL_OSVERSIONINFOW osInfo = { sizeof(osInfo) };
        if (RtlGetVersion(&osInfo) == 0) {
            cfg.OsMajorVersion = osInfo.dwMajorVersion;
            cfg.OsBuildNumber  = osInfo.dwBuildNumber;
        }
    }

    // Write HWID spoof values
    memcpy(cfg.HwIdSpoofUUID,            spoof.uuid,            16);
    memcpy(cfg.HwIdSpoofSystemSerial,    spoof.systemSerial,    128);
    memcpy(cfg.HwIdSpoofBaseboardSerial, spoof.baseboardSerial, 128);
    memcpy(cfg.HwIdSpoofProcessorSerial, spoof.processorSerial, 128);
    cfg.HwIdApply = 1; // Signal the module to apply on next read

    // Ensure serials are null-terminated within the field
    cfg.HwIdSpoofSystemSerial[127]    = '\0';
    cfg.HwIdSpoofBaseboardSerial[127] = '\0';
    cfg.HwIdSpoofProcessorSerial[127] = '\0';

    // Prepare directory in case this is a fresh ESP
    ::CreateDirectoryW(L"X:\\EFI", nullptr);
    ::CreateDirectoryW(L"X:\\EFI\\Boot", nullptr);

    bool ok = WriteRawEfiConfig(cfg);

    system("mountvol X: /D");
    return ok;
}

// ── DcuQueryReadiness — quick check ─────────────────────────────────────────

DcuReadinessResult DcuQueryReadiness() {
    DcuReadinessResult r;

    HKEY hk = OpenDcuKey(false);
    if (!hk) {
        // No registry key at all
        if (MarkerExists()) {
            DcuMarker m;
            if (ReadMarker(m) && m.postBootTimestamp > 0) {
                r.state = DcuState::Ready;
                r.flags |= DCU_FLAG_MARKER_FILE;
                r.flags |= DCU_FLAG_POSTBOOT_DONE;
                r.postBootTimestamp = m.postBootTimestamp;
                r.installTimestamp  = m.installTimestamp;
                r.version           = m.version;
                r.efiMethod         = m.efiMethod;
            } else {
                r.state = DcuState::Failed;
                r.errorDetail = "Marker file present but post-boot timestamp missing";
            }
        } else {
            r.state = DcuState::NotInstalled;
        }
        return r;
    }

    r.flags |= DCU_FLAG_REGISTRY;

    DWORD installed = 0;
    if (RegReadDword(hk, L"Installed", installed) && installed == 1) {
        DWORD dwVersion = 0, dwEfiMethod = 0;
        if (RegReadDword(hk, L"Version", dwVersion)) r.version = dwVersion;
        if (RegReadDword(hk, L"EfiMethod", dwEfiMethod)) r.efiMethod = dwEfiMethod;
        RegReadQword(hk, L"InstallTimestamp", r.installTimestamp);

        uint64_t postBoot = 0;
        if (RegReadQword(hk, L"PostBootLaunchTimestamp", postBoot) && postBoot > 0) {
            r.postBootTimestamp = postBoot;
            r.state = DcuState::Ready;
            r.flags |= DCU_FLAG_POSTBOOT_DONE;
        } else {
            r.state = DcuState::PendingReboot;
        }
    } else {
        r.state = DcuState::NotInstalled;
    }

    // Cross-check with marker file
    if (MarkerExists()) {
        DcuMarker m;
        if (ReadMarker(m)) {
            r.flags |= DCU_FLAG_MARKER_FILE;
            if (r.state == DcuState::PendingReboot && m.postBootTimestamp > 0) {
                // Marker says post-boot done but registry hasn't been updated
                // — EfiDSEFix might not have had registry write permission.
                // Trust the marker.
                r.state = DcuState::Ready;
                r.flags |= DCU_FLAG_POSTBOOT_DONE;
                r.postBootTimestamp = m.postBootTimestamp;
            }
        }
    }

    ::RegCloseKey(hk);
    return r;
}

// ── DcuQueryReadinessFull — includes ESP + UEFI vars ────────────────────────

DcuReadinessResult DcuQueryReadinessFull() {
    DcuReadinessResult r = DcuQueryReadiness();

    if (r.state != DcuState::Ready && r.state != DcuState::PendingReboot)
        return r;

    // Mount ESP and verify payload hashes
    if (!MountEsp(L'X')) {
        r.errorDetail = "Failed to mount ESP (run as admin)";
        return r;
    }

    std::wstring espPath = L"X:\\" + std::wstring(ESP_REL_DIR);
    std::wstring dxePath = espPath + L"\\" + ESP_EFI_DXE;

    if (::GetFileAttributesW(dxePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        r.errorDetail = "EfiGuardDxe.efi not found on ESP";
        r.state = DcuState::NotInstalled;
        // Dismount
        system("mountvol X: /D");
        return r;
    }

    r.flags |= DCU_FLAG_ESP_PAYLOADS;

    // If marker exists, verify payload hash
    if (MarkerExists()) {
        DcuMarker m;
        if (ReadMarker(m)) {
            uint8_t actual[32] = {};
            if (Sha256File(dxePath, actual)) {
                if (memcmp(actual, m.sha256, 32) == 0) {
                    // Hash matches — all good
                } else {
                    r.errorDetail = "ESP payload hash mismatch (tampered or update overwrote?)";
                    r.state = DcuState::Failed;
                }
            }
        }
    }

    // Dismount
    system("mountvol X: /D");
    return r;
}

// ── DcuInstall ──────────────────────────────────────────────────────────────

DcuState DcuInstall(const std::string& root,
                    const std::string& token,
                    const std::string& hwid,
                    DcuProgressFn progress)
{
    auto report = [&](DcuStep s, const char* msg, float p) {
        LDIAG() << "[dcu] " << msg << " (" << int(p * 100) << "%)";
        if (progress) progress(s, msg, p);
    };

    // ── Step 0: Snapshot pre-install state + audit BCD for known footguns ──
    //
    // The snapshot is what DcuUninstall reads back to do symmetric teardown.
    // The audit checks for BCD options that are known to interact badly with
    // the bootkit (kernel debug mode forces sync overhead on every DPC, which
    // compounds with the bootkit's runtime hooks and causes multi-second
    // freezes in graphics drivers post-uninstall).
    SnapshotPreInstallState();
    {
        std::string dbg, loadOpts;
        if (BcdReadProperty(L"{current}", "debug", dbg) && dbg == "Yes") {
            LDIAG() << "[dcu] WARNING: bcdedit {current} debug=Yes BEFORE install. "
                    << "Kernel debug mode will dramatically slow boot and graphics "
                    << "driver activity (NVIDIA App / display driver freezes). "
                    << "Run 'bcdedit /debug off' before installing if no kernel "
                    << "debugger is attached.";
        }
        if (BcdReadProperty(L"{current}", "loadoptions", loadOpts) && !loadOpts.empty()) {
            LDIAG() << "[dcu] WARNING: bcdedit {current} loadoptions is non-empty: '"
                    << loadOpts << "'. This is unusual and may slow boot.";
        }
    }

    // ── Step 1: Download the three compatibility module binaries separately ──
    report(DcuStep::DownloadAssets, "Downloading EfiGuardDxe.efi...", 0.00f);
    auto [dxeBlob, dxeAlloc] = Api::StreamAsset(OBF_S("SHARED"), OBF_S("dxe_efi"), token, hwid);
    if (dxeBlob.empty()) {
        std::string errDetail = "Failed to download EfiGuardDxe.efi: ";
        DWORD sc = Api::LastStreamStatusCode();
        if (sc == 0) {
            errDetail += "Network error (WinHTTP could not reach server or TLS handshake failed)";
        } else if (sc == 401) {
            errDetail += "HTTP 401 — session expired, please log in again";
        } else if (sc == 403) {
            errDetail += "HTTP 403 — HWID mismatch or access denied";
        } else if (sc == 404) {
            errDetail += "HTTP 404 — asset not found on server (upload under Shared Assets with type 'dxe_efi')";
        } else {
            errDetail += "HTTP " + std::to_string(sc);
            const std::string& body = Api::LastStreamErrorBody();
            if (!body.empty())
                errDetail += " — " + body.substr(0, (std::min)(body.size(), size_t(128)));
        }
        report(DcuStep::Failed, errDetail.c_str(), 0.0f);
        return DcuState::Failed;
    }

    report(DcuStep::DownloadAssets, "Downloading Loader.efi...", 0.07f);
    auto [loaderBlob, loaderAlloc] = Api::StreamAsset(OBF_S("SHARED"), OBF_S("loader_efi"), token, hwid);
    if (loaderBlob.empty()) {
        std::string errDetail = "Failed to download Loader.efi: ";
        DWORD sc = Api::LastStreamStatusCode();
        if (sc == 0) {
            errDetail += "Network error (WinHTTP could not reach server or TLS handshake failed)";
        } else if (sc == 401) {
            errDetail += "HTTP 401 — session expired, please log in again";
        } else if (sc == 403) {
            errDetail += "HTTP 403 — HWID mismatch or access denied";
        } else if (sc == 404) {
            errDetail += "HTTP 404 — asset not found on server (upload under Shared Assets with type 'loader_efi')";
        } else {
            errDetail += "HTTP " + std::to_string(sc);
            const std::string& body = Api::LastStreamErrorBody();
            if (!body.empty())
                errDetail += " — " + body.substr(0, (std::min)(body.size(), size_t(128)));
        }
        report(DcuStep::Failed, errDetail.c_str(), 0.0f);
        return DcuState::Failed;
    }

    report(DcuStep::DownloadAssets, "Downloading EfiDSEFix.exe...", 0.13f);
    auto [dsefixBlob, dsefixAlloc] = Api::StreamAsset(OBF_S("SHARED"), OBF_S("dsefix_exe"), token, hwid);
    if (dsefixBlob.empty()) {
        std::string errDetail = "Failed to download EfiDSEFix.exe: ";
        DWORD sc = Api::LastStreamStatusCode();
        if (sc == 0) {
            errDetail += "Network error (WinHTTP could not reach server or TLS handshake failed)";
        } else if (sc == 401) {
            errDetail += "HTTP 401 — session expired, please log in again";
        } else if (sc == 403) {
            errDetail += "HTTP 403 — HWID mismatch or access denied";
        } else if (sc == 404) {
            errDetail += "HTTP 404 — asset not found on server (upload under Shared Assets with type 'dsefix_exe')";
        } else {
            errDetail += "HTTP " + std::to_string(sc);
            const std::string& body = Api::LastStreamErrorBody();
            if (!body.empty())
                errDetail += " — " + body.substr(0, (std::min)(body.size(), size_t(128)));
        }
        report(DcuStep::Failed, errDetail.c_str(), 0.0f);
        return DcuState::Failed;
    }

    report(DcuStep::DownloadAssets, "Compatibility module downloaded OK", 0.20f);

    // ── Step 2: Check HVCI ───────────────────────────────────────────────
    report(DcuStep::CheckHvci, "Checking Memory Integrity (HVCI) status...", 0.25f);

    // Query HVCI via the registry mirror (same method the Mapper uses)
    BOOLEAN hvciEnabled = FALSE;
    {
        HKEY hk = nullptr;
        DWORD val = 0;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
            L"HypervisorEnforcedCodeIntegrity",
            0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS)
        {
            DWORD type = 0, size = sizeof(val);
            ::RegQueryValueExW(hk, L"Enabled", nullptr, &type,
                reinterpret_cast<BYTE*>(&val), &size);
            ::RegCloseKey(hk);
            hvciEnabled = (val != 0);
        }
    }

    if (hvciEnabled) {
        // Try to schedule HVCI off via policy-permitted path
        report(DcuStep::CheckHvci,
            "HVCI is ON — attempting to schedule disable...", 0.30f);

        bool regSuccess = false;
        HKEY hkHvci = nullptr;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
            L"HypervisorEnforcedCodeIntegrity",
            0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hkHvci) == ERROR_SUCCESS)
        {
            DWORD off = 0;
            if (::RegSetValueExW(hkHvci, L"Enabled", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&off), sizeof(off)) == ERROR_SUCCESS) {
                regSuccess = true;
                
                HKEY hkPol = nullptr;
                if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
                    L"HypervisorEnforcedCodeIntegrity\\Policy",
                    0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hkPol) == ERROR_SUCCESS)
                {
                    ::RegSetValueExW(hkPol, L"Enabled", 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&off), sizeof(off));
                    ::RegCloseKey(hkPol);
                }
                report(DcuStep::CheckHvci, "HVCI scheduled to disable via Registry", 0.35f);
            }
            ::RegCloseKey(hkHvci);
        }

        bool skciSuccess = false;
        if (!regSuccess) {
            report(DcuStep::CheckHvci, "Registry locked by Group Policy. Trying skci bypass...", 0.32f);
            TrustedInstallerIntegrator ti;
            
            wchar_t sysDir[MAX_PATH];
            if (::GetSystemDirectoryW(sysDir, MAX_PATH) != 0) {
                std::wstring srcPath = std::wstring(sysDir) + L"\\skci.dll";
                std::wstring dstPath = std::wstring(sysDir) + L"\\skci\u200B.dll";

                if (ti.RenameFileAsTrustedInstaller(srcPath, dstPath)) {
                    std::wstring srcP = std::wstring(L"\\??\\") + sysDir + L"\\skci\u200B.dll";
                    std::wstring dstP = std::wstring(L"\\??\\") + sysDir + L"\\skci.dll";
                    std::vector<wchar_t> multiString;
                    multiString.insert(multiString.end(), srcP.begin(), srcP.end());
                    multiString.push_back(L'\0');
                    multiString.insert(multiString.end(), dstP.begin(), dstP.end());
                    multiString.push_back(L'\0');
                    multiString.push_back(L'\0');

                    HKEY hkSm = nullptr;
                    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
                                      0, KEY_WRITE | KEY_WOW64_64KEY, &hkSm) == ERROR_SUCCESS) {
                        ::RegSetValueExW(hkSm, L"PendingFileRenameOperations", 0, REG_MULTI_SZ,
                            reinterpret_cast<const BYTE*>(multiString.data()),
                            static_cast<DWORD>(multiString.size() * sizeof(wchar_t)));
                        DWORD allowFlag = 1;
                        ::RegSetValueExW(hkSm, L"AllowProtectedRenames", 0, REG_DWORD,
                                        reinterpret_cast<const BYTE*>(&allowFlag), sizeof(DWORD));
                        ::RegCloseKey(hkSm);
                        skciSuccess = true;
                        report(DcuStep::CheckHvci, "HVCI disabled via skci rename", 0.35f);
                    } else {
                        // Revert rename if setting pending failed
                        ti.RenameFileAsTrustedInstaller(dstPath, srcPath);
                    }
                }
            }
        }

        bool efiSuccess = false;
        if (!regSuccess && !skciSuccess) {
            report(DcuStep::CheckHvci, "skci bypass failed. Trying EFIVbsPolicyDisabled fallback...", 0.34f);
            
            HANDLE hToken = nullptr;
            if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
                TOKEN_PRIVILEGES tp = {};
                tp.PrivilegeCount = 1;
                if (::LookupPrivilegeValueW(nullptr, L"SeSystemEnvironmentPrivilege", &tp.Privileges[0].Luid)) {
                    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                    ::AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
                }
                ::CloseHandle(hToken);
            }

            DWORD efiVal32 = 1;
            BOOLEAN efiVal8 = TRUE;
            
            // 0x3 = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
            if (::SetFirmwareEnvironmentVariableExW(L"VbsPolicyDisabled", L"{77fa9abd-0359-4d32-bd60-28f4e78f784b}", &efiVal32, sizeof(efiVal32), 0x00000003) ||
                ::SetFirmwareEnvironmentVariableExW(L"VbsPolicyDisabled", L"{77fa9abd-0359-4d32-bd60-28f4e78f784b}", &efiVal8, sizeof(efiVal8), 0x00000003) ||
                ::SetFirmwareEnvironmentVariableW(L"VbsPolicyDisabled", L"{77fa9abd-0359-4d32-bd60-28f4e78f784b}", &efiVal32, sizeof(efiVal32))) {
                efiSuccess = true;
                report(DcuStep::CheckHvci, "HVCI scheduled to disable via EFI VbsPolicyDisabled", 0.35f);
            }
        }

        if (!regSuccess && !skciSuccess && !efiSuccess) {
            report(DcuStep::Failed,
                "HVCI bypass failed (Registry, skci, and EFI methods exhausted). "
                "Open Windows Security > Device Security > Core Isolation "
                "and turn off Memory Integrity manually, then retry.", 0.0f);
            return DcuState::Failed;
        }
    } else {
        report(DcuStep::CheckHvci, "HVCI is already OFF", 0.35f);
    }

    // ── Step 3: Write EFI Config ─────────────────────────────────────────
    report(DcuStep::WriteEfiConfig, "Writing EFI configuration to ESP...", 0.38f);
    
    DcuEfiSettings settings;
    // Try to get actual bootmgfw.efi path using bcdedit
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE hRead, hWrite;
        if (::CreatePipe(&hRead, &hWrite, &sa, 0)) {
            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
            PROCESS_INFORMATION pi = {};
            wchar_t cmd[] = L"bcdedit /enum {bootmgr}";
            if (::CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 5000);
                ::CloseHandle(pi.hProcess);
                ::CloseHandle(pi.hThread);
            }
            ::CloseHandle(hWrite);
            char buf[2048] = {};
            DWORD bytesRead = 0;
            ::ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr);
            ::CloseHandle(hRead);
            
            std::string output(buf);
            size_t pathPos = output.find("path");
            if (pathPos != std::string::npos) {
                size_t efiPos = output.find("\\EFI\\", pathPos);
                if (efiPos != std::string::npos) {
                    size_t endPos = output.find("\n", efiPos);
                    if (endPos != std::string::npos) {
                        std::string efiPathStr = output.substr(efiPos, endPos - efiPos);
                        while (!efiPathStr.empty() && (efiPathStr.back() == '\r' || efiPathStr.back() == ' '))
                            efiPathStr.pop_back();
                        std::wstring mgfwPath(efiPathStr.begin(), efiPathStr.end());
                        wcsncpy_s(settings.bootmgfwPath, 256, mgfwPath.c_str(), _TRUNCATE);
                    }
                }
            }
        }
    }

    if (!DcuWriteEfiConfig(settings)) {
        report(DcuStep::Failed, "Failed to write scootware.cfg to ESP", 0.0f);
        return DcuState::Failed;
    }

    // ── Step 4: Mount ESP and stage payloads ─────────────────────────────
    report(DcuStep::StageEfiPayloads, "Mounting EFI System Partition...", 0.40f);

    if (!MountEsp(L'X')) {
        report(DcuStep::Failed,
            "Failed to mount ESP (run as administrator)", 0.0f);
        return DcuState::Failed;
    }

    // Create EFI/Boot directory if missing
    std::wstring espDir = L"X:\\" + std::wstring(ESP_REL_DIR);
    ::CreateDirectoryW(L"X:\\EFI", nullptr);
    ::CreateDirectoryW(espDir.c_str(), nullptr);

    // Write EfiGuardDxe.efi
    {
        std::wstring dest = espDir + L"\\" + ESP_EFI_DXE;
        HANDLE h = ::CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            report(DcuStep::Failed, "Failed to write EfiGuardDxe.efi to ESP", 0.0f);
            system("mountvol X: /D");
            return DcuState::Failed;
        }
        DWORD written = 0;
        ::WriteFile(h, dxeBlob.data(), (DWORD)dxeBlob.size(), &written, nullptr);
        ::CloseHandle(h);
    }

    // Write Loader.efi (optional — if present in asset)
    if (!loaderBlob.empty()) {
        std::wstring dest = espDir + L"\\" + ESP_EFI_LOADER;
        HANDLE h = ::CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ::WriteFile(h, loaderBlob.data(), (DWORD)loaderBlob.size(), &written, nullptr);
            ::CloseHandle(h);
        }
    }

    // Write EfiDSEFix.exe (optional)
    if (!dsefixBlob.empty()) {
        std::wstring dest = espDir + L"\\" + ESP_EFI_DSEFIX;
        HANDLE h = ::CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ::WriteFile(h, dsefixBlob.data(), (DWORD)dsefixBlob.size(), &written, nullptr);
            ::CloseHandle(h);
        }
    }

    // Compute SHA-256 of the DXE file for the marker
    uint8_t payloadHash[32] = {};
    Sha256File(espDir + L"\\" + ESP_EFI_DXE, payloadHash);

    report(DcuStep::StageEfiPayloads, "Payloads staged to ESP", 0.60f);

    // ── Step 4: Register UEFI boot entry ─────────────────────────────────
    report(DcuStep::RegisterBootEntry, "Registering UEFI boot entry...", 0.65f);

    // Try the loader entry method first (bcfg boot addp).
    // Fall back to driver entry if the ESP doesn't have Loader.efi or
    // if bcfg fails (some firmware needs addp vs add).
    //
    // We use a small helper that shells out to bcfg via the UEFI shell
    // or direct EFI variable manipulation. For simplicity, we write a
    // marker and the boot entry will be created by a lightweight UEFI
    // app on next boot. But the standard method is:
    //
    //   bcfg boot addp 0 Loader.efi "EfiGuard"
    //
    // On UEFI-class firmware we can also create the entry via SetFirmwareEnvironmentVariable.
    // We attempt the latter (no UEFI shell dependency):

    bool bootEntryCreated = false;

    // Generate an EFI boot entry variable
    // Format: Boot#### where #### is hex index. We use Boot0004 (pick a free slot).
    // The variable data is a EFI_LOAD_OPTION structure.
    //
    // For the simplified implementation, we use bootentry via the firmware
    // variable API. If this fails, we fall back to instructing the user.

    // EFI Global Variable GUID
    const uint8_t efiGlobalGuid[16] = {
        0x8B, 0xE4, 0xDF, 0x61, 0xCA, 0x93, 0x11, 0xD2,
        0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C
    };

    // File path for the loader: \EFI\Boot\Loader.efi
    std::wstring bootFilePath = std::wstring(L"\\EFI\\Boot\\") + ESP_EFI_LOADER;

    // Build a simple EFI_LOAD_OPTION:
    //   Attributes (4 bytes) = LOAD_OPTION_ACTIVE = 0x01
    //   Description (variable length) = L"EfiGuard"
    //   FilePathList (variable length) = FileDevicePath for the boot file
    //   OptionalData (0 bytes)
    //
    // In practice we set up the entry using a helper or instruct the user.
    // For our implementation, we use the NtSetSystemEnvironmentValueEx approach
    // via direct UEFI RT variable writing.

    // Since writing UEFI variables from userspace requires boot services
    // runtime access and certain privilege levels, we use the more reliable
    // approach of writing a RunOnce that calls bootentry.exe or bcfg.
    //
    // For v1, we:
    //   1. Write the boot entry using a known-index approach (Boot0004)
    //   2. Also update BootOrder to include it
    //
    // We try via SetFirmwareEnvironmentVariableW API:

    // Prepare Boot#### variable name
    wchar_t bootVarName[16] = L"Boot0004";

    // If SetFirmwareEnvironmentVariable fails (it will on most Windows
    // versions from userspace — needs SE_SYSTEM_ENVIRONMENT_NAME privilege),
    // we fall back to scheduling the boot entry via a post-boot tool.

    // For production, we will rely on a small bootentry.exe helper that gets
    // placed on ESP and run. For now, use bcdedit + mountvol + manual.
    // Actually, let's just schedule the bcfg command via RunOnce for first boot.

    // The simplest reliable approach: tell the user to use the UEFI Shell,
    // OR we generate a boot entry using the firmware variable API.
    // Let's attempt SetFirmwareEnvironmentVariable:

    BOOL fwOk = FALSE;
    {
        // ── bcdedit method for UEFI boot entry ────────────────────────────────
        std::wstring bcdPath = std::wstring(L"\\EFI\\Boot\\") + ESP_EFI_LOADER;
        
        // 1. Copy {bootmgr}
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE hRead, hWrite;
        ::CreatePipe(&hRead, &hWrite, &sa, 0);
        
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;
        
        PROCESS_INFORMATION pi = {};
        wchar_t cmdCopy[] = L"bcdedit /copy {bootmgr} /d \"Scootware Compatibility Module\"";
        if (::CreateProcessW(nullptr, cmdCopy, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            ::WaitForSingleObject(pi.hProcess, 5000);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
        }
        ::CloseHandle(hWrite);
        
        char buf[512] = {};
        DWORD bytesRead = 0;
        ::ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr);
        ::CloseHandle(hRead);
        
        std::string output(buf);
        size_t start = output.find('{');
        size_t end = output.find('}');
        if (start != std::string::npos && end != std::string::npos) {
            std::string guid = output.substr(start, end - start + 1);
            std::wstring wguid(guid.begin(), guid.end());
            
            STARTUPINFOW si2 = { sizeof(si2) };
            si2.dwFlags = STARTF_USESHOWWINDOW;
            si2.wShowWindow = SW_HIDE;

            // 2. Set path
            std::wstring cmdSetPath = L"bcdedit /set " + wguid + L" path " + bcdPath;
            if (::CreateProcessW(nullptr, &cmdSetPath[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si2, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 5000);
                ::CloseHandle(pi.hProcess); ::CloseHandle(pi.hThread);
            }
            
            // 3. Set device to X: (the currently mounted ESP)
            // bcdedit will resolve X: to the actual volume path.
            std::wstring cmdSetDev = L"bcdedit /set " + wguid + L" device partition=X:";
            if (::CreateProcessW(nullptr, &cmdSetDev[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si2, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 5000);
                ::CloseHandle(pi.hProcess); ::CloseHandle(pi.hThread);
            }

            // 4. Add to fwbootmgr displayorder
            std::wstring cmdOrder = L"bcdedit /set {fwbootmgr} displayorder " + wguid + L" /addfirst";
            if (::CreateProcessW(nullptr, &cmdOrder[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si2, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 5000);
                ::CloseHandle(pi.hProcess); ::CloseHandle(pi.hThread);
            }

            // Persist the GUID so DcuUninstall can target this exact entry
            // without parsing bcdedit text output (the previous text parser
            // could miss localized descriptions or extra braces).
            SaveFirmwareEntryGuid(wguid);

            bootEntryCreated = true;
        } else {
            bootEntryCreated = false;
        }
    }

    uint32_t efiMethodVal = 2;
    if (!bootEntryCreated) {
        // Boot entry will be created by the post-boot helper.
        // For now we note that the boot entry is pending user action.
        report(DcuStep::RegisterBootEntry,
            "Boot entry will be created by helper on next boot", 0.70f);
    } else {
        report(DcuStep::RegisterBootEntry, "UEFI boot entry registered", 0.70f);
    }

    // Dismount ESP
    system("mountvol X: /D");

    // ── Step 5: Schedule post-boot EfiDSEFix ─────────────────────────────
    report(DcuStep::SchedulePostBoot, "Scheduling EfiDSEFix post-boot...", 0.80f);

        // Use RunOnce
    HKEY hkRunOnce = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hkRunOnce) == ERROR_SUCCESS)
    {
        // EfiDSEFix --postboot
        std::wstring dsefixCmd = std::wstring(L"X:\\EFI\\Boot\\") + ESP_EFI_DSEFIX +
            L" --postboot";
        // But X: won't exist after reboot — we need to use the ESP's real path.
        // Let's instead copy EfiDSEFix.exe to a persistent location.
        std::wstring dsefixLocal = MARKER_DIR;
        dsefixLocal += L"\\EfiDSEFix.exe";

        // Ensure MARKER_DIR exists before writing
        ::CreateDirectoryW(L"C:\\ProgramData\\Scootware", nullptr);
        ::CreateDirectoryW(MARKER_DIR, nullptr);

        // Copy if not already there
        if (!dsefixBlob.empty()) {
            HANDLE hDst = ::CreateFileW(dsefixLocal.c_str(), GENERIC_WRITE, 0,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hDst != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                ::WriteFile(hDst, dsefixBlob.data(), (DWORD)dsefixBlob.size(),
                    &written, nullptr);
                ::CloseHandle(hDst);
            }
        }

        std::wstring runOnceCmd = dsefixLocal + L" --postboot";
        ::RegSetValueExW(hkRunOnce, L"ScootwareDcuDSEFix", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(runOnceCmd.c_str()),
            (DWORD)((runOnceCmd.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(hkRunOnce);

        report(DcuStep::SchedulePostBoot, "EfiDSEFix scheduled via RunOnce", 0.90f);
    } else {
        report(DcuStep::Failed, "Failed to write RunOnce entry", 0.0f);
        return DcuState::Failed;
    }

    // ── Write registry markers ───────────────────────────────────────────
    {
        HKEY hk = OpenDcuKey(true);
        if (hk) {
            uint64_t now = 0;
            ::GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&now));

            RegWriteDword(hk, L"Installed", 1);
            RegWriteDword(hk, L"Version", DCU_CURRENT_VERSION);
            RegWriteDword(hk, L"EfiMethod", efiMethodVal);
            RegWriteQword(hk, L"InstallTimestamp", now);
            RegWriteDword(hk, L"HvciWasScheduled", hvciEnabled ? 1 : 0);
            ::RegCloseKey(hk);
        }
    }

    // ── Write marker file ────────────────────────────────────────────────
    {
        DcuMarker m;
        m.version = DCU_CURRENT_VERSION;
        m.efiMethod = efiMethodVal;
        ::GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&m.installTimestamp));
        memcpy(m.sha256, payloadHash, 32);
        WriteMarker(m);
    }

    report(DcuStep::Done,
        "DCU installed successfully — restart to complete setup", 1.00f);

    return DcuState::PendingReboot;
}

// ── DcuUninstall ────────────────────────────────────────────────────────────

// Restore HVCI registry values from snapshot. If a value was absent before
// install we delete it; if it was present we write back the original DWORD.
// Uninstall must call this BEFORE removing REG_BASE (snapshot lives under it).
static void RestoreHvciFromSnapshot() {
    HKEY hk = OpenSnapshotKey(false);
    if (!hk) {
        LDIAG() << "[dcu] no snapshot subkey — skipping HVCI restore";
        return;
    }

    auto restore = [&](const wchar_t* presentName,
                       const wchar_t* valueName,
                       const wchar_t* hvciSubKey,
                       const wchar_t* hvciValueName) {
        DWORD present = 0, val = 0;
        if (!RegReadDword(hk, presentName, present)) return;
        RegReadDword(hk, valueName, val);

        HKEY hvci = nullptr;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, hvciSubKey, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, &hvci) != ERROR_SUCCESS) {
            // Key doesn't exist anymore — nothing to restore against.
            return;
        }
        if (present == 0) {
            // Was absent before install. Delete what we may have written.
            LONG ret = ::RegDeleteValueW(hvci, hvciValueName);
            LDIAG() << "[dcu] HVCI restore: deleted "
                    << ToNarrowForLog(hvciValueName) << " (rc=" << ret << ")";
        } else {
            LONG ret = ::RegSetValueExW(hvci, hvciValueName, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&val), sizeof(val));
            LDIAG() << "[dcu] HVCI restore: " << ToNarrowForLog(hvciValueName)
                    << " = " << val << " (rc=" << ret << ")";
        }
        ::RegCloseKey(hvci);
    };

    restore(L"HvciEnabledPresent", L"HvciEnabled",
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
            L"HypervisorEnforcedCodeIntegrity",
            L"Enabled");
    restore(L"HvciPolicyEnabledPresent", L"HvciPolicyEnabled",
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
            L"HypervisorEnforcedCodeIntegrity\\Policy",
            L"Enabled");

    ::RegCloseKey(hk);
}

// Audit-only diagnostic: compare current BCD state against the snapshot and
// log differences. We deliberately do NOT mutate debug/loadoptions/hypervisor
// because the install path doesn't set them — but if a user reports a slow
// post-uninstall boot the log will show what changed during the install
// window and what's currently unusual.
static void LogBcdDriftSinceInstall() {
    HKEY hk = OpenSnapshotKey(false);
    if (!hk) return;

    auto audit = [&](const char* prop, const wchar_t* regName,
                     const char* footgunIfYes) {
        std::wstring snap;
        if (!RegReadString(hk, regName, snap)) return;
        std::string cur;
        bool present = BcdReadProperty(L"{current}", prop, cur);
        std::string snapNarrow(snap.begin(), snap.end());
        std::string curRepr = present ? cur : "<absent>";
        if (curRepr != snapNarrow) {
            LDIAG() << "[dcu] BCD drift: " << prop
                    << " was '" << snapNarrow
                    << "' at install, now '" << curRepr << "'";
        }
        if (footgunIfYes && present && cur == "Yes") {
            LDIAG() << "[dcu] WARNING: bcdedit {current} " << prop
                    << "=Yes — " << footgunIfYes;
        }
    };
    audit("debug", L"BcdDebug",
          "kernel debug mode is on. If you did not enable a kernel "
          "debugger intentionally, run 'bcdedit /debug off' to fix "
          "slow boot and graphics-driver freezes.");
    audit("loadoptions", L"BcdLoadOptions", nullptr);
    audit("hypervisorlaunchtype", L"BcdHypervisorLaunchType", nullptr);

    ::RegCloseKey(hk);
}

bool DcuUninstall() {
    LDIAG() << "[dcu] DcuUninstall entered";

    // 1. Remove RunOnce entry
    HKEY hkRunOnce = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        0, KEY_SET_VALUE, &hkRunOnce) == ERROR_SUCCESS)
    {
        ::RegDeleteValueW(hkRunOnce, L"ScootwareDcuDSEFix");
        ::RegCloseKey(hkRunOnce);
    }

    // 2. Remove EFI files from ESP. Mount failure is fatal here: a silent
    // skip would leave Loader.efi / ScootwareCompatDxe.efi on the ESP and the
    // bootkit would re-load on every subsequent boot.
    bool espOk = true;
    if (!MountEsp(L'X')) {
        LDIAG() << "[dcu] ERROR: MountEsp failed in uninstall — ESP files NOT "
                << "removed. Re-run uninstall as Administrator, or delete "
                << "manually from <ESP>\\EFI\\Boot\\ "
                << "(Loader.efi, ScootwareCompatDxe.efi, EfiDSEFix.exe, "
                << "scootware.cfg).";
        espOk = false;
    } else {
        // Track per-file outcome so we don't claim success when a delete failed
        // (e.g. file in use by a still-loaded EfiGuardDxe runtime image).
        auto tryDel = [&](const wchar_t* path) {
            if (::GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                return; // already gone
            }
            if (::DeleteFileW(path)) {
                LDIAG() << "[dcu] deleted ESP file: " << ToNarrowForLog(path);
                return;
            }
            DWORD err = ::GetLastError();
            // Schedule delete on next reboot. If THIS also fails, the file is
            // truly stuck and the user will need WinRE/UEFI shell to remove it.
            if (::MoveFileExW(path, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
                LDIAG() << "[dcu] WARN: ESP file " << ToNarrowForLog(path)
                        << " in use (err=" << err
                        << ") — scheduled for delete on next reboot";
            } else {
                LDIAG() << "[dcu] ERROR: failed to delete " << ToNarrowForLog(path)
                        << " (err=" << err
                        << ") and could not schedule pending rename. "
                        << "Bootkit may persist. Delete manually from WinRE.";
                espOk = false;
            }
        };
        tryDel((std::wstring(L"X:\\EFI\\Boot\\") + ESP_EFI_DXE).c_str());
        tryDel((std::wstring(L"X:\\EFI\\Boot\\") + ESP_EFI_LOADER).c_str());
        tryDel((std::wstring(L"X:\\EFI\\Boot\\") + ESP_EFI_DSEFIX).c_str());
        tryDel(L"X:\\EFI\\Boot\\scootware.cfg");
        system("mountvol X: /D");
    }

    // 3. Remove marker file
    DeleteMarker();
    ::RemoveDirectoryW(MARKER_DIR);

    // 4. Remove local EfiDSEFix.exe copy
    std::wstring dsefixLocal = MARKER_DIR;
    dsefixLocal += L"\\EfiDSEFix.exe";
    ::DeleteFileW(dsefixLocal.c_str());

    // 5. Remove the firmware boot entry created by `bcdedit /copy {bootmgr}`.
    //    Primary path: read the snapshot GUID written at install time and
    //    delete that exact entry. Fallback: walk bcdedit text output looking
    //    for the description, in case the snapshot is absent (e.g. legacy
    //    install or registry was tampered with). The text fallback is brittle
    //    by design — only use it when there is no better signal.
    bool firmwareEntryDeleted = false;
    {
        std::wstring guid;
        if (LoadFirmwareEntryGuid(guid)) {
            DWORD ec = 0;
            std::string out;
            std::wstring delCmd = L"bcdedit /delete " + guid + L" /f";
            if (RunCaptureOutput(delCmd, ec, out) && ec == 0) {
                firmwareEntryDeleted = true;
                LDIAG() << "[dcu] deleted firmware entry " << ToNarrowForLog(guid);
            } else {
                LDIAG() << "[dcu] WARN: bcdedit /delete " << ToNarrowForLog(guid)
                        << " failed (ec=" << ec << "): " << out;
            }
        }

        if (!firmwareEntryDeleted) {
            // Text-parse fallback. Scan `bcdedit /enum firmware /v` for any
            // entry whose description contains our marker string. The parser
            // pairs each match with the nearest preceding `identifier` line.
            DWORD ec = 0;
            std::string output;
            if (RunCaptureOutput(L"bcdedit /enum firmware /v", ec, output) && ec == 0) {
                size_t descPos = output.find("Scootware Compatibility Module");
                while (descPos != std::string::npos) {
                    size_t idLine = output.rfind("\nidentifier", descPos);
                    if (idLine == std::string::npos) idLine = output.rfind("identifier", descPos);
                    if (idLine != std::string::npos) {
                        size_t startGuid = output.find('{', idLine);
                        size_t endGuid   = output.find('}', startGuid);
                        if (startGuid != std::string::npos && endGuid != std::string::npos
                            && startGuid < descPos) {
                            std::string guidA = output.substr(startGuid, endGuid - startGuid + 1);
                            std::wstring wguid(guidA.begin(), guidA.end());
                            DWORD ec2 = 0;
                            std::string out2;
                            RunCaptureOutput(L"bcdedit /delete " + wguid + L" /f", ec2, out2);
                            LDIAG() << "[dcu] fallback deleted firmware entry "
                                    << guidA << " (ec=" << ec2 << ")";
                            firmwareEntryDeleted = true;
                        }
                    }
                    descPos = output.find("Scootware Compatibility Module", descPos + 1);
                }
            }
        }
    }

    // 6. Restore HVCI registry values modified by the install-time bypass.
    //    Must run BEFORE we delete REG_BASE (snapshot lives under it).
    RestoreHvciFromSnapshot();

    // 7. Audit current BCD against snapshot. We don't mutate debug /
    //    loadoptions / hypervisorlaunchtype here because the install path
    //    doesn't set them — but logging drift makes post-uninstall slow-boot
    //    diagnosis trivial.
    LogBcdDriftSinceInstall();

    // 8. Remove our registry key (incl. the Snapshot subkey beneath it).
    //    RegDeleteKeyW only deletes empty keys on NT, so blow the subkey away
    //    explicitly first.
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, SNAPSHOT_SUBKEY);
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, REG_BASE);

    LDIAG() << "[dcu] DcuUninstall complete (espOk=" << espOk
            << ", firmwareEntryDeleted=" << firmwareEntryDeleted << ")";
    return espOk && firmwareEntryDeleted;
}

// ── DcuReadPostBootTimestamp ────────────────────────────────────────────────

uint64_t DcuReadPostBootTimestamp() {
    DcuMarker m;
    if (ReadMarker(m))
        return m.postBootTimestamp;
    return 0;
}
