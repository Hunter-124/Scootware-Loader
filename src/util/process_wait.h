#pragma once
//
// process_wait.h
//
// Cold-start support: detects whether a target host process (e.g. cs2.exe)
// is currently running, so the loader can stall the streaming/RunPE step
// behind a "Waiting for game process..." status message instead of failing
// or burning a session token on a host that never appears.
//
// The actual readiness gate (game initialised, modules paged in, signon
// state == 6) lives inside scootware-external — see core/game/game_ready.*.
// The loader only owns the coarse "is the host EXE alive" check because it
// owns the env-var handoff window and the user-visible status message.
//
#include <string>
#include <atomic>
#include <functional>

namespace ProcessWait {
    // Maps a productId to its host process name. Empty string means
    // "this product does not require a pre-existing host" (fall through
    // to the legacy hollowed-host injection path with no wait).
    //
    // Centralising this here keeps main.cpp out of the per-product weeds
    // and gives the porting doc a single edit point when bringing up a
    // new title.
    std::wstring HostProcessFor(const std::string& productId);

    // Returns true if a process with the given image name is currently
    // running for this user. Case-insensitive comparison against the
    // ToolHelp32 snapshot.
    bool IsRunning(const std::wstring& exeName);

    // Returns the PID of the first matching process, or 0 if none.
    // Case-insensitive comparison against the ToolHelp32 snapshot. Used by
    // the kernel-injector path to resolve `CMD_INJECT_DLL`'s target_pid
    // from the server-supplied process name.
    unsigned long PidFor(const std::wstring& exeName);

    // Blocks until the named process appears, periodically calling
    // tickCallback(seconds_elapsed) so the UI thread can refresh its
    // status text. Returns true once the process is detected, or false
    // if cancel() returns true (typically wired to "user closed loader").
    //
    // Polling cadence: 500ms. No hard timeout — cold starts can take
    // arbitrarily long depending on Steam / shader compile / first-run.
    bool WaitForProcess(const std::wstring& exeName,
                        const std::function<bool()>& cancel,
                        const std::function<void(int)>& tickCallback);
}
