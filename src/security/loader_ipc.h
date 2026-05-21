#pragma once
//
// loader_ipc.h
//
// In-process IPC channel between the loader (scootware-loader.exe) and the
// kernel driver. Replaces the historical "spawn a separate scootware.exe
// probe" trick now that the driver's target_names table accepts the loader
// image directly (FINAL-DRV/driver.cpp -> find_target_process_with_ipc).
//
// Protocol is identical to the speed-test client (IPC_MEMORY / IPC_SLOT in
// FINAL-DRV/shared_memory_ipc.h, IPC_VERSION = 2). Only slot 0 is used —
// the loader never issues concurrent commands.
//
// CRITICAL placement requirement:
//
//   The driver's find_ipc_buffer() walks our PE image base..base+SizeOfImage
//   in 4 KiB strides looking for IPC_MAGIC at the first qword of each page.
//   A heap / VirtualAlloc allocation lives outside that range and is
//   invisible to the scan. The IPC storage MUST therefore be a static
//   __declspec(align(4096)) global so the linker places it in .bss inside
//   the image. See loader_ipc.cpp.
//
// Lifecycle:
//
//   LoaderIpc::Init()                  -> arms magic; idempotent
//   LoaderIpc::Ping(timeoutMs)         -> true if a driver is attached
//   LoaderIpc::RequestShutdown(t_ms)   -> ask the attached driver to unload
//   LoaderIpc::WaitForDriverGone(t_ms) -> poll Ping() until it fails
//   LoaderIpc::Release()               -> burn the magic so the driver's
//                                         next re-scan tick will skip us
//                                         and pick up scootware.exe instead
//
// Handoff to scootware.exe:
//
//   The driver only re-scans for a new target when its current g_test_process
//   exits or its IPC buffer disappears. So the natural handoff sequence is:
//
//     1. loader keeps its IPC alive while the cheat is being spawned
//     2. loader calls Release() to zero the magic (so a future re-scan
//        won't latch back onto us if the page lingers)
//     3. loader exits — driver's polling loop observes the target exit
//        within ~50 ms, tears down the loader session, re-scans, and
//        picks up scootware.exe
//

#include <cstdint>

namespace LoaderIpc {

    // Initialise the in-image IPC buffer (zero, set IPC_MAGIC + version,
    // prep slot 0). Safe to call more than once. Cheap.
    bool Init();

    // CMD_PING with a wall-clock-bounded poll. Returns true iff the driver
    // flipped slot 0 status to STATUS_IPC_SUCCESS within the budget.
    // A false return is the canonical "no driver attached" signal.
    bool Ping(uint32_t timeout_ms);

    // CMD_SHUTDOWN. The driver's dispatcher acks success and then tears
    // its own state down. Caller should follow with WaitForDriverGone.
    bool RequestShutdown(uint32_t timeout_ms);

    // Poll Ping() until it fails (driver gone) or the budget expires.
    // Returns true once the driver stops responding.
    bool WaitForDriverGone(uint32_t total_timeout_ms);

    // Zero the magic so the driver's re-scan path skips this page.
    // Does not free the storage — the static .bss array lives for the
    // process lifetime. Idempotent.
    void Release();

    // CMD_HANDOFF: pre-announce the cheat's PID and IPC buffer VA to the
    // driver before calling Release(). The driver stores the hint and uses
    // it to establish the cheat session directly (no EPROCESS scan needed).
    // Returns true iff the driver acknowledged the command within timeout_ms.
    // A false return is non-fatal — the driver falls back to its normal scan.
    bool NotifyHandoff(uint32_t cheat_pid, uint64_t ipc_va,
                       uint32_t timeout_ms = 3000);

    // ── HWID Spoofing ────────────────────────────────────────────────────────
    // All HWID commands require Init() + a live driver (Ping succeeds).
    // They all use slot 0 and are NOT thread-safe with each other.

    bool HwidSave(uint32_t timeout_ms = 10000);
    bool HwidSpoof(uint32_t components = 0xFFFFFFFF, uint64_t seed = 0,
                   uint32_t timeout_ms = 15000);
    bool HwidRestore(uint32_t timeout_ms = 15000);
    bool HwidReroll(uint32_t components = 0xFFFFFFFF,
                    uint32_t timeout_ms = 15000);

    struct HwidStatus {
        bool     ok       = false;
        bool     spoofed  = false;   // driver reports spoof active
        uint32_t state    = 0;       // raw state enum from driver
        uint8_t  smbios_uuid[16]          = {};
        char     system_serial[64]        = {};
        char     baseboard_serial[64]     = {};
        char     machine_guid[40]         = {};
        char     mac_address[18]          = {};
        char     volume_serial[128]       = {};
    };
    bool HwidQueryStatus(HwidStatus& out, uint32_t timeout_ms = 10000);

    // ── Benchmarks ───────────────────────────────────────────────────────────
    // Synchronous; call from a background thread.

    struct BenchSample {
        size_t n     = 0;
        double avg_us = 0, min_us = 0, p50_us = 0,
               p95_us = 0, p99_us = 0, max_us = 0;
    };

    // CMD_PING round-trip latency benchmark.
    bool BenchPing(int iterations, BenchSample& out);

    // Single-slot R/W latency benchmark against our own process memory.
    bool BenchRwLatency(size_t size_bytes, int iterations,
                        BenchSample& write_out, BenchSample& read_out);

    // ── Kernel DLL injection ─────────────────────────────────────────────────
    //
    // CMD_INJECT_DLL via the in-image IPC. The driver reads `dll_bytes`
    // across the loader's address space (it is attached to us by image
    // name), maps the PE into `target_pid` using the requested alloc mode,
    // and runs DllMain via RtlCreateUserThread. The DLL bytes are wiped
    // by the caller right after this returns — there is no on-disk artifact
    // and the plaintext is gone before the loader exits.
    //
    // alloc_mode must be one of these (mirrored from injector_api.hpp; we
    // do not include the driver header from user mode):
    enum InjAllocMode : uint32_t {
        INJ_ALLOC_INSIDE_MAIN_MODULE     = 0,
        INJ_ALLOC_BETWEEN_LEGIT_MODULES  = 1,
        INJ_ALLOC_AT_LOW_ADDRESS         = 2,
        INJ_ALLOC_AT_HIGH_ADDRESS        = 3,
        // INJ_ALLOC_AT_HYPERSPACE (4) is disabled for KDU-mapped drivers
    };

    // Returns true iff the driver acknowledged STATUS_IPC_SUCCESS within
    // the timeout. On success, `out_remote_base` (optional) receives the
    // VA of the mapped DLL in the target process — useful for diag only;
    // the loader has no reason to do anything with it.
    bool InjectDll(uint32_t target_pid,
                   const void* dll_bytes,
                   uint32_t dll_size,
                   uint32_t alloc_mode,
                   uint64_t* out_remote_base = nullptr,
                   uint32_t timeout_ms = 15000);

    // ── Generic memory R/W (single shot) ─────────────────────────────────────
    // Used by the Settings color-tile validation test. Operates against the
    // loader's own process memory; addr is a VA inside this process. size
    // must fit a single slot (<= IPC_SLOT_DATA_SIZE). Both routines block
    // until the driver acks STATUS_IPC_SUCCESS or timeout_ms expires.
    bool MemWrite(uint64_t addr, const void* src, size_t size,
                  bool use_cr3 = false, uint32_t timeout_ms = 2000);
    bool MemRead (uint64_t addr, void* dst, size_t size,
                  bool use_cr3 = false, uint32_t timeout_ms = 2000);

} // namespace LoaderIpc
