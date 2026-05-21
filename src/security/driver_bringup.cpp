#include "driver_bringup.h"

#include <windows.h>
#include <bcrypt.h>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "../api/api.h"
#include "../memory/runpe.h"
#include "../security/obf.h"
#include "../util/diaglog.h"

#pragma comment(lib, "bcrypt.lib")

namespace {

std::mutex gDriverBringupMutex;

// %TEMP%\<random16hex>\Scoot-Mapper.exe so concurrent loader instances on the same
// machine don't clobber each other's host file. We prefer .exe over a
// random extension because a few user-mode AVs use the extension as a
// scan trigger and we want the same path the cheat host uses.
std::string RandomTempExePath(std::string& outDir) {
    char tmp[MAX_PATH] = {};
    DWORD n = ::GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH) {
        return {};
    }

    uint64_t r = 0;
    ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&r), sizeof(r),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    char dirName[32] = {};
    _snprintf_s(dirName, _countof(dirName), _TRUNCATE, "%016llx", r);

    std::string out{ tmp };
    if (!out.empty() && out.back() != '\\' && out.back() != '/') {
        out.push_back('\\');
    }
    out.append(dirName);
    
    ::CreateDirectoryA(out.c_str(), nullptr);
    outDir = out;

    out.append("\\Scoot-Mapper.exe");
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

// Clean up driver artifacts (vuln drivers) that the mapper might leave behind in %TEMP%
void CleanUpTempDriverArtifacts() {
    char tmp[MAX_PATH] = {};
    DWORD n = ::GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return;

    std::string searchPath = std::string(tmp) + "\\*.sys";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = ::FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fileToDel = std::string(tmp) + "\\" + fd.cFileName;
                ::DeleteFileA(fileToDel.c_str());
            }
        } while (::FindNextFileA(hFind, &fd));
        ::FindClose(hFind);
    }
}

// SpawnProbe lived here historically — replaced by in-process
// LoaderIpc::Ping / RequestShutdown. See loader_ipc.h.

} // namespace

namespace DriverBringup {

Outcome Run(const std::string& productId,
            const std::string& token,
            const std::string& hwid,
            const std::vector<uint8_t>& hollowExeBuffer,
            size_t hollowAllocSize,
            const MapperConfig& mapperCfg) {
    std::unique_lock<std::mutex> bringupLock(gDriverBringupMutex, std::try_to_lock);
    Outcome out;
    out.result = Result::HollowFailed;

    if (!bringupLock.owns_lock()) {
        out.result = Result::MapperReportedFailure;
        out.details = OBF_S("driver mapper is already running; wait for the current bring-up attempt to finish");
        LDIAG() << "[drvbringup] refusing overlapping mapper launch: " << out.details;
        return out;
    }

    LDIAG() << "[drvbringup] Run entered: product='" << productId
            << "', token.empty=" << token.empty()
            << ", hwid.empty=" << hwid.empty()
            << ", hollowExe=" << hollowExeBuffer.size() << " bytes"
            << " (alloc " << hollowAllocSize << ")"
            << ", mapperCfg: prov=" << mapperCfg.providerPublicId
            << " scv=" << mapperCfg.shellCodeVersion
            << " dseReq=" << mapperCfg.requireDseSuccess
            << " drvn='" << mapperCfg.driverObjectName << "'"
            << " drvr='" << mapperCfg.driverRegistryPath << "'";

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

    // 1. Stage hollow_exe to %TEMP%\<random>\Scoot-Mapper.exe. Same host binary the cheat uses,
    //    so in the task list the mapper child shows up as `Scoot-Mapper.exe`
    //    (matching the later cheat) — no `DriverLoader.exe` fingerprint.
    std::string hostDir;
    std::string hostPath = RandomTempExePath(hostDir);
    if (hostPath.empty() || !WriteHostBinary(hostPath, hollowExeBuffer)) {
        out.result = Result::HostMissing;
        out.details = OBF_S("failed to stage hollow_exe to %TEMP% for the "
                            "driver mapper (disk full, AV blocking write?)");
        if (!hostDir.empty()) {
            ::RemoveDirectoryA(hostDir.c_str());
        }
        LDIAG_LINE(out.details);
        return out;
    }

    LDIAG() << "[drvbringup] staged mapper host at: " << hostPath;

    // 2. (was: spawn host probe to detect existing driver — replaced by
    //    LoaderIpc::Ping/RequestShutdown in main.cpp before this call.)

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
        if (!hostDir.empty()) {
            ::RemoveDirectoryA(hostDir.c_str());
        }
        return out;
    }

    LDIAG() << "[drvbringup] streamed driver_loader: " << mapperBytes.size()
            << " bytes (alloc " << mapperAlloc << ") in " << dl_dt << "ms";

    // 4. Build mapper command-line args from config.  The mapper's
    //    KDUProcessCommandLine detects `-kdu-ap` and parses the key=value
    //    pairs that follow, so the loader can change provider / shellcode
    //    / DSE policy without rebuilding the mapper binary.
    //
    //    Format:  -kdu-ap prov=N,scv=N[,dse=1][,drvn=S[,drvr=S]]
    //
    std::string mapperArgs = OBF_S("-kdu-ap");
    {
        std::ostringstream cfg;
        cfg << "prov=" << mapperCfg.providerPublicId;
        cfg << ",scv=" << mapperCfg.shellCodeVersion;
        if (mapperCfg.requireDseSuccess) {
            cfg << ",dse=1";
        }
        //
        // Shellcode V3: append driver object name (required) and registry
        // path (optional).  For V1/V2 these are omitted — the mapper ignores
        // them.
        //
        if (!mapperCfg.driverObjectName.empty()) {
            cfg << ",drvn=" << mapperCfg.driverObjectName;
        }
        if (!mapperCfg.driverRegistryPath.empty()) {
            cfg << ",drvr=" << mapperCfg.driverRegistryPath;
        }
        mapperArgs += " ";
        mapperArgs += cfg.str();
    }
    LDIAG() << "[drvbringup] mapper args: " << mapperArgs;

    // 5. Hollow the mapper into the staged host. ExpectCleanExit because
    //    the mapper is one-shot — it does its kernel I/O, exits with
    //    code 0, and we move on to spawning the cheat. 12s covers normal
    //    cold-box bring-up; the driver mapper completes quickly in practice.
    RunPE::Options opts;
    opts.waitMode      = RunPE::WaitMode::ExpectCleanExit;
    opts.waitTimeoutMs = 12000;
    opts.commandArgs   = mapperArgs;
    RunPE::CleanExitDiag mapperDiag;
    opts.cleanExitDiag = &mapperDiag;

    // Mapper does not need the SW_S/SW_P/SW_E handoff — it neither calls
    // any auth endpoint nor talks to the cheat. We deliberately pass an
    // empty env block so the mapper inherits the loader's plain env (no
    // session secrets) and the wrap stays exclusive to the cheat host.
    RunPE::Result mapperResult = RunPE::Execute(mapperBytes, mapperAlloc, hostPath,
                                                 /*env*/ {}, opts);
    bool hollowed = mapperResult.ok;

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
    if (!hostDir.empty()) {
        ::RemoveDirectoryA(hostDir.c_str());
    }

    // Clean up any .sys artifacts the mapper may have left in %TEMP%
    CleanUpTempDriverArtifacts();

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
                case 0x5A0B: msg << " (AP_EXIT_DSE_FAILED — DSE disable failed and RequireDSESuccess is set; driver signature enforcement could not be turned off)"; break;
                case 0x5A0C: msg << " (AP_EXIT_NO_COMPATIBLE_PROVIDER — no vulnerable driver in the provider table supports the requested shellcode version on this build)"; break;
                case 0x0000001F: msg << " (ERROR_GEN_FAILURE — mapper returned default/unclassified failure — see mapper diag above for the step that bailed)"; break;
                default: break;
                }
            }
        } else {
            msg << " — RunPE failed before mapper finished (bad PE, "
                   "CreateProcess, or hollowing step)";
        }
        msg << OBF_S(" (see %TEMP%\\scootware-diag.log)");
        out.details = msg.str();
        LDIAG_LINE(out.details);
        return out;
    }

    // Success path still drains the mapper diag so the user can see
    // the happy-path trace (provider selection, DSE toggle, map OK).
    // Cheap — the file is ~KB size and deletes right after folding.
    FoldMapperDiagIntoLoaderDiag();

    out.result  = Result::Success;
    out.details = OBF_S("driver mapper completed cleanly");
    LDIAG_LINE("[drvbringup] success — kernel driver bring-up done");
    return out;
}

} // namespace DriverBringup
