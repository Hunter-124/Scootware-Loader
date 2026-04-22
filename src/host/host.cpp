#include <windows.h>
#include <intrin.h>
#include <string.h>
#include <stdint.h>

#include "host_probe_protocol.h"

//
// host.cpp — dual-mode hollow target / driver probe.
//
// Default mode (no args):
//   Spawned suspended by RunPE and process-hollowed before main() ever
//   runs. The infinite Sleep is unreachable in the hollow path; it only
//   exists so the linker keeps an entry point and the binary is a
//   well-formed PE.
//
// Probe mode (HOST_PROBE_ARG on the command line):
//   Spawned NORMALLY (no suspended flag, no hollowing) by the loader's
//   DriverBringup::Run before it commits to mapping the kernel driver.
//   We allocate a page-aligned IPC buffer with the magic the kernel
//   driver scans for, send CMD_PING, and exit with a status code the
//   loader can read.
//
//   If HOST_PROBE_SHUTDOWN_ARG is also present, a successful PING is
//   followed by CMD_SHUTDOWN — that's the "tells the existing driver to
//   close itself so we don't double-load" half of the contract. The
//   driver-side handler unmaps itself, releases its IRP queue, and
//   stops answering IPC; we then sleep a short bit so the loader's
//   subsequent map operation isn't racing the unload.
//
// The probe is intentionally minimal: no CRT-allocator-backed
// containers, no exception machinery, no third-party headers. The IPC
// protocol constants are duplicated rather than `#include`'d from
// Scootware-External so this file stays buildable from the loader tree
// alone.
//

// ───── IPC protocol (kept in lockstep with Scootware-External/utilities/memory/shared_memory_ipc.h) ─────

#define HOST_IPC_MAGIC          0x504D585F49504331ULL
#define HOST_IPC_VERSION        1
#define HOST_IPC_DATA_OFFSET    0x100
#define HOST_IPC_DATA_SIZE      0x1000
#define HOST_IPC_TOTAL_SIZE     (HOST_IPC_DATA_OFFSET + HOST_IPC_DATA_SIZE)

#define HOST_CMD_IDLE           0
#define HOST_CMD_PING           8
#define HOST_CMD_SHUTDOWN       9

#define HOST_STATUS_IDLE        0
#define HOST_STATUS_PROCESSING  1
#define HOST_STATUS_SUCCESS     2
#define HOST_STATUS_ERROR       3

#pragma pack(push, 1)
struct host_ipc_header {
    uint64_t magic;          // 0x00
    uint32_t version;        // 0x08
    uint32_t process_id;     // 0x0C
    uint32_t command;        // 0x10
    uint32_t status;         // 0x14
    uint64_t cr3_cached;     // 0x18
    uint64_t reserved[4];    // 0x20
};
#pragma pack(pop)

// PING budget for the probe. Short on purpose: a driver that's actually
// loaded answers in well under 100ms (it's just a "set status = SUCCESS"
// in the dispatch loop), so 2.5s is two orders of magnitude of safety
// margin. Anything longer adds wall-clock to every cold launch where
// no driver is present, since we always have to wait the full budget
// in that case.
//
// (The previous design used the cheat-side 8s budget, but that ran
// AFTER the mapper — at probe time we have no reason to be that
// patient: either the previous launch's driver is up and answers
// instantly, or it isn't.)
constexpr int HOST_PROBE_PING_BUDGET_MS     = 2500;

// SHUTDOWN budget. The driver's unload path is short but does have to
// wait for any in-flight IRPs to drain, so 5s is the conservative
// upper bound.
constexpr int HOST_PROBE_SHUTDOWN_BUDGET_MS = 5000;

// ───── Probe implementation ─────

namespace {

bool send_ipc(host_ipc_header* h, void* buf, uint32_t cmd, int timeout_ms) {
    h->status  = HOST_STATUS_IDLE;

    // Flush the command-data window so the kernel's page-walked view
    // sees zeros before we publish the new command. Same cache-line
    // dance as the cheat's ipc_probe in driver_orchestrator.cpp.
    for (size_t off = 0x40; off < 0x200; off += 64) {
        _mm_clflush(reinterpret_cast<char*>(buf) + off);
    }
    _mm_sfence();

    h->command = cmd;
    _mm_clflush(buf);
    _mm_sfence();

    // Wall-clock-bounded poll. Sleep granularity early in startup is
    // ~15ms (timeBeginPeriod hasn't been called), so a counted-loop of
    // Sleep(1) iterations would massively over-wait — bound the deadline
    // by GetTickCount64 instead so the timeout means what it says.
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    while (::GetTickCount64() < deadline) {
        if (h->status == HOST_STATUS_SUCCESS || h->status == HOST_STATUS_ERROR) {
            break;
        }
        ::Sleep(1);
    }
    return h->status == HOST_STATUS_SUCCESS;
}

int run_driver_probe(bool request_shutdown) {
    // Page-aligned buffer pinned in physical memory. The kernel driver
    // uses CR3-based scanning of the target process address space to
    // find IPC_MAGIC; if Windows pages this buffer out before the
    // driver gets to it, the magic is gone. VirtualLock is best-effort
    // (it requires the working set to be large enough) but on a
    // standard desktop it succeeds and pins the single page we need.
    void* raw = ::VirtualAlloc(nullptr, HOST_IPC_TOTAL_SIZE,
                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!raw) {
        return HOST_PROBE_EXIT_NO_DRIVER;
    }
    ::VirtualLock(raw, HOST_IPC_TOTAL_SIZE);
    memset(raw, 0, HOST_IPC_TOTAL_SIZE);

    auto* h = reinterpret_cast<host_ipc_header*>(raw);
    h->magic   = HOST_IPC_MAGIC;
    h->version = HOST_IPC_VERSION;
    h->command = HOST_CMD_IDLE;
    h->status  = HOST_STATUS_IDLE;

    _mm_clflush(raw);
    _mm_sfence();

    // PING — does any driver answer at all?
    bool alive = send_ipc(h, raw, HOST_CMD_PING, HOST_PROBE_PING_BUDGET_MS);

    int exit_code = HOST_PROBE_EXIT_NO_DRIVER;
    if (alive) {
        exit_code = HOST_PROBE_EXIT_DRIVER_ALIVE;

        if (request_shutdown) {
            // Tell the existing driver to unload itself. If it
            // acknowledges, escalate the exit code so the loader knows
            // it needs to give the kernel a moment before the new
            // mapper goes in.
            if (send_ipc(h, raw, HOST_CMD_SHUTDOWN, HOST_PROBE_SHUTDOWN_BUDGET_MS)) {
                exit_code = HOST_PROBE_EXIT_DRIVER_SHUTDOWN;
            }
            // If SHUTDOWN didn't come back as SUCCESS we leave the
            // exit code at DRIVER_ALIVE — the loader treats that as
            // "there's a driver in the way that won't go quietly"
            // and decides for itself.
        }
    }

    // Burn the magic so the kernel doesn't latch onto a freed buffer
    // after we exit (page may sit in the freelist for a tick before
    // it's actually scrubbed).
    memset(raw, 0, HOST_IPC_TOTAL_SIZE);
    _mm_clflush(raw);
    _mm_sfence();

    ::VirtualUnlock(raw, HOST_IPC_TOTAL_SIZE);
    ::VirtualFree(raw, 0, MEM_RELEASE);
    return exit_code;
}

bool has_arg(const char* cmd_line, const char* needle) {
    if (!cmd_line || !needle) return false;
    return strstr(cmd_line, needle) != nullptr;
}

} // namespace

// ───── Entry point ─────

int main() {
    LPSTR cmd_line = ::GetCommandLineA();

    if (has_arg(cmd_line, HOST_PROBE_ARG)) {
        const bool want_shutdown = has_arg(cmd_line, HOST_PROBE_SHUTDOWN_ARG);
        return run_driver_probe(want_shutdown);
    }

    // Default: hollow target. Spawned suspended by RunPE; this code is
    // overwritten before it ever runs. The infinite sleep is just a
    // safety net so a misconfigured launch (started without --probe AND
    // without being hollowed) doesn't spin a CPU core.
    while (TRUE) {
        ::Sleep(10000);
    }
    return 0;
}
