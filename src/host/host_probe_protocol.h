#pragma once
//
// host_probe_protocol.h
//
// Tiny contract shared between the hollow-target host (host.cpp) and the
// loader's DriverBringup module. It exists so we can do a one-shot
// "is the kernel driver already loaded?" check (and optionally tell an
// existing driver to unload itself) WITHOUT the loader ever having to
// touch IPC directly.
//
// Why the loader can't just open the IPC buffer itself: the kernel driver
// scans the system process list for the literal image name `scootware.exe`
// (see Scootware-External/utilities/memory/ipc_config.h, IPC_APP_NAME).
// The loader's image is `ScootwareLoader.exe`, so a buffer it allocates
// would never get discovered. We need a process named `scootware.exe` to
// host the IPC handshake — and the cheapest one we have is the hollow
// host binary, which is already named `scootware.exe` (CMake OUTPUT_NAME)
// and is already going to be staged on disk for the mapper hollow.
//
// Flow:
//
//   loader (DriverBringup::Run)
//       │
//       │  1. stages hollow_exe to %TEMP%\<random>.exe
//       │  2. spawns it normally with HOST_PROBE_ARG (no hollowing) and
//       │     waits for it to exit
//       │
//       ▼
//   scootware.exe (host.cpp, probe branch)
//       │
//       │  3. allocates a page-aligned IPC buffer with IPC_MAGIC, sends
//       │     CMD_PING with a short budget
//       │  4. if PING comes back AND HOST_PROBE_SHUTDOWN_ARG was on the
//       │     command line, sends CMD_SHUTDOWN so the existing driver
//       │     unloads cleanly before the new mapper installs
//       │  5. exits with one of the HOST_PROBE_EXIT_* codes below
//       │
//       ▼
//   loader inspects exit code → decides whether to skip mapping, give the
//   kernel a moment to unload, or surface the unexpected case for the
//   diaglog.
//
// The exit codes are deliberately small distinct positive ints (NOT 0/1)
// so we can tell "the probe ran and reported X" apart from "the probe
// crashed before it could report" (which usually surfaces as 0xC0000005,
// 1, or ERROR_*-shaped values).
//

// Command-line args. Recognized by host.cpp via a single substring match
// on GetCommandLineA() — we don't want to drag a full argv parser into a
// binary whose .text we'd like to keep small.
#define HOST_PROBE_ARG               "--sw-probe-driver"
#define HOST_PROBE_SHUTDOWN_ARG      "--sw-shutdown-existing"

// No driver responded to PING within the budget — the loader should
// proceed to map the kernel driver fresh.
#define HOST_PROBE_EXIT_NO_DRIVER        0x10

// A driver did respond to PING. Either SHUTDOWN was not requested, or
// it was requested but the driver did NOT acknowledge the unload — the
// loader should treat this as "another driver is in the way" and decide
// whether to skip mapping or attempt it anyway.
#define HOST_PROBE_EXIT_DRIVER_ALIVE     0x11

// A driver responded to PING AND acknowledged SHUTDOWN. The loader
// should give the kernel a brief grace period to actually tear the
// driver down before re-mapping (in-flight callbacks, IRP completion,
// etc.) and then proceed to map.
#define HOST_PROBE_EXIT_DRIVER_SHUTDOWN  0x12
