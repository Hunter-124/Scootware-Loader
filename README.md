# Scootware-Loader

A usermode loader application that communicates with the **Scootware kernel driver** (`Scootware-Driver`) to enable kernel-level memory read/write operations, arbitrary DLL injection into target processes, and hardware identity (HWID) spoofing — bypassing anticheat protections such as Easy Anti-Cheat (EAC).

The loader spawns `scootware.exe`, a hollowed IPC helper process that serves as the usermode ↔ kernel communication bridge via shared memory. It streams encrypted payload DLLs into target applications using a page-table (PT) injector and includes a Driver Compatibility Utility (DCU) for automated bootkit-assisted driver circumvention.

---

## Table of Contents

- [Project Structure](#project-structure)
- [Dependencies](#dependencies)
- [Build Instructions](#build-instructions)
- [How It Works](#how-it-works)
- [Known Issues / Limitations](#known-issues--limitations)
- [Usage Guidance](#usage-guidance)
- [Disclaimer](#disclaimer)
- [Attribution](#attribution)
- [License](#license)

---

## Project Structure

```
Scootware-Loader/
├── CMakeLists.txt              # Build configuration (MSVC, CMake 3.10+)
├── build.bat                   # Convenience batch build script
├── DCU-ARCHITECTURE.md         # DCU state machine and protocol design
├── DCU-DETECTION-CONTRACT.md   # DCU readiness detection contract (TASK 2)
├── imgui-master/               # Dear ImGui (bundled, D3D11 backend)
├── IPC-dependencies/
│   ├── ipc_config.h            # IPC tuning parameters (timeouts, app name)
│   └── shared_memory_ipc.h     # Shared memory IPC protocol (IPC v2, multi-slot)
├── LICENSE                     # AGPL v3
└── src/
    ├── api/                    # Network API layer (auth, asset streaming, heartbeat)
    │   ├── api.cpp / api.h
    │   └── session.cpp / session.h
    ├── host/                   # Hollow target process (scootware.exe)
    │   ├── host.cpp            # Dual-mode: probe or hollow target
    │   ├── host_console.cpp
    │   └── host_probe_protocol.h
    ├── memory/
    │   ├── runpe.cpp / runpe.h # Process hollowing (RunPE) engine
    ├── reverse-attempt/        # Reverse engineering artifacts and RE tools
    │   └── (reverse-attempt.rep/ — RE project files, Ghidra/IDA outputs)
    ├── security/
    │   ├── common.cpp / common.h
    │   ├── dcu.cpp / dcu.h     # Driver Compatibility Utility (install, query, rollback)
    │   ├── driver_bringup.cpp  # Kernel driver mapping (KDU/provider init)
    │   ├── handoff.cpp         # Auth token handoff to hollowed child (env + shmem)
    │   ├── loader_ipc.cpp      # In-process IPC between loader and kernel driver
    │   ├── loader_ipc.h
    │   ├── obf.h               # Compile-time string obfuscation macro layer
    │   ├── skCrypter.h         # XOR-based constexpr crypter (skadro)
    │   ├── TrustedInstallerIntegrator.cpp/.h
    │   └── WmiDefenderClient.cpp/.h
    ├── util/
    │   ├── diaglog.h
    │   ├── process_wait.cpp    # Cold-start host process detection
    │   └── process_wait.h
    ├── vmdetect.cpp / vmdetect.h  # VM / sandbox / anti-debug detection
    ├── hwid.cpp / hwid.h           # HWID fingerprinting + spoofing
    ├── image_loader.cpp / image_loader.h  # Async D3D11 texture loading
    ├── main.cpp                  # Entry point, ImGui UI, D3D11 device setup
    └── ScootwareLoader.manifest  # UAC elevation manifest
```

---

## Dependencies

| Dependency | Purpose | Sourcing |
|---|---|---|
| **Dear ImGui** (`imgui-master/`) | Immediate-mode GUI for the loader's UI (login, product library, wizard, injecting screen) | Bundled as a git submodule under `imgui-master/` |
| **Shared Memory IPC** (`IPC-dependencies/`) | Usermode ↔ kernel driver communication via `IPC_MEMORY` ring buffer (IPC v2, multi-slot, 16 concurrent slots) | Bundled in `IPC-dependencies/` |
| **Chromium Embedded Framework (CEF)** | IPC interface between the usermode loader frontend and the kernel communication backend | External dependency; must be installed separately |
| **CMake** ≥ 3.10 | Build system generation | System-level |
| **MSVC / Visual Studio** | Compiler and toolchain (the build targets MSVC-specific flags and Win32 APIs) | System-level |
| **Windows SDK** | Win32 / D3D11 / CNG / BCrypt / WinHTTP APIs | System-level |

> **Note:** The CMake configuration also links against `d3d11`, `d3dcompiler`, `dxgi`, `user32`, `gdi32`, `crypt32`, `shell32`, `winhttp`, `windowscodecs`, and `ole32` from the Windows SDK.

---

## Build Instructions

### Prerequisites

- Windows with Visual Studio (MSVC toolchain)
- CMake 3.10 or later on `PATH`
- Git (for cloning the repository)

### Quick Build

```bat
build.bat
```

This script:
1. Sources the build environment from `..\build\lib\env.bat`
2. Runs CMake configuration (`cmake -S . -B build`)
3. Compiles two targets in Release configuration:
   - `ScootwareLoader` → `build\Release\scootware-loader.exe` (the main loader)
   - `ScootwareHost` → `build\Release\scootware.exe` (the hollow IPC host)
4. Copies both binaries to the `bin/` output directory

### Manual Build

```bash
# Configure
cmake -S . -B build

# Build loader
cmake --build build --config Release --target ScootwareLoader

# Build hollow host
cmake --build build --config Release --target ScootwareHost
```

### Build Output

| Binary | Description |
|---|---|
| `scootware-loader.exe` | Main usermode loader (requires admin/UAC elevation) |
| `scootware.exe` | Hollow IPC host process, named `scootware.exe` so the kernel driver discovers it via its process scan |

### Release Hardening

Release builds apply the following MSVC compiler/linker flags to resist static analysis (IDA, Ghidra, Binary Ninja):

- `/O2`, `/Ob2`, `/Oi`, `/Os` — aggressive optimization
- `/GL` + `/LTCG` — whole-program optimization and link-time code generation
- `/Gy` + `/OPT:REF` / `/OPT:ICF` — function-level linking and identical COMDAT folding
- `/DYNAMICBASE`, `/NXCOMPAT`, `/HIGHENTROPYVA` — ASLR / DEP / high-entropy ASLR
- `/guard:cf` — Control Flow Guard
- `/DEBUG:NONE` — no PDB, no debug directory entries
- Static CRT (`/MT`) — no external CRT DLL footprint
- String encryption via `obf.h` / `skCrypter` — sensitive strings are XOR-obfuscated at compile time and decrypted at runtime

---

## How It Works

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        usermode (loader)                               │
│                                                                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │
│  │  ImGui   │  │   API    │  │  DCU     │  │  LoaderIpc (in-image)│  │
│  │  UI      │  │  (auth,  │  │  Wizard  │  │  ┌─────────────────┐ │  │
│  │  (D3D11) │  │   asset  │  │  (EfiGuard│  │  │ IPC_MEMORY      │ │  │
│  │          │  │  stream) │  │   + DSE)  │  │  │ ┌─slot[0]──────┐│ │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  │  │ │ CMD_PING/    ││ │  │
│       │              │             │          │  │ │ CMD_INJECT/  ││ │  │
│       │              │             │          │  │ │ CMD_READ/    ││ │  │
│       │              │             │          │  │ │ CMD_WRITE/   ││ │  │
│       │              │             │          │  │ │ CMD_HWID_*   ││ │  │
│       │              │             │          │  │ └─────────────────┘│ │  │
│       │              │             │          └──────────────────────┘  │  │
│       │              │             │                                     │  │
│       ▼              ▼             ▼                                     │  │
│  ┌──────────┐  ┌──────────┐  ┌─────────────────────────────────────┐   │  │
│  │ Session  │  │  RunPE   │  │  Shared Memory (IPC_MEMORY)         │   │  │
│  │ Storage  │  │  Engine  │  │  Mapped into both loader and driver │   │  │
│  └──────────┘  └──────────┘  └─────────────────────────────────────┘   │  │
└─────────────────────────────────────────────────────────────────────────┘
                         │                    │
                         │  CreateProcess /   │
                         │  lpEnvironment     │
                         ▼                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     usermode (scootware.exe)                           │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Hollow target / Probe process                                    │  │
│  │                                                                   │  │
│  │  Probe mode: allocates page-aligned IPC buffer with IPC_MAGIC,  │  │
│  │  sends CMD_PING to driver, optionally CMD_SHUTDOWN, exits with   │  │
│  │  status code.                                                    │  │
│  │                                                                   │  │
│  │  Hollow mode: spawned suspended by RunPE; main() overwritten;    │  │
│  │  infinite Sleep() acts as a safety net for misconfigured launches.│  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                         │
              ┌──────────┴──────────┐
              │  Kernel Driver      │
              │  (Scootware-Driver) │
              │                     │
              │  • Scans system     │
              │    process list for │
              │    "scootware.exe"  │
              │  • Finds IPC buffer │
              │    in .bss via      │
              │    IPC_MAGIC scan    │
              │  • Processes CMD_*  │
              │    commands         │
              │  • CR3-based read/  │
              │    write to bypass  │
              │    EAC protections   │
              │  • PT injector for  │
              │    DLL mapping       │
              │  • HWID spoofing    │
              │    (SMBIOS/UUID/    │
              │     serial/MAC/     │
              │     volume serial)   │
              └─────────────────────┘
```

### IPC Communication Flow

1. **Handshake** — The loader initializes an in-image `IPC_MEMORY` buffer (`__declspec(align(4096))`) embedded in its `.bss` section. The kernel driver scans the target process's address space in 4 KiB strides looking for `IPC_MAGIC` (`0x504D585F49504332` = `'PMX_IPC2'`). When found, it maps the buffer and begins polling for commands on slot 0.

2. **Driver Discovery** — The driver's discovery thread periodically scans process names. When it finds a process named `scootware.exe` (matching `IPC_APP_NAME`), it checks whether that process's memory contains the IPC magic. If so, it locks onto that process.

3. **Command Dispatch** — The loader sends commands via `IPC_SLOT` shared structure fields:
   - `CMD_PING` — verify the driver is present and responsive
   - `CMD_READ_MEMORY` / `CMD_WRITE_MEMORY` — CR3-based kernel memory R/W (bypasses EAC's usermode hooks)
   - `CMD_INJECT_DLL` — PT injector maps a payload DLL into a target process and runs `DllMain` via `RtlCreateUserThread`
   - `CMD_HWID_SPOOF` / `CMD_HWID_RESTORE` — spoof or restore hardware identity fields
   - `CMD_HANDOFF` — pre-announce the cheat process PID and IPC VA to the driver before releasing the loader's IPC

4. **Handoff Shutdown Sequence** — Once the cheat process (`scootware.exe`) is spawned and its IPC buffer is established, the loader calls `Release()` to zero the magic bytes and closes its session. The driver detects the original target exit, then re-scans and picks up the new `scootware.exe` process automatically.

5. **Auth Handoff** — The loader passes session credentials (`SW_S`, `SW_P`, `SW_E`) to the hollowed child process via two parallel channels:
   - Inherited environment block (explicit `lpEnvironment` in `CreateProcessA`) — primary path
   - Named file-mapping (`Local\SW_HANDOFF_<loader_pid>`) — fallback path
   Both channels use HWID-keyed XOR wrap for the token to prevent casual sniffing.

### DCU (Driver Compatibility Utility)

The DCU automates the installation of the **EfiGuard** bootkit and **EfiDSEFix** post-boot DSE bypass so the mapper can load unsigned kernel drivers via KDU. It operates as a five-state finite state machine:

| State | Meaning |
|---|---|
| `Unknown` | No artifacts found — shows install prompt |
| `NotInstalled` | DCU has never been installed |
| `Installing` | In-progress installation (HVCI off, ESP staging, boot entry, post-boot scheduling) |
| `PendingReboot` | Installation succeeded; reboot required |
| `Ready` | EfiGuard boot chain complete; mapper can proceed |
| `Failed` | Installation failed; offers retry or rollback |

On first launch, the loader queries registry keys (`HKLM\SOFTWARE\Scootware\DriverCompatibility`) and a marker file (`C:\ProgramData\Scootware\DCU\ready.sig`) to determine the current state. If `Ready` is not detected, the user is prompted to run the DCU wizard, which:
1. Streams the EfiGuard package from the API (`Other/Data` asset)
2. Verifies HVCI is off (or schedules it off)
3. Writes `scootware.cfg` to the ESP
4. Stages EFI payloads (`EfiGuardDxe.efi`, `Loader.efi`, `EfiDSEFix.exe`) to the ESP
5. Registers a UEFI boot entry via `bcfg`
6. Schedules `EfiDSEFix.exe -d` via RunOnce for post-boot execution

---

## Known Issues / Limitations

- **Windows-only** — The entire codebase targets the Win32 API, D3D11, and Windows-specific kernel interfaces. No Linux or macOS support exists or is planned.
- **MSVC-only build** — The CMake configuration applies MSVC-specific release hardening flags (`/O2`, `/GL`, `/guard:cf`, etc.) and uses MSVC intrinsics (`__cpuid`, `__rdtsc`, `_mm_clflush`, `_mm_sfence`). Building with GCC or Clang on Windows may require flag adjustments.
- **Visual Studio integration** — The `.vcxproj` / `.sln` files are not committed; a clean CMake reconfiguration is required each time the project is checked out on a new machine.
- **CEF dependency not yet integrated** — The CEF dependency is listed in the project spec but the build system does not yet link or include CEF headers/libraries. The current IPC implementation relies entirely on shared memory (`IPC_MEMORY`). CEF integration is a future task.
- **Anti-cheat fingerprinting** — The VM/anti-debug detection suite (`vmdetect.cpp`) is comprehensive but not infallible. Sophisticated hypervisors (e.g., those with EPT shadowing) and kernel-level anti-cheat may evade some vectors. The `drivers.ini` KVC coexistence guard is explicitly excluded from DCU state inference to avoid false positives.
- **HVCI dependency** — DCU installation requires HVCI (Memory Integrity) to be off. If HVCI is enforced via Group Policy, the DCU wizard fails and cannot proceed.
- **Single-slot usage** — While the IPC protocol supports 16 concurrent slots, the loader uses only slot 0 for all operations. Multi-slot concurrency is not utilized by the current implementation.
- **No telemetry persistence** — VM/detection events and loader events are reported to the backend via HTTP POST but are not persisted locally for offline analysis.
- **Asset encryption key management** — The AES-256-GCM decryption key is derived on the client side from the `ASSET_ENCRYPTION_SECRET` environment variable and the machine's HWID. If the environment variable is not set or the HWID does not match the server's record, asset decryption fails with HTTP 403.

---

## Usage Guidance

This software is intended **strictly for educational and research purposes**. It demonstrates techniques in:

- Kernel-mode usermode IPC via shared memory
- Process hollowing (RunPE) for defensive security research
- CR3-based kernel memory access and anticheat bypass analysis
- UEFI bootkit deployment and DSE circumvention
- Hardware fingerprinting and spoofing

The code should **not** be used to cheat in online multiplayer games. Doing so violates the Terms of Service of virtually every online game and cheats platform (Easy Anti-Cheat, BattlEye, Vanguard, FACEIT, EAC, etc.) and may result in permanent account bans, hardware bans, and potential legal action from game publishers or anti-cheat vendors.

**The authors and contributors assume no liability for any misuse of this software.**

### Quick Start (Educational)

1. Clone the repository and initialize git submodules:
   ```bash
   git clone https://github.com/<org>/Scootware-Loader.git
   cd Scootware-Loader
   git submodule update --init --recursive
   ```
2. Build:
   ```bash
   build.bat
   ```
3. Run `scootware-loader.exe` as Administrator from the `bin/` directory.
4. Log in with valid credentials (the loader connects to `scootware.us` on port 443).
5. Use the DCU wizard to install the EfiGuard bootkit if needed.
6. Launch a product to observe the injection and kernel communication pipeline.

> **Note:** A valid Scootware account, active subscription, and a machine that passes DCU readiness checks are required for the full pipeline to function.

---

## Disclaimer

This project is an **educational research tool**. It implements techniques that are also used by malicious software (kernel drivers, process hollowing, anticheat bypasses, bootkits). The authors:

- Do **not** condone or support the use of this software to cheat in games.
- Do **not** encourage any activity that violates the Terms of Service of any online platform, game publisher, or anti-cheat vendor.
- Bear **no responsibility** for any damage, account loss, hardware ban, or legal consequence resulting from misuse of this code.

By using this software, you acknowledge that you understand these risks and accept full responsibility for your actions.

---

## Attribution

- **Dear ImGui** — Copyright (c) 2011-2024 Omar Cornut and ImGui contributors — [github.com/ocornut/imgui](https://github.com/ocornut/imgui)
- **skCrypter** — Copyright (c) 2020 skadro — Compile-time string crypter used via `obf.h`
- **GNU C Library** (for `skCrypter`'s `std::remove_reference` backport in kernel-mode mode)
- All other code in this repository is original work unless otherwise noted in file headers.

---

## License

This program is free software: you can redistribute it and/or modify
it under the terms of the **GNU Affero General Public License** as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but **WITHOUT ANY WARRANTY**; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.

The full license text is in `LICENSE` in this repository.
