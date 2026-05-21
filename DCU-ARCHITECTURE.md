# DCU — Driver Compatibility Utility: Architecture & State Machine

## Purpose

The Driver Compatibility Utility (DCU) automates the installation and verification
of the EfiGuard bootkit + EfiDSEFix post-boot DSE fix so that the Mapper can safely
load unsigned drivers via KDU. This document defines the **finite state machine**,
**detection surfaces**, **ordering contract**, and **non-goals** that all agents
(PRO, FLASH-A, FLASH-B) must share.

---

## State Machine

```
                    ┌──────────────┐
                    │   Unknown    │
                    └──────┬───────┘
                           │ check artifacts
                    ┌──────v───────┐
              ┌─────│ NotInstalled │◄────┐
              │     └──────┬───────┘     │
              │            │ user says   │
              │            │ "Install"   │
              │     ┌──────v───────┐     │
              │     │  Installing  │─────┤──► Failed
              │     └──────┬───────┘     │    (any step errors)
              │            │ all steps   │
              │            │ OK          │
              │     ┌──────v───────┐     │
              │     │PendingReboot │─────┤──► Failed
              │     └──────┬───────┘     │    (reboot timeout)
              │            │ user        │
              │            │ reboots     │
              │     ┌──────v───────┐     │
              │     │   Ready      │─────────► (proceed to KDU)
              │     └──────────────┘     │
              │                          │
              └──────────────────────────┘
```

### States

#### Unknown
- **Who sees it:** Loader (wizard first paint), Mapper (preflight gate)
- **Loader behavior:** No UI yet — first action is to scan artifacts.
- **Mapper behavior:** Preflight check runs `DcuQueryReadiness()` (TASK 2). Result
  `Unknown` is treated as **NotInstalled** — Mapper must trigger the wizard.
- **Machine checks:** None performed yet.

#### NotInstalled
- **Who sees it:** Loader wizard (first screen), Mapper preflight → IPC → wizard
- **Loader behavior:** Shows *"Driver Compatibility Utility is not installed.
  Would you like to install it?"* (Yes / No). No → signals `DCU_CANCEL` via IPC
  (TASK 4). Mapper exits non-success.
- **Mapper behavior:** `DcuQueryReadiness()` returns `{state: NotInstalled}`.
  Mapper sends `DCU_RUN_WIZARD` IPC and waits bounded (default 10 min timeout).
- **Machine checks:**
  - ESP (mountvol /S): **no** `EfiGuardDxe.efi` or `Loader.efi` under
    `EFI/Boot/`.
  - Registry: **no** `HKLM\SOFTWARE\Scootware\DriverCompatibility` keys.
  - BCD / boot entries: **no** EfiGuard UEFI driver or loader entry.
  - Marker file under stable path (see TASK 2): absent.

#### Installing
- **Who sees it:** Loader wizard (progress view)
- **Loader behavior:** Step-by-step progress bar + status list. Calls the
  following PRO APIs **in strict order**:
  | # | Step                      | API (TASK)               | Description                              |
  |---|---------------------------|--------------------------|------------------------------------------|
  | 1 | HVCI / Memory integrity   | `DcuQueryHvci()` (TASK 8)| Blocking — must be off or scheduled off  |
  | 2 | Stage EFI payloads to ESP | `DcuStageEfiPayloads()` (TASK 6)| Copy `EfiGuardDxe.efi`, `Loader.efi`, `EfiDSEFix.exe` |
  | 3 | Register boot entry       | `DcuRegisterBootEntry()` (TASK 7)| `bcfg` or equivalent — makes next boot run EfiGuard |
  | 4 | Schedule post-boot DSE fix| `DcuSchedulePostBootCi()` (TASK 9)| RunOnce + `EfiDSEFix.exe -d` |
- **Mapper behavior:** Blocked on IPC, polling `DCU_STATUS` every 500 ms.
  If `DCU_CANCEL` received — exits with code `DCU_E_CANCELLED`.
- **Machine checks:**
  - HVCI status via `DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity` reg
    mirror + WMI (TASK 8).
  - ESP writeability (mountvol).
  - Boot entry API availability (EFI boot manager / bcfg).
- **Transitions to:** `PendingReboot` (all steps OK), `Failed` (any step error).

#### PendingReboot
- **Who sees it:** Loader wizard (completion screen), Mapper preflight (still blocked)
- **Loader behavior:** Shows *"DCU installed successfully. You must restart your
  computer for changes to take effect. Restart now?"* (Yes / No). No → signals
  `DCU_CANCEL` . Yes → `InitiateSystemShutdownEx` (or equivalent) with reboot flag.
- **Mapper behavior:** Still blocked. `DCU_STATUS` returns `PendingReboot`.
  Mapper must NOT attempt KDU mapping — EfiGuard hasn't run yet.
- **Machine checks confirming state:**
  - ESP: payloads present + hash verified.
  - Registry: `HKLM\SOFTWARE\Scootware\DriverCompatibility\Installed` = `1`,
    `Version`, `EfiMethod` (boot entry type), `InstallTimestamp`.
  - Boot entry: EfiGuard entry present in UEFI boot list.
  - RunOnce: `EfiDSEFix.exe -d` scheduled.
- **Transition to Ready:** User reboots → firmware loads EfiGuard (UEFI driver
  or loader entry) → EfiGuard patches at boot → `EfiDSEFix.exe -d` runs
  post-boot → registry timestamp written.

#### Ready
- **Who sees it:** Mapper preflight (pass), Loader wizard (status display)
- **Loader behavior:** Shows *"DCU is ready. Driver compatibility enabled."*
- **Mapper behavior:** `DcuQueryReadiness()` returns `{state: Ready}`.
  Preflight gate **passes** → Mapper proceeds to KDU/provider init.
- **Machine checks:**
  - ESP: EfiGuard payloads present.
  - Registry marker: `Installed=1` + `PostBootSuccessTimestamp` present.
  - DSE status verification: either `g_CiOptions` reflects disabled DSE, or
    `SetVariable` backdoor is active (detectable via `NtSetSystemEnvironmentValueEx`
    probe — TASK 2).
- **What "Ready" means for KDU (ordering contract):**

```
 HVCI off path ──► EfiGuard boot chain ──► Post-boot EfiDSEFix / CI flags ──► MAP
     ↑                      ↑                          ↑
  TASK 8                TASK 6+7                   TASK 9
```
  All three must be satisfied. If HVCI is on and cannot be scheduled off, state
  is `Failed` with reason `HVCI_BLOCKING`.

#### Failed
- **Who sees it:** Loader wizard (error panel), Mapper preflight (exit)
- **Loader behavior:** Shows actionable error (e.g., *"Could not mount ESP at
  X:\ — access denied"*). Offers **Retry** or **Rollback** (TASK 11).
- **Mapper behavior:** `DCU_STATUS` returns `Failed` + reason string.
  Mapper logs via `MapperMainDiagWrite`, exits with code `DCU_E_FAILED`.
- **Machine checks:** Partial artifacts may exist. Rollback (TASK 11) must:
  - Remove EFI files from ESP.
  - Remove UEFI boot entry (best-effort).
  - Clear RunOnce/scheduled task.
  - Clear registry markers.
  - **Do not** re-enable HVCI (user decision).

---

## Sequence Diagram

```
Loader/Wizard                     Mapper                    System / UEFI
     |                              |                            |
     |    (proactive check or DCU_RUN_WIZARD IPC)                |
     |<--- DCU_RUN_WIZARD ---------|                            |
     |                              |                            |
     |== DCU Wizard session =======|                            |
     |                              |                            |
     |-- State: Unknown             |                            |
     |-- scan ESP + registry        |                            |
     |-- State: NotInstalled        |                            |
     |   "Install DCU? (Y/N)"      |                            |
     |   N ──► DCU_CANCEL ──► exit |                            |
     |   Y ──► continue            |                            |
     |                              |                            |
     |-- State: Installing          |                            |
     |   1. DcuQueryHvci() ────────|──── check HVCI ───────────►|
     |      ├─ off ─► OK           |                            |
     |      └─ on  ─► schedule off?|── (user guided) ──────────►|
     |   2. DcuStageEfiPayloads() ─|──── mountvol, copy ───────►| (ESP)
     |   3. DcuRegisterBootEntry() |──── bcfg add ─────────────►| (UEFI vars)
     |   4. DcuSchedulePostBootCi()|──── RunOnce + EfiDSEFix ──►| (registry)
     |                              |                            |
     |   [any error] ──► Failed    |                            |
     |                              |                            |
     |-- State: PendingReboot       |                            |
     |   "Restart now? (Y/N)"      |                            |
     |   N ──► DCU_CANCEL ──► exit |                            |
     |   Y ──► ExitWindowsEx() ────|────── REBOOT ────────────►|
     |                              |                            |
     |                              |       EfiGuard loads at    |
     |                              |       boot, patches        |
     |                              |                            |
     |                              |       EfiDSEFix -d runs    |
     |                              |       post-boot            |
     |                              |                            |
     |                              |-- State: Ready             |
     |                              |   proceed to KDU map ─────►| (driver load)
     |                              |                            |
```

---

## Detection Contract Summary (TASK 2 boundary)

| Surface                  | Primary                            | Fallback                                   |
|--------------------------|------------------------------------|--------------------------------------------|
| **NotInstalled**         | ESP: no EFI payloads               | Registry: key absent                       |
| **PendingReboot**        | ESP: payloads + hash OK            | Registry: `Installed=1`, no `PostBoot*`    |
|                          | Boot entry present                 |                                            |
|                          | RunOnce for EfiDSEFix scheduled    |                                            |
| **Ready**                | ESP: payloads + hash OK            | Registry: `Installed=1` + `PostBootSuccess`|
|                          | Boot entry present                 | Marker file under `C:\ProgramData\Scootware\DCU\ready.sig` |

---

## Non-Goals

1. **DCU does not treat `C:\Windows\drivers.ini` as EfiGuard evidence.**
   That file is the footprint of Kernel Virtualization Check (KVC), a separate
   mechanism. Wizard text and detection code must never infer DCU from KVC alone
   (see TASK 10 for explicit guard).

2. **DCU does not manage HVCI at OS policy level.**
   `DcuTryScheduleHvciOff()` is policy-permitted only. If HVCI is Group Policy
   enforced, DCU warns and fails — it does not override.

3. **DCU does not modify Secure Boot state.**
   EfiGuard can work with Secure Boot enabled (custom PK/db), but DCU will only
   surface a note per EfiGuard README. Secure Boot modification is out of scope.

4. **DCU does not replace KDU.**
   The Mapper's KDU/provider init path is unchanged. DCU only gates access:
   preflight check → pass = proceed, fail = abort.

5. **DCU does not protect against HVCI post-boot detection.**
   Per EfiGuard README: *"EfiGuard can coexist with HVCI … but this is not useful
   in practice because HVCI will catch what PatchGuard did previously."* If HVCI
   re-enables after boot, the DSE bypass is invisible to user-mode callers but
   HVCI will still block unsigned driver execution. The ordering contract
   requires HVCI off before mapping.
