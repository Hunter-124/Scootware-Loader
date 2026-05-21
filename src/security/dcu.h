#pragma once
//
// dcu.h — Driver Compatibility Utility
//
// DCU automates installation of the EfiGuard bootkit + EfiDSEFix so the
// Mapper can load unsigned drivers.  This header exposes the full API that
// the Loader wizard (TASK 5) calls and that the Mapper preflight (TASK 3)
// queries via exit-code signalling.
//
// Lifecycle (caller-side):
//
//   1. DcuQueryReadiness()        — quick check (registry + marker file)
//   2. if not Ready && not Failed:
//        DcuInstall(root, tok, hw)— streams "Other/Data" asset, stages to
//                                   ESP, registers UEFI boot entry, schedules
//                                   EfiDSEFix post-boot
//   3. DcuInstall sets PendingReboot state
//   4. After reboot: EfiGuard boots, EfiDSEFix runs → writes PostBootTimestamp
//   5. DcuQueryReadiness() now returns Ready
//   6. Rollback: DcuUninstall()
//

#include <cstdint>
#include <string>
#include <vector>

// ── State machine ───────────────────────────────────────────────────────────

enum class DcuState : uint8_t {
    Unknown       = 0,
    NotInstalled  = 1,
    Installing    = 2,   // transient — operation in progress
    PendingReboot = 3,
    Ready         = 4,
    Failed        = 5,
};

// Bitmask returned in DcuReadinessResult.flags
enum DcuReadinessFlags : uint32_t {
    DCU_FLAG_NONE          = 0,
    DCU_FLAG_HVCI_OFF      = 1u << 0,
    DCU_FLAG_ESP_PAYLOADS  = 1u << 1,
    DCU_FLAG_BOOT_ENTRY    = 1u << 2,
    DCU_FLAG_POSTBOOT_DONE = 1u << 3,
    DCU_FLAG_MARKER_FILE   = 1u << 4,
    DCU_FLAG_REGISTRY      = 1u << 5,
};

struct DcuReadinessResult {
    DcuState   state             = DcuState::Unknown;
    uint32_t   flags             = 0;
    uint32_t   version           = 0;
    uint32_t   efiMethod         = 0;    // 1=drv-entry, 2=loader-entry, 3=manual
    uint64_t   installTimestamp  = 0;
    uint64_t   postBootTimestamp = 0;
    std::string hvciReason;               // non-empty if HVCI blocking
    std::string errorDetail;              // non-empty if state == Failed
};

// ── EFI config settings (written to scootware.cfg on ESP) ──────────────────

struct DcuEfiSettings {
    uint32_t dseBypassMethod  = 3;    // 0=None 1=AtBoot 2=SetVarHook 3=Auto (default)
    bool     waitForKeyPress  = false;
    wchar_t  bootmgfwPath[256] = {};
};

bool DcuWriteEfiConfig(const DcuEfiSettings& settings);
bool DcuReadEfiConfig(DcuEfiSettings& settings);

// ── EFI HWID Spoof config (written to scootware.cfg for the Compatibility-Module) ──

struct DcuEfiHwidSpoof {
    uint8_t uuid[16];                    // Raw UUID bytes (Type 1 UUID)
    char    systemSerial[128];           // Type 1 system serial (null-terminated)
    char    baseboardSerial[128];        // Type 2 baseboard serial (null-terminated)
    char    processorSerial[128];        // Type 4 processor serial (null-terminated)
    bool    captured;                    // HwIdCaptured flag read from config
    bool    applyPending;                // HwIdApply flag read from config
};

// Read the HWID spoof fields from the current ESP config.
// Returns false if the ESP cannot be mounted or config is invalid/absent.
bool DcuReadEfiHwidConfig(DcuEfiHwidSpoof& out);

// Write HWID spoof values into the ESP config and set HwIdApply = 1.
// Preserves all existing config fields (DSE settings, etc.).
// Returns false if the ESP cannot be written.
bool DcuWriteEfiHwidConfig(const DcuEfiHwidSpoof& spoof);

// ── Install step identifiers (for progress callbacks) ──────────────────────

enum class DcuStep : uint8_t {
    Begin              = 0,
    DownloadAssets     = 1,   // stream "Other/Data" from API
    CheckHvci          = 2,   // verify HVCI is off or schedule off
    WriteEfiConfig     = 3,   // write scootware.cfg to ESP before payloads
    StageEfiPayloads   = 4,   // mount ESP + copy files
    RegisterBootEntry  = 5,   // bcfg boot addp or driver add
    SchedulePostBoot   = 6,   // RunOnce + EfiDSEFix.exe --postboot
    Done               = 7,
    Failed             = 8,
};

// Progress callback: step, human-readable status, 0.0–1.0
using DcuProgressFn = void(*)(DcuStep step, const char* status, float progress);

// ── Public API ──────────────────────────────────────────────────────────────

// Quick readiness check — reads registry + marker file only.
// No elevation needed beyond admin token.
DcuReadinessResult DcuQueryReadiness();

// Full readiness check — includes ESP mount + UEFI var probe.
// Requires elevated (admin) process.
DcuReadinessResult DcuQueryReadinessFull();

// Install DCU: stream asset, stage ESP, register boot entry, schedule post-boot.
// root     — productId that owns the "Other/Data" asset
// token    — session bearer token from login
// hwid     — machine HWID
// progress — optional callback for UI updates (called from worker thread)
// Returns: DcuState::PendingReboot on success, DcuState::Failed on error.
DcuState DcuInstall(const std::string& root,
                    const std::string& token,
                    const std::string& hwid,
                    DcuProgressFn progress = nullptr);

// Uninstall DCU: remove boot entry, EFI files, RunOnce, registry markers.
bool DcuUninstall();

// Utility: reads the marker file PostBootTimestamp (returns 0 if absent).
uint64_t DcuReadPostBootTimestamp();

// Mapper exit code constants — the mapper exits with one of these so the
// loader knows what happened.
constexpr int DCU_EXIT_SUCCESS      = 0;          // KDU map succeeded
constexpr int DCU_EXIT_FAILED       = 0xDC000001; // general failure
constexpr int DCU_EXIT_NEEDS_DCU    = 0xDC000002; // DCU not installed — loader must show wizard
constexpr int DCU_EXIT_CANCELLED    = 0xDC000003; // user cancelled
