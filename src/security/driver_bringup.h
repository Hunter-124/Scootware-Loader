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
// To work around that without giving up the "is one already loaded?"
// check, we use the SAME hollow_exe binary as a probe. It's already
// named `scootware.exe` (CMake OUTPUT_NAME), it's already going to be
// staged on disk for the mapper hollow, and host.cpp has a `--sw-probe-
// driver` mode that does PING + optional SHUTDOWN over a tiny IPC stub
// and reports the result via process exit code. See
// Website/Loader/src/host/host_probe_protocol.h for the protocol.
//
// With the probe in place the bring-up flow is:
//
//   1. stage hollow_exe to %TEMP%
//   2. spawn host.exe with --sw-probe-driver --sw-shutdown-existing,
//      wait for it to exit
//   3. interpret exit code:
//        - NO_DRIVER         -> map a fresh one
//        - DRIVER_SHUTDOWN   -> sleep briefly for the kernel to fully
//                               unload, then map a fresh one
//        - DRIVER_ALIVE      -> existing driver refused to leave; bail
//                               with ProbeFailedDriverStuck rather than
//                               attempt a double-load
//   4. (if mapping) stream driver_loader, hollow it into the staged host
//   5. cleanup
//

#include <string>
#include <vector>
#include <cstdint>

namespace DriverBringup {

    enum class Result {
        Success,                // mapper ran cleanly (with or without a prior SHUTDOWN of an existing driver)
        AlreadyUp,              // probe found a working driver and SkipIfAlreadyMapped was set, so we never invoked the mapper
        StreamFailed,           // /api/products/.../assets/stream returned nothing
        HostMissing,            // could not stage hollow_exe to %TEMP% (also: no hollowExeBuffer supplied)
        HollowFailed,           // RunPE::Execute returned false
        MapperReportedFailure,  // mapper exited with non-zero code or hung past the timeout
        ProbeFailedDriverStuck, // probe detected an existing driver but it refused SHUTDOWN, so we declined to map a second one on top of it
    };

    struct Outcome {
        Result      result = Result::HollowFailed;
        // Human-readable summary suitable for Api::ReportLoaderEvent and
        // for the loader status pill. Always populated, even on success.
        std::string details;
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
    // Returns once the mapper has exited (success path) or once we've
    // given up on it (failure path). Does NOT spin a PING-poll — that's
    // the cheat's job after it's hollowed.
    Outcome Run(const std::string& productId,
                const std::string& token,
                const std::string& hwid,
                const std::vector<uint8_t>& hollowExeBuffer,
                size_t hollowAllocSize);
}
