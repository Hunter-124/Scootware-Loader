#pragma once
//
// driver_bringup.h
//
// Loader-side kernel-driver bring-up.
//
// Historically the cheat (Scootware-External, hollowed scootware.exe) was
// responsible for noticing "no driver loaded" and pulling the DriverLoader
// EXE down, ShellExecuteEx(runas)'ing it, and PING-polling for the kernel
// driver to come online. Two problems with that:
//
//   * The UAC prompt fired AFTER the cheat had already started rendering,
//     which felt like a second login step from the user's POV.
//   * The mapper EXE was visible in the task list as "DriverLoader.exe"
//     (or whatever filename the temp path picked) — easy fingerprint.
//
// This module flips both of those:
//
//   * The loader is already manifest-elevated (requireAdministrator), so
//     it can hollow elevated children directly without re-prompting. The
//     UAC prompt the user already saw to launch the loader is the only
//     UAC prompt for the whole session.
//
//   * The mapper payload is process-hollowed into a freshly extracted
//     `scootware.exe` host (the same hollow_exe binary the cheat uses).
//     In Task Manager the user briefly sees a second `scootware.exe`
//     instance — same image name as the cheat, no `DriverLoader.exe`
//     anywhere — that exits as soon as the mapping work is done.
//
// The success signal is "mapper child exited with code 0 within the
// timeout budget". The loader can't directly do a PING/SHUTDOWN against
// the kernel driver because the driver scans the process list for the
// literal image name `scootware.exe` (see ipc_config.h, IPC_APP_NAME)
// and the loader's image name is `ScootwareLoader.exe` — its IPC magic
// would never be found.
//
// We used to spawn a separate scootware.exe probe to do that check via
// the host.cpp `--sw-probe-driver` mode. That codepath is gone — the
// loader image is now itself a recognized IPC target (see
// FINAL-DRV/driver.cpp target_names + CMake OUTPUT_NAME), so the loader
// can ping/shutdown the existing driver in-process via LoaderIpc::Ping /
// LoaderIpc::RequestShutdown BEFORE calling Run(). That collapses the
// double-spawn (probe scootware.exe + mapper scootware.exe) into a
// single scootware.exe (the mapper) per bring-up.
//
// Run() therefore is now strictly the "stream + hollow + wait" stage:
//
//   1. stage hollow_exe to %TEMP%
//   2. stream driver_loader from the API
//   3. hollow it into the staged host and wait for clean exit
//   4. cleanup
//
// The caller is responsible for the "should I map at all?" decision —
// see main.cpp where it pings via LoaderIpc and skips the call entirely
// if a healthy driver is already attached.
//

#include <string>
#include <vector>
#include <cstdint>

namespace DriverBringup {

    enum class Result {
        Success,                // mapper ran cleanly
        AlreadyUp,              // retained for ABI symmetry with caller switch — Run() itself never returns this; caller short-circuits via LoaderIpc::Ping
        StreamFailed,           // /api/products/.../assets/stream returned nothing
        HostMissing,            // could not stage hollow_exe to %TEMP% (also: no hollowExeBuffer supplied)
        HollowFailed,           // RunPE::Execute returned false
        MapperReportedFailure,  // mapper exited with non-zero code or hung past the timeout
    };

    struct Outcome {
        Result      result = Result::HollowFailed;
        // Human-readable summary suitable for Api::ReportLoaderEvent and
        // for the loader status pill. Always populated, even on success.
        std::string details;
    };

    // Mapper configuration — controls which KDU provider, shellcode version,
    // and DSE behaviour the hollowed mapper uses.  Pass default-initialised
    // (zero / empty) for the baked defaults (auto-detect provider, SC V2,
    // DSE mandatory).
    struct MapperConfig {
        // Provider public id (1..KDU_PUBLIC_ID_MAX).  0 = auto-detect first
        // compatible provider on the target system.
        int providerPublicId = 0;

        // Shellcode version (1 = KiBUGCHECK, 2 = ExQueueWorkItem, 3 = ObCreateObject).
        // 0 = use mapper's compile-time default (currently V3).
        int shellCodeVersion = 0;

        // TRUE (default) — abort the whole mapping sequence if DSE disable
        // fails.  FALSE — log a warning and try the map anyway.
        bool requireDseSuccess = true;

        // Driver object name (required for shellcode V3).  Pass empty string
        // for shellcode V1/V2 (ignored by the mapper).
        std::string driverObjectName;

        // Driver registry key name (optional, shellcode V3 only).  When empty
        // the mapper uses driverObjectName as the registry path fallback.
        std::string driverRegistryPath;
    };

    // Pull the latest `driver_loader` asset for `productId` from the API,
    // hollow it into a fresh `scootware.exe` host (using `hollowExeBuffer`
    // as the host PE), and wait for the mapper to exit cleanly.
    //
    // hollowExeBuffer / hollowAllocSize must be the same buffer the
    // caller already streamed from the API for the cheat host — there is
    // no need to fetch hollow_exe twice. Pass an empty buffer if the
    // caller has not streamed it yet; in that case `Run` will refuse
    // (HostMissing) rather than silently degrading to a non-hollowed
    // ShellExecute (which would lose the stealth benefit and re-introduce
    // the visible DriverLoader.exe name in the task list).
    //
    // mapperCfg controls which vulnerable-driver provider the mapper uses,
    // which shellcode version, and whether DSE failure is fatal.  Leave
    // default-initialised for baked defaults.
    //
    // Returns once the mapper has exited (success path) or once we've
    // given up on it (failure path). Does NOT spin a PING-poll — that's
    // the cheat's job after it's hollowed.
    Outcome Run(const std::string& productId,
                const std::string& token,
                const std::string& hwid,
                const std::vector<uint8_t>& hollowExeBuffer,
                size_t hollowAllocSize,
                const MapperConfig& mapperCfg = {});
}
