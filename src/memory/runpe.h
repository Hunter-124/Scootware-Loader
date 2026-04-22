#pragma once
#include <vector>
#include <cstdint>
#include <windows.h>

#include <string>

namespace RunPE {
    // What the post-resume wait expects from the freshly hollowed child.
    //
    // ExpectAlive (default) — the payload is expected to keep running
    //    indefinitely (e.g. the cheat). If it dies inside the wait
    //    window we treat that as an init failure (DLL load, bad
    //    handoff, AV killing the host, etc.) and return false.
    //
    // ExpectCleanExit — the payload is expected to do its work and
    //    exit cleanly (e.g. the kernel-driver mapper). Clean exit
    //    inside the wait window is treated as SUCCESS; non-zero
    //    exit code or timeout (still alive) is treated as FAILURE.
    //    This flips the polarity of the wait without duplicating the
    //    700-line hollowing pipeline.
    enum class WaitMode {
        ExpectAlive,
        ExpectCleanExit,
    };

    // When waitMode==ExpectCleanExit and Execute returns false, the
    // caller can read this to distinguish timeout vs exit code vs an
    // earlier RunPE failure (PE parse, CreateProcess, hollowing).
    struct CleanExitDiag {
        bool  reachedWait = false; // child ran far enough that we waited on it
        bool  timedOut    = false;
        DWORD exitCode    = 0;
    };

    struct Options {
        // See WaitMode docs above.
        WaitMode waitMode = WaitMode::ExpectAlive;

        // How long to wait after ResumeThread before deciding the
        // wait outcome. The ExpectAlive default of 10s matches the
        // historical behaviour callers depend on; mapper bring-up
        // uses a longer budget because kernel I/O on a cold box can
        // legitimately take a few seconds beyond initial init.
        DWORD waitTimeoutMs = 10000;

        // Optional: only consulted when waitMode==ExpectCleanExit.
        CleanExitDiag* cleanExitDiag = nullptr;
    };

    // Process hollowing: maps PE from memoryBuffer into a suspended child
    // process and resumes it.
    //
    // childEnvironment, when non-empty, is passed verbatim as the
    // lpEnvironment argument of CreateProcessA — i.e. KEY=VAL\0KEY=VAL\0...\0
    // (a single trailing NUL on the last entry, plus the final NUL byte
    // that terminates the block, producing a double-NUL tail). When empty,
    // the child inherits the loader's env block via the implicit
    // CreateProcess inheritance path.
    //
    // Pass an explicit block when reliable propagation matters (e.g. the
    // SW_S/SW_P/SW_E auth handoff): process hollowing's
    // CreateRemoteThread / LdrInitializeThunk dance has been observed to
    // race with implicit env inheritance on certain Windows builds.
    bool Execute(const std::vector<uint8_t>& memoryBuffer,
                 size_t allocationSize,
                 const std::string& customHostPath = "",
                 const std::vector<char>& childEnvironment = {},
                 const Options& opts = {});
}
