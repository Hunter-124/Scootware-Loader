# DCU Ready Detection Contract (TASK 2)

## Purpose

Define the **exact readiness signals** that the Mapper preflight gate (TASK 3) and
the Loader DCU wizard (TASK 5) use to determine DCU state. A single contract shared
by both callers prevents incompatible detection logic.

---

## 1. Registry Key Layout (Fallback — both paths)

All DCU state is persisted under:

```
HKLM\SOFTWARE\Scootware\DriverCompatibility\
```

| Value                     | Type       | Meaning                                                                |
|---------------------------|------------|------------------------------------------------------------------------|
| `Installed`               | `DWORD`    | `1` = DCU has been installed. `0` or absent = not installed.            |
| `Version`                 | `DWORD`    | DCU version (major.minor packed, e.g. `0x0100` = v1.0).                |
| `EfiMethod`               | `DWORD`    | `1` = UEFI driver entry (`bcfg driver add`), `2` = loader boot entry   |
|                          |            | (`bcfg boot addp`), `3` = manual ESP copy (user placed files).         |
| `InstallTimestamp`        | `QWORD`    | `GetSystemTimeAsFileTime` at install completion.                        |
| `PostBootLaunchTimestamp` | `QWORD`    | Written by `EfiDSEFix.exe -d` after EfiGuard boot patching completes.  |
|                          |            | **Absent** = post-boot step not yet done (PendingReboot or Failed).    |
| `PostBootMethod`          | `DWORD`    | Which DSE bypass was confirmed post-boot: `1` = boot-time patch,       |
|                          |            | `2` = `SetVariable` hook active (detected by probing).                 |
| `HvciWasScheduled`        | `DWORD`    | `1` = HVCI was toggled off during install; `0` = HVCI was already off. |
| `UninstallPending`        | `DWORD`    | `1` = rollback requested but reboot needed (TASK 11).                  |

### Registry ACL

The key is created by the **elevated installer** (Loader). Default ACL grants
`KEY_READ` to `ALL APPLICATION PACKAGES` and `BUILTIN\Users` so the Mapper can
read it without elevation:

```
Win32 security descriptor (SDDL):
  O:BAG:SYD:(A;;KR;;;AU)(A;;KR;;;BU)(A;;KA;;;SY)(A;;KA;;;BA)

  AU = APPLICATION PACKAGE AUTHORITY\ALL APPLICATION PACKAGES
  BU = BUILTIN\Users
  SY = LOCAL SYSTEM
  BA = BUILTIN\Administrators
```

The Mapper's `DcuQueryReadiness()` reads via `RegOpenKeyExW(HKEY_LOCAL_MACHINE,
..., KEY_READ)` — succeeds from any process token.

---

## 2. Marker File (Primary — Mapper-elevated path)

Stable path:

```
C:\ProgramData\Scootware\DCU\ready.sig
```

### Format

Binary file, 64 bytes total:

| Offset | Size     | Field                 | Description                                      |
|--------|----------|-----------------------|--------------------------------------------------|
| 0      | 4        | Magic                 | `0x55434421` (`"!DCU"`)                         |
| 4      | 4        | Version               | DCU version that wrote this marker.              |
| 8      | 8        | InstallTimestamp       | `ULARGE_INTEGER` from `GetSystemTimeAsFileTime`. |
| 16     | 8        | PostBootTimestamp      | Written by `EfiDSEFix.exe` after successful run. |
| 24     | 4        | EfiMethod             | Same as registry `EfiMethod`.                    |
| 28     | 4        | Flags                 | Bit 0: HVCI was off at install time.             |
| 32     | 32       | SHA256                | SHA-256 of the concatenated `EfiGuardDxe.efi` +  |
|        |          |                       | `Loader.efi` payloads on ESP at install time.    |

### ACL

```
O:BAG:SYD:(A;;FR;;;AU)(A;;FR;;;BU)(A;;FA;;;SY)(A;;FA;;;BA)
```

`FILE_GENERIC_READ` for `BUILTIN\Users` — readable without elevation.
Only `SYSTEM` / `Administrators` can write or delete.

### Lifecycle

| Event                         | `ready.sig` state                                |
|-------------------------------|---------------------------------------------------|
| Before any DCU install        | Does **not** exist.                                |
| `DcuStageEfiPayloads()` (6)   | Created with magic + version + InstallTimestamp.  |
|                               | `PostBootTimestamp` field = 0.                    |
| `DcuSchedulePostBootCi()` (9) | `PostBootTimestamp` still 0 (not yet booted).     |
| `EfiDSEFix.exe -d` runs       | Loads file, writes `PostBootTimestamp`, writes    |
|                               | `PostBootMethod` to registry, updates SHA256.     |
| `DcuUninstall()` (TASK 11)    | Deleted.                                           |

---

## 3. ESP/UEFI Evidence (Primary — elevated path only)

Requires **SeBackupPrivilege** (admin token → mountvol `X: /S` succeeds).
Used by the **Loader installer** / **full check**; the Mapper **never** mounts
ESP directly — it relies on the registry + marker file.

### Files expected on ESP

```
<ESP>:\EFI\Boot\EfiGuardDxe.efi
<ESP>:\EFI\Boot\Loader.efi          (only if EfiMethod=2)
<ESP>:\EFI\Boot\EfiDSEFix.exe       (optional — staged for post-boot)
```

Verification: SHA-256 of each file is compared against the hash stored in
`ready.sig` at install time. The installer writes the expected hash; the
post-boot check compares actual file hashes against it.

### Boot entry check

- EfiMethod=1: UEFI driver entry exists (`bcfg driver list` contains
  `EfiGuardDxe`). Checked via `GetFirmwareEnvironmentVariable` /
  `NtQuerySystemInformation(SystemBootEnvironmentInformation)`.
- EfiMethod=2: UEFI boot entry exists (loader entry for `Loader.efi` pointing
  at `EFI/Boot/Loader.efi` on ESP).
- EfiMethod=3: No boot entry — user will manually select the loader from
  firmware boot menu. Detection relies on ESP file presence + registry marker
  only.

---

## 4. `DcuQueryReadiness()` API Contract

Exported from `Loader\src\security\dcu_detect.h` (new file — TASK 2 delivers the
header). Called by Mapper preflight (TASK 3) and Loader wizard (TASK 5).

```cpp
// dcu_detect.h

#pragma once
#include <cstdint>
#include <string>

// Bitmask returned in DcuReadinessResult.flags
enum DcuReadinessFlags : uint32_t {
    DCU_READINESS_NONE            = 0,
    DCU_READINESS_HVCI_OFF        = 1u << 0,  // HVCI confirmed off
    DCU_READINESS_ESP_PAYLOADS    = 1u << 1,  // EFI files present on ESP
    DCU_READINESS_BOOT_ENTRY      = 1u << 2,  // UEFI boot/driver entry exists
    DCU_READINESS_POSTBOOT_DONE   = 1u << 3,  // EfiDSEFix ran post-boot
    DCU_READINESS_MARKER_FILE     = 1u << 4,  // ready.sig present + valid
    DCU_READINESS_REGISTRY        = 1u << 5,  // registry marker present
};

enum class DcuState : uint8_t {
    Unknown        = 0,
    NotInstalled   = 1,
    Installing     = 2,   // only returned by IPC (TASK 4)
    PendingReboot  = 3,
    Ready          = 4,
    Failed         = 5,
};

struct DcuReadinessResult {
    DcuState     state;
    uint32_t     flags;             // bitmask of DcuReadinessFlags
    uint32_t     version;           // 0 if not installed
    uint32_t     efiMethod;         // 0 if unknown
    uint64_t     installTimestamp;
    uint64_t     postBootTimestamp; // 0 if post-boot not yet confirmed
    std::string  hvciReason;        // empty if HVCI OK, else human-readable
    std::string  errorDetail;       // empty unless state == Failed
};

// ── Query (non-elevated — registry + marker file only) ──────────────────────
//
// Reads HKLM registry keys + C:\ProgramData\Scootware\DCU\ready.sig.
// Does NOT mount ESP or query UEFI variables.
// Suitable for Mapper preflight check (TASK 3) — fast, no elevation needed
// beyond the admin token the mapper already has.
//
DcuReadinessResult DcuQueryReadiness();

// ── Full check (elevated — includes ESP mount + UEFI variable probe) ────────
//
// Calls DcuQueryReadiness(), then also:
//   1. Mountvol ESP to verify payload file hashes.
//   2. Query UEFI boot/driver entries.
// Returns the merged result. Only callable from an elevated (admin) process.
//
// Used by the Loader DCU wizard (TASK 5) as a confirming re-check before
// showing "Ready" or before initiating install.
//
DcuReadinessResult DcuQueryReadinessFull();
```

### State derivation logic

`DcuQueryReadiness()` implements:

```
if (registry absent AND marker absent):
    return Unknown       (caller should treat as NotInstalled)

if (registry.Installed != 1 AND marker absent):
    return NotInstalled

if (registry.Installed == 1):
    if (registry.PostBootLaunchTimestamp exists AND > 0):
        verify marker file:
            if (marker.PostBootTimestamp > 0):
                return Ready
            else:
                return Failed  (registry inconsistent with marker)
    else:
        return PendingReboot

if (marker file exists):
    if (marker.PostBootTimestamp > 0):
        return Ready
    else:
        return PendingReboot

return Unknown
```

`DcuQueryReadinessFull()` additionally confirms:

```
if (state == Ready):
    mountvol ESP
    verify payload SHA-256 against marker.sig
    if (mismatch):
        return Failed  (ESP tampered or install corrupted)
    verify UEFI boot entry exists
    if (EfiMethod=1/2 AND entry missing):
        return PendingReboot (boot entry was lost — re-run install)
```

---

## 5. Mapper vs Installer Paths

| Aspect                    | Mapper Preflight (TASK 3)               | Loader Installer / Wizard (TASK 5)            |
|---------------------------|-----------------------------------------|-----------------------------------------------|
| **Process token**         | Elevated (admin) — inherited from       | Elevated (admin) — manifest                   |
|                           | Loader hollowing; standalone also       | `requireAdministrator`.                       |
|                           | requires admin (`ntsupUserIsFullAdmin`) |                                               |
| **Detection API**         | `DcuQueryReadiness()`                   | `DcuQueryReadinessFull()`                     |
| **Reads ESP?**            | **No** — uses registry + marker file    | **Yes** — mountvol + hash verify              |
| **Reads UEFI vars?**      | **No**                                  | **Yes** — `GetFirmwareEnvironmentVariable`    |
| **Reads registry?**       | **Yes** — `KEY_READ` (HKLM)             | **Yes** — `KEY_READ` / `KEY_SET_VALUE`        |
| **Writes registry?**      | **No** — read-only check                | **Yes** — writes `Installed`, `PostBoot*`, etc.|
| **Writes marker file?**   | **No**                                  | **Yes** — creates `ready.sig` via TASK 6/9    |
| **Fallback on failure**   | Sends `DCU_RUN_WIZARD` via IPC (TASK 4) | Shows error panel + offers retry/rollback     |
| **Timeout before abort**  | 10 min (configurable via IPC negotiation)| N/A (blocking user interaction)               |

### Why Mapper never mounts ESP

1. **Complexity** — mountvol requires `SeBackupPrivilege` + volume management
   calls that add ~1-2 seconds and risk `ERROR_ACCESS_DENIED` on locked ESPs.
2. **Minimal signal needed** — the Mapper only needs to answer one question:
   *"Has the EfiGuard boot chain completed?"* The registry `PostBootLaunchTimestamp`
   alone answers that. The ESP contents only matter during install (TASK 6) and
   post-boot verification (which `EfiDSEFix.exe` does itself).
3. **Latency** — the Mapper preflight gate fires synchronously before KDU init.
   Every millisecond adds to perceived startup time. A registry read + file stat
   is ~500 μs; mountvol + hash is ~2000 ms.

---

## 6. KVC Coexistence Guard (per TASK 10)

`DcuQueryReadiness()` must explicitly check for KVC footprint before concluding
NotInstalled:

```
if (C:\Windows\drivers.ini exists):
    // This is KVC, NOT DCU. Do NOT infer DCU state from this file.
    clear KVC_INFERRED_DCU flag (internal)
    proceed with normal registry + marker check
```

The registry and marker file are **canonical DCU evidence**. `drivers.ini` is
never consulted for DCU state. The wizard text in TASK 5 must also distinguish:

- *"Driver Compatibility Utility (DCU) is not installed"* — normal NotInstalled
- *"Kernel Virtualization Check (KVC) footprint detected — this is not DCU"*
  — only if `drivers.ini` exists AND no DCU markers found.

---

## 7. Cross-State Detection Summary

| State          | Registry                          | Marker File              | ESP / UEFI (Full check)       | Mapper action                          |
|----------------|-----------------------------------|--------------------------|-------------------------------|----------------------------------------|
| Unknown        | Absent                            | Absent                   | No payloads, no boot entry    | Send DCU_RUN_WIZARD                    |
| NotInstalled   | `Installed` != 1 (or absent)      | Absent                   | No payloads, no boot entry    | Send DCU_RUN_WIZARD                    |
| PendingReboot  | `Installed`=1, `PostBoot*`=absent | Present, timestamp=0     | Payloads OK, boot entry OK    | Send DCU_RUN_WIZARD (no map — reboot)  |
| Ready          | `Installed`=1, `PostBoot*`>0      | Present, timestamp>0     | Payloads OK, entry OK         | **Proceed to KDU map**                 |
| Failed         | `Installed`=1, `PostBoot*`=absent | Present, timestamp=0     | Payloads/entry may be partial | Send DCU_RUN_WIZARD (recovery)         |
|                | + `errorDetail` may be set        | Or absent unexpectedly    | Or tampered hash              |                                         |
