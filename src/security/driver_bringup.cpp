#include "driver_bringup.h"

#include <windows.h>
#include <bcrypt.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "../api/api.h"
#include "../host/host_probe_protocol.h"
#include "../memory/runpe.h"
#include "../security/obf.h"
#include "../util/diaglog.h"

#pragma comment(lib, "bcrypt.lib")

namespace {

// %TEMP%\<random16hex>.exe so concurrent loader instances on the same
// machine don't clobber each other's host file. We prefer .exe over a
// random extension because a few user-mode AVs use the extension as a
// scan trigger and we want the same path the cheat host uses.
std::string RandomTempExePath() {
    char tmp[MAX_PATH] = {};
    DWORD n = ::GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH) {
        return {};
    }

    uint64_t r = 0;
    ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&r), sizeof(r),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    char name[64] = {};
    _snprintf_s(name, _countof(name), _TRUNCATE, "%016llx.exe", r);

    std::string out{ tmp };
    if (!out.empty() && out.back() != '\\' && out.back() != '/') {
        out.push_back('\\');
    }
    out.append(name);
    return out;
}

bool WriteHostBinary(const std::string& path, const std::vector<uint8_t>& bytes) {
    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    BOOL ok = ::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()),
                          &written, nullptr);
    ::CloseHandle(h);
    return ok && written == bytes.size();
}

// Drain %TEMP%\scootware-mapper-diag.log into the loader diag so a
// failed mapper invocation leaves everything on a single timeline. The
// mapper writes to its own file because the loader's RunPE never
// pumps the child's stdout pipe — see runpe.cpp's comment around the
// hStdOutWrite handles. The resulting file is small (KB-scale) even
// after many runs, but we hard-cap our read to 64 KiB so a broken
// mapper that loops printing lines cannot blow out the loader diag.
//
// We also delete the mapper diag after folding it in so the NEXT run
// starts with a clean slate; otherwise every new launch would append
// its diag on top of the last, and the "this line was from THIS run"
// heuristic would stop being obvious.
void FoldMapperDiagIntoLoaderDiag() {
    char tempDir[MAX_PATH] = {};
    DWORD n = ::GetTempPathA(MAX_PATH, tempDir);
    if (n == 0 || n >= MAX_PATH) return;

    std::string path{ tempDir };
    if (!path.empty() && path.back() != '\\' && path.back() != '/') {
        path.push_back('\\');
    }
    path += "scootware-mapper-diag.log";

    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LDIAG() << "[drvbringup] no mapper diag at " << path
                << " (mapper never reached its logging path — suspect "
                   "crash before main(), hollowing failure, or entrypoint "
                   "redirection issue)";
        return;
    }

    LARGE_INTEGER fileSize{};
    ::GetFileSizeEx(h, &fileSize);
    LONGLONG readCap = 64 * 1024;
    LONGLONG toRead  = fileSize.QuadPart > readCap ? readCap : fileSize.QuadPart;

    std::string blob;
    blob.resize(static_cast<size_t>(toRead));
    DWORD got = 0;
    if (toRead > 0) {
        ::ReadFile(h, blob.data(), static_cast<DWORD>(toRead), &got, nullptr);
        blob.resize(got);
    }
    ::CloseHandle(h);
    ::DeleteFileA(path.c_str());

    LDIAG() << "[drvbringup] --- begin folded mapper diag (" << got
            << " bytes) from " << path << " ---";

    // Split the blob on newlines and emit each as its own loader-diag
    // line so timestamps + "loader-pid" prefixes nest cleanly around
    // the mapper's own "mapper-pid" prefixes.
    size_t cursor = 0;
    while (cursor < blob.size()) {
        size_t eol = blob.find('\n', cursor);
        if (eol == std::string::npos) eol = blob.size();
        size_t end = eol;
        while (end > cursor && (blob[end - 1] == '\r' ||
                                 blob[end - 1] == '\n')) {
            --end;
        }
        if (end > cursor) {
            LDIAG() << "  " << std::string_view(
                blob.data() + cursor, end - cursor);
        }
        cursor = eol + 1;
    }

    LDIAG() << "[drvbringup] --- end folded mapper diag ---";
}

// Spawn the staged host EXE in probe mode and wait for it to exit.
//
// The host's main() (Website/Loader/src/host/host.cpp) detects the probe
// arg on its command line and runs a tiny PING + (optional) SHUTDOWN
// against the kernel driver's IPC, then exits with one of the
// HOST_PROBE_EXIT_* codes.
//
// Returns the raw exit code, or -1 if the spawn / wait itself failed
// (which the caller treats as "probe was inconclusive — proceed
// cautiously").
int SpawnProbe(const std::string& hostPath, bool requestShutdown) {
    std::string cmdLine;
    cmdLine.reserve(hostPath.size() + 64);
    cmdLine.push_back('"');
    cmdLine.append(hostPath);
    cmdLine.push_back('"');
    cmdLine.push_back(' ');
    cmdLine.append(HOST_PROBE_ARG);
    if (requestShutdown) {
        cmdLine.push_back(' ');
        cmdLine.append(HOST_PROBE_SHUTDOWN_ARG);
    }

    // CreateProcessA mutates lpCommandLine, so we have to give it a
    // writable buffer (std::string::data() since C++17 is null-terminated
    // and writable, but a vector copy is still the conventionally
    // portable choice).
    std::vector<char> cmdMutable(cmdLine.begin(), cmdLine.end());
    cmdMutable.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    // CREATE_NO_WINDOW so the probe never flashes a console even
    // momentarily — the loader is windowed and the user shouldn't see
    // a black box appear during launch.
    BOOL ok = ::CreateProcessA(nullptr, cmdMutable.data(),
                               nullptr, nullptr,
                               FALSE,
                               CREATE_NO_WINDOW,
                               nullptr, nullptr,
                               &si, &pi);
    if (!ok) {
        return -1;
    }

    // Generous outer wait — the probe internally bounds itself to
    // HOST_PROBE_PING_BUDGET_MS + HOST_PROBE_SHUTDOWN_BUDGET_MS (~7.5s
    // worst case in host.cpp), so 15s gives the kernel side plenty of
    // slack for cold-boot scheduling jitter without leaving the loader
    // hung if the probe wedges.
    DWORD waitResult = ::WaitForSingleObject(pi.hProcess, 15000);

    DWORD exitCode = static_cast<DWORD>(-1);
    if (waitResult == WAIT_OBJECT_0) {
        if (!::GetExitCodeProcess(pi.hProcess, &exitCode)) {
            exitCode = static_cast<DWORD>(-1);
        }
    } else {
        // Probe wedged — kill it so we don't leak a scootware.exe that
        // the kernel might still be talking to. The driver-side IPC
        // dispatcher tears down on process exit, so this also clears
        // any half-finished SHUTDOWN that's mid-flight.
        ::TerminateProcess(pi.hProcess, 1);
    }

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

} // namespace

namespace DriverBringup {

Outcome Run(const std::string& productId,
            const std::string& token,
            const std::string& hwid,
            const std::vector<uint8_t>& hollowExeBuffer,
            size_t hollowAllocSize) {
    Outcome out;
    out.result = Result::HollowFailed;

    LDIAG() << "[drvbringup] Run entered: product='" << productId
            << "', token.empty=" << token.empty()
            << ", hwid.empty=" << hwid.empty()
            << ", hollowExe=" << hollowExeBuffer.size() << " bytes"
            << " (alloc " << hollowAllocSize << ")";

    if (hollowExeBuffer.empty()) {
        // Refuse to silently fall back to a non-hollowed launch — that
        // would leak the mapper's own image name into the task list and
        // defeat the entire point of routing it through scootware.exe.
        // Caller is expected to stream hollow_exe up front for the
        // cheat anyway, so this is a programming error, not a runtime
        // condition we want to swallow.
        out.result = Result::HostMissing;
        out.details = OBF_S("hollow_exe buffer not supplied to DriverBringup");
        LDIAG_LINE(out.details);
        return out;
    }

    // 1. Stage hollow_exe to %TEMP%. Same host binary the cheat uses,
    //    so in the task list both children (probe + mapper, plus the
    //    later cheat) all show up under the identical `scootware.exe`
    //    image name. We stage BEFORE streaming the mapper because the
    //    probe step (next) might tell us we don't need to map at all,
    //    and pulling the mapper down only to throw it away is
    //    unnecessary network + disk churn on every launch.
    std::string hostPath = RandomTempExePath();
    if (hostPath.empty() || !WriteHostBinary(hostPath, hollowExeBuffer)) {
        out.result = Result::HostMissing;
        out.details = OBF_S("failed to stage hollow_exe to %TEMP% for the "
                            "driver mapper (disk full, AV blocking write?)");
        LDIAG_LINE(out.details);
        return out;
    }

    LDIAG() << "[drvbringup] staged probe/mapper host at: " << hostPath;

    // 2. Probe for an already-loaded kernel driver. If one's there we
    //    ask it to SHUTDOWN so the new mapper isn't installing a second
    //    driver on top of the existing one (the kernel-side mapper has
    //    no "is one already mapped?" check; double-loading would either
    //    duplicate the driver, fail in opaque ways, or trash the IPC
    //    target the cheat uses to find the working instance).
    //
    //    The probe is the staged host EXE, spawned with
    //    HOST_PROBE_ARG. Its main() is in Website/Loader/src/host/
    //    host.cpp, which sets up a tiny IPC buffer with IPC_MAGIC,
    //    sends CMD_PING (and optionally CMD_SHUTDOWN), and exits with
    //    a HOST_PROBE_EXIT_* code we read off the process.
    LDIAG_LINE("[drvbringup] probing for an already-loaded kernel driver "
               "(this is host.cpp running with --sw-probe-driver, NOT a hollow)");
    const ULONGLONG probe_t0 = ::GetTickCount64();
    int probeExit = SpawnProbe(hostPath, /*requestShutdown*/ true);
    const ULONGLONG probe_dt = ::GetTickCount64() - probe_t0;

    LDIAG() << "[drvbringup] probe exited with 0x" << std::hex << probeExit
            << std::dec << " in " << probe_dt << "ms"
            << " (NO_DRIVER=0x" << std::hex << HOST_PROBE_EXIT_NO_DRIVER
            << ", DRIVER_ALIVE=0x" << HOST_PROBE_EXIT_DRIVER_ALIVE
            << ", DRIVER_SHUTDOWN=0x" << HOST_PROBE_EXIT_DRIVER_SHUTDOWN
            << std::dec << ")";

    // After the probe the loader's policy is "always map fresh" — even
    // when the existing driver acknowledged SHUTDOWN, we don't trust
    // that the previous-generation kernel state is still consistent
    // with whatever the new mapper expects. The only branch that
    // SKIPS mapping is the ProbeFailedDriverStuck case below, which
    // bails entirely rather than risking a double-load.
    bool waitForUnload = false;

    switch (probeExit) {
    case HOST_PROBE_EXIT_NO_DRIVER:
        LDIAG_LINE("[drvbringup] probe: no existing driver detected — fresh map");
        break;

    case HOST_PROBE_EXIT_DRIVER_SHUTDOWN:
        LDIAG_LINE("[drvbringup] probe: existing driver acknowledged SHUTDOWN — "
                   "will sleep briefly for kernel unload before mapping fresh");
        waitForUnload = true;
        break;

    case HOST_PROBE_EXIT_DRIVER_ALIVE: {
        // Existing driver answered PING but ignored / errored on
        // SHUTDOWN. Mapping a second driver on top of one that won't
        // unload is a recipe for kernel-state corruption and
        // double-IPC contention, so we abort instead.
        //
        // The user-facing remediation is straightforward: reboot to
        // clear the stuck driver, or manually unload it via whatever
        // tool the previous loader instance used. Surface that in the
        // outcome so it lands in the loader's status pill.
        out.result = Result::ProbeFailedDriverStuck;
        out.details = OBF_S(
            "an existing kernel driver is loaded but refused to unload "
            "via IPC SHUTDOWN — refusing to double-load. Reboot the "
            "machine to clear the stuck driver and re-launch.");
        LDIAG_LINE(out.details);

        if (::GetFileAttributesA(hostPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!::DeleteFileA(hostPath.c_str())) {
                ::MoveFileExA(hostPath.c_str(), nullptr,
                              MOVEFILE_DELAY_UNTIL_REBOOT);
            }
        }
        return out;
    }

    default:
        // Probe spawn failed (-1) or returned an unexpected value
        // (probe binary mismatch, AV killed it mid-flight, etc.).
        // We don't have enough info to know whether a driver is up,
        // so we proceed to map and let RunPE / the cheat-side PING
        // surface any subsequent failure. Logged loud so it lands in
        // the diag for triage.
        LDIAG() << "[drvbringup] probe returned UNEXPECTED exit code 0x"
                << std::hex << probeExit << std::dec
                << " — proceeding to map (caveat emptor; if a driver "
                   "is silently up this may double-load)";
        break;
    }

    if (waitForUnload) {
        // Driver acknowledged SHUTDOWN but the actual unload is
        // asynchronous on the kernel side — IRP queue draining,
        // callback unregistration, etc. 1.5s is empirically enough
        // for the previous-generation driver to fully tear down on
        // a stock Win10/11 machine. We could PING-poll for "is it
        // gone yet?" but a fixed sleep keeps the protocol simple
        // and the worst case is just a slightly slower launch.
        ::Sleep(1500);
    }

    // 3. Stream the mapper EXE from the API. Same encrypted/HWID-bound
    //    transport the cheat uses for primary_exe / hollow_exe — the
    //    server-side enforcement (see api-server/src/routes/productAssets.ts)
    //    refuses unauthenticated or HWID-mismatched callers, so we don't
    //    need to repeat any of those checks here.
    LDIAG() << "[drvbringup] streaming driver_loader for product='" << productId << "'...";
    const ULONGLONG dl_t0 = ::GetTickCount64();
    auto [mapperBytes, mapperAlloc] =
        Api::StreamAsset(productId, OBF_S("driver_loader"), token, hwid);
    const ULONGLONG dl_dt = ::GetTickCount64() - dl_t0;

    if (mapperBytes.empty()) {
        out.result = Result::StreamFailed;
        if (Api::LastStreamWasHwidMismatch()) {
            out.details = OBF_S(
                "driver_loader stream rejected by server: HWID mismatch");
        } else {
            out.details = OBF_S(
                "driver_loader stream returned 0 bytes (asset missing on "
                "server, or transport/auth failure) — upload one under "
                "Admin -> Products -> Assets with type 'Driver Loader'");
        }
        LDIAG() << "[drvbringup] FAILED: " << out.details
                << " (took " << dl_dt << "ms)";

        if (::GetFileAttributesA(hostPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!::DeleteFileA(hostPath.c_str())) {
                ::MoveFileExA(hostPath.c_str(), nullptr,
                              MOVEFILE_DELAY_UNTIL_REBOOT);
            }
        }
        return out;
    }

    LDIAG() << "[drvbringup] streamed driver_loader: " << mapperBytes.size()
            << " bytes (alloc " << mapperAlloc << ") in " << dl_dt << "ms";

    // 4. Hollow the mapper into the staged host. ExpectCleanExit because
    //    the mapper is one-shot — it does its kernel I/O, exits with
    //    code 0, and we move on to spawning the cheat. The 30s budget
    //    covers cold-box bring-up where the very first kernel allocation
    //    can sit in queue behind boot-time driver loads.
    RunPE::Options opts;
    opts.waitMode      = RunPE::WaitMode::ExpectCleanExit;
    opts.waitTimeoutMs = 30000;
    RunPE::CleanExitDiag mapperDiag;
    opts.cleanExitDiag = &mapperDiag;

    // Mapper does not need the SW_S/SW_P/SW_E handoff — it neither calls
    // any auth endpoint nor talks to the cheat. We deliberately pass an
    // empty env block so the mapper inherits the loader's plain env (no
    // session secrets) and the wrap stays exclusive to the cheat host.
    bool hollowed = RunPE::Execute(mapperBytes, mapperAlloc, hostPath,
                                   /*env*/ {}, opts);

    // Cleanup the staged host file regardless of outcome — RunPE::Execute
    // already deletes customHostPath on its own success path, but if the
    // CreateProcessA call inside RunPE failed before that delete fired
    // we'd otherwise leak the file. Best-effort: if Windows still has the
    // file open (rare), schedule it for delete-on-reboot.
    if (::GetFileAttributesA(hostPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!::DeleteFileA(hostPath.c_str())) {
            ::MoveFileExA(hostPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }

    // Wipe the in-memory plaintext mapper as soon as it's been mapped.
    // (RunPE made its own remote copy inside the suspended child before
    // ResumeThread, so this doesn't tear out the running code.)
    if (!mapperBytes.empty()) {
        ::SecureZeroMemory(mapperBytes.data(), mapperBytes.size());
    }

    if (!hollowed) {
        // RunPE in ExpectCleanExit mode returns false for both
        // "mapper hung past timeout" AND "mapper exited non-zero" —
        // we lump them together under MapperReportedFailure since
        // the user-visible remediation is the same: investigate the
        // mapper's own logs / kernel state.
        out.result = Result::MapperReportedFailure;

        // Fold the mapper's own diag file in first, so the loader log
        // can be read top-to-bottom and still make sense. We do this
        // BEFORE composing the summary message so the summary lands
        // AFTER the mapper lines (which is what a reader expects —
        // details first, conclusion last).
        FoldMapperDiagIntoLoaderDiag();

        std::ostringstream msg;
        msg << "driver mapper failed to map the kernel driver";
        if (mapperDiag.reachedWait) {
            if (mapperDiag.timedOut) {
                msg << " — timed out after " << opts.waitTimeoutMs << "ms";
            } else {
                msg << " — mapper exit code 0x" << std::uppercase
                    << std::hex << std::setfill('0') << std::setw(8)
                    << mapperDiag.exitCode;
                // 0x5A00..0x5A0A are autopilot's own error codes — call
                // them out explicitly because the hex alone is opaque.
                switch (mapperDiag.exitCode) {
                case 0x5A00: msg << " (AP_EXIT_UNKNOWN — autopilot bailed before a specific step tagged its own failure)"; break;
                case 0x5A01: msg << " (AP_EXIT_INVALID_PROVIDER_ID)"; break;
                case 0x5A02: msg << " (AP_EXIT_SELF_MODULE_HANDLE_NULL — GetModuleHandleW(NULL) returned null, hollow-target PEB is wrong)"; break;
                case 0x5A03: msg << " (AP_EXIT_PE_HEADER_INVALID — mapper's own PE headers are corrupt in memory)"; break;
                case 0x5A04: msg << " (AP_EXIT_RESOURCE_DIR_MISSING — no .rsrc directory in mapper image)"; break;
                case 0x5A05: msg << " (AP_EXIT_LDR_FIND_RESOURCE_FAILED — IDR_EMBEDDED_DRIVER missing — check smap_packed.exe build)"; break;
                case 0x5A06: msg << " (AP_EXIT_LDR_ACCESS_RESOURCE_FAILED)"; break;
                case 0x5A07: msg << " (AP_EXIT_RESOURCE_DECODE_FAILED — XOR/MSDelta roundtrip failed — drv.bin likely stale)"; break;
                case 0x5A08: msg << " (AP_EXIT_PE_LAYOUT_FAILED)"; break;
                case 0x5A09: msg << " (AP_EXIT_PROVIDER_CREATE_FAILED)"; break;
                case 0x5A0A: msg << " (AP_EXIT_MAP_DRIVER_FAILED — provider was loaded but kernel write rejected)"; break;
                case 0x0000001F: msg << " (ERROR_GEN_FAILURE — mapper returned default/unclassified failure — see mapper diag above for the step that bailed)"; break;
                default: break;
                }
            }
        } else {
            msg << " — RunPE failed before mapper finished (bad PE, "
                   "CreateProcess, or hollowing step)";
        }
        msg << " (see %TEMP%\\scootware-diag.log)";
        out.details = msg.str();
        LDIAG_LINE(out.details);
        return out;
    }

    // Success path still drains the mapper diag so the user can see
    // the happy-path trace (provider selection, DSE toggle, map OK).
    // Cheap — the file is ~KB size and deletes right after folding.
    FoldMapperDiagIntoLoaderDiag();

    out.result  = Result::Success;
    if (waitForUnload) {
        out.details = OBF_S("driver mapper completed cleanly "
                            "(replaced an existing driver that was politely shut down first)");
    } else {
        out.details = OBF_S("driver mapper completed cleanly");
    }
    LDIAG_LINE("[drvbringup] success — kernel driver bring-up done");
    return out;
}

} // namespace DriverBringup
