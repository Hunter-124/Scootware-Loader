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

// ───── IPC protocol (kept in lockstep with FINAL-DRV/shared_memory_ipc.h — IPC v2 multi-slot) ─────
//
// Layout mirror of IPC_MEMORY + IPC_SLOT (shared_memory_ipc.h):
//
//   IPC_MEMORY (0x11010 bytes total):
//     0x00  magic         uint64   — must be IPC_MAGIC = 0x504D585F49504332
//     0x08  version       uint32   — must be IPC_VERSION = 2
//     0x0C  active_slots  uint32   — number of active slots (16)
//     0x10  slots[0..15]  IPC_SLOT — each 0x1100 bytes
//
//   IPC_SLOT (0x1100 bytes):
//     slot+0x00  slot_state  uint32  — SLOT_STATE_FREE(0) / SLOT_STATE_BUSY(1)
//     slot+0x04  status      uint32  — STATUS_IPC_*
//     slot+0x08  command     uint32  — CMD_*   ← driver reads THIS for the command
//     slot+0x0C  process_id  uint32
//     slot+0x10  cr3_cached  uint64
//     slot+0x18  cmd_data    union[0x30]
//     slot+0x48  reserved[0xB8]
//     slot+0x100 data_buffer[0x1000]
//
// Previous version used HOST_IPC_MAGIC = IPC_v1 (0x...31) and a flat
// host_ipc_header with command at 0x10 (= slots[0].slot_state) and
// status at 0x14 (= slots[0].status). The driver scans for IPC_v2 magic
// and reads command from slots[0].command (0x18), so:
//   * wrong magic  → driver never finds the probe buffer → always NO_DRIVER
//   * wrong command offset → even if magic matched, PING written to
//     slot_state (0x10), not command (0x18) → driver never processes it

#define HOST_IPC_MAGIC          0x504D585F49504332ULL   // IPC_MAGIC  ('PMX_IPC2')
#define HOST_IPC_VERSION        2                       // IPC_VERSION
#define HOST_IPC_MAX_SLOTS      16                      // IPC_MAX_SLOTS
#define HOST_SLOT_SIZE          0x1100                  // sizeof(IPC_SLOT)
#define HOST_IPC_TOTAL_SIZE     (0x10 + HOST_IPC_MAX_SLOTS * HOST_SLOT_SIZE)  // 0x11010

#define HOST_CMD_IDLE           0
#define HOST_CMD_PING           8
#define HOST_CMD_SHUTDOWN       9

#define HOST_STATUS_IDLE        0
#define HOST_STATUS_PROCESSING  1
#define HOST_STATUS_SUCCESS     2
#define HOST_STATUS_ERROR       3

#define HOST_SLOT_STATE_FREE    0
#define HOST_SLOT_STATE_BUSY    1

// Mirrors IPC_MEMORY header (0x10 bytes)
#pragma pack(push, 1)
struct host_mem_header {
    uint64_t magic;          // 0x00
    uint32_t version;        // 0x08
    uint32_t active_slots;   // 0x0C
};

// Mirrors the fields of IPC_SLOT we actually touch (slot starts at header+0x10)
struct host_ipc_slot {
    uint32_t slot_state;     // slot+0x00
    uint32_t status;         // slot+0x04
    uint32_t command;        // slot+0x08  ← driver reads command here
    uint32_t process_id;     // slot+0x0C
    uint64_t cr3_cached;     // slot+0x10
    // remaining bytes up to HOST_SLOT_SIZE are zeroed by memset
};

// Combined view used by the probe (slots[] trails the header in memory)
struct host_ipc_memory {
    host_mem_header header;                     // 0x00 – 0x0F
    host_ipc_slot   slots[HOST_IPC_MAX_SLOTS];  // 0x10 – (each struct is smaller
                                                //  than HOST_SLOT_SIZE, but the
                                                //  full allocation is HOST_IPC_TOTAL_SIZE
                                                //  so padding between slots is covered
                                                //  by the initial memset)
};
#pragma pack(pop)

// PING budget for the probe. Short on purpose: a driver that's actually
// loaded answers in well under 100ms (it's just a "set status = SUCCESS"
// in the dispatch loop), so 5s gives ample margin for any scheduling
// jitter while still being short enough to not stall a cold launch for
// too long when no driver is present.
constexpr int HOST_PROBE_PING_BUDGET_MS     = 5000;

// SHUTDOWN budget. The driver's unload path is short but does have to
// wait for any in-flight IRPs to drain, so 5s is the conservative
// upper bound.
constexpr int HOST_PROBE_SHUTDOWN_BUDGET_MS = 5000;

// ───── Static IPC storage ──────────────────────────────────────────────────
//
// CRITICAL: this buffer MUST be a static global embedded within the PE image.
//
// The driver's find_ipc_buffer() scans the target process in 4 KiB steps
// from image_base to image_base+SizeOfImage (the range reported by the PE
// headers). It is looking for IPC_MAGIC at the first qword of each page.
//
// A VirtualAlloc'd buffer lives at an arbitrary heap/free-region address,
// which is completely outside the image scan range — the driver NEVER sees
// it. A static global with __declspec(align(4096)) is placed inside the
// .bss section by the linker, at a page-aligned offset within the image.
// The scan hits it, reads IPC_MAGIC, verifies IPC_VERSION, and maps it.
//
// This is exactly what ipc_speed_test.cpp and IPC-Interface/main.cpp do:
//   __declspec(align(4096)) static char g_ipc_storage[IPC_TOTAL_SIZE];
//
// The full HOST_IPC_TOTAL_SIZE (0x11010 bytes = IPC_MEMORY) is allocated so
// the driver's IoAllocateMdl(ipc_va, IPC_TOTAL_SIZE) call covers the whole
// structure without reading past the end of our allocation.
//
__declspec(align(4096)) static char g_probe_ipc_storage[HOST_IPC_TOTAL_SIZE];

// ───── Probe implementation ─────

namespace {

uint64_t now_ns() {
    static double qpc_to_ns = 0.0;
    if (qpc_to_ns == 0.0) {
        LARGE_INTEGER f;
        ::QueryPerformanceFrequency(&f);
        qpc_to_ns = 1e9 / static_cast<double>(f.QuadPart);
    }
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(static_cast<double>(c.QuadPart) * qpc_to_ns);
}

// Send a command on slot 0 and wait for the driver to respond.
// `mem` is the typed view of the IPC buffer; `raw` is the same pointer
// as a raw byte pointer for cache-line flushes.
bool send_ipc(host_ipc_memory* mem, void* raw, uint32_t cmd, int timeout_ms) {
    host_ipc_slot* s = &mem->slots[0];

    // Reset status and command before flushing so the kernel's page-walked
    // view sees a clean slate before we arm the new command.
    s->status  = HOST_STATUS_IDLE;
    s->command = HOST_CMD_IDLE;

    // Flush the header + slot 0 area (covers magic through cmd_data window).
    for (size_t off = 0; off < 0x80; off += 64) {
        _mm_clflush(reinterpret_cast<char*>(raw) + off);
    }
    _mm_sfence();

    // Arm the command. The driver worker checks:
    //   if (cmd != CMD_IDLE && st == STATUS_IPC_IDLE) → process
    // command is at slot+0x08 (= buffer+0x18); status is at slot+0x04.
    s->command = cmd;
    _mm_clflush(reinterpret_cast<char*>(raw) + 0x10);  // flush slot 0 header
    _mm_sfence();

    const uint64_t start_ns = now_ns();
    const uint64_t deadline_ns = start_ns + static_cast<uint64_t>(timeout_ms) * 1000000ull;

    // Phase 1: High-performance tight spin (budgeted up to 3ms for quick processing)
    const uint64_t phase1_deadline_ns = start_ns + 3000000ull;
    while (now_ns() < phase1_deadline_ns) {
        UINT32 st = s->status;
        if (st == HOST_STATUS_SUCCESS || st == HOST_STATUS_ERROR) {
            return st == HOST_STATUS_SUCCESS;
        }
        _mm_pause();
    }

    // Phase 2: Cooperative yield using SwitchToThread (budgeted up to 10ms)
    const uint64_t phase2_deadline_ns = start_ns + 10000000ull;
    while (now_ns() < phase2_deadline_ns && now_ns() < deadline_ns) {
        UINT32 st = s->status;
        if (st == HOST_STATUS_SUCCESS || st == HOST_STATUS_ERROR) {
            return st == HOST_STATUS_SUCCESS;
        }
        ::SwitchToThread();
    }

    // Phase 3: Sleep(1) for power/scheduler friendliness on longer ops
    while (now_ns() < deadline_ns) {
        UINT32 st = s->status;
        if (st == HOST_STATUS_SUCCESS || st == HOST_STATUS_ERROR) {
            return st == HOST_STATUS_SUCCESS;
        }
        ::Sleep(1);
    }
    return false;
}

int run_driver_probe(bool request_shutdown) {
    // Use the static page-aligned storage declared above.
    //
    // The driver's find_ipc_buffer() scans image_base → image_base+SizeOfImage
    // in 4 KiB steps. g_probe_ipc_storage is a static .bss global inside this
    // image, so it is within the scan range. A VirtualAlloc'd buffer at a random
    // heap address would be completely invisible to the scan.
    //
    // Touch every page of the storage with memset before writing the magic.
    // g_probe_ipc_storage is demand-paged (.bss); until first access the PTEs
    // are not present and translate_linearBE returns 0. The memset below faults
    // all pages in so the driver's subsequent physical scan can actually find them.
    //
    // No VirtualLock is needed: the driver uses MmProbeAndLockPages (called from
    // the discovery thread) which pins the pages into physical memory for the
    // lifetime of the MDL — it will fault the pages back in if they were trimmed
    // in the brief window between our write and its MDL construction.
    auto* mem = reinterpret_cast<host_ipc_memory*>(g_probe_ipc_storage);

    memset(g_probe_ipc_storage, 0, HOST_IPC_TOTAL_SIZE);

    // Initialise IPC_MEMORY header (matches IPC_MEMORY in shared_memory_ipc.h)
    mem->header.magic        = HOST_IPC_MAGIC;
    mem->header.version      = HOST_IPC_VERSION;
    mem->header.active_slots = HOST_IPC_MAX_SLOTS;

    // Initialise slot 0 (the probe only uses slot 0)
    mem->slots[0].slot_state  = HOST_SLOT_STATE_FREE;
    mem->slots[0].status      = HOST_STATUS_IDLE;
    mem->slots[0].command     = HOST_CMD_IDLE;
    mem->slots[0].process_id  = 0;
    mem->slots[0].cr3_cached  = 0;

    // Full fence + flush so the in-cache writes are visible to the kernel's
    // physical read path before the PING poll below starts.
    _mm_sfence();
    _mm_clflush(g_probe_ipc_storage);
    _mm_sfence();

    // PING — does any driver answer at all?
    bool alive = send_ipc(mem, g_probe_ipc_storage, HOST_CMD_PING, HOST_PROBE_PING_BUDGET_MS);

    int exit_code = HOST_PROBE_EXIT_NO_DRIVER;
    if (alive) {
        exit_code = HOST_PROBE_EXIT_DRIVER_ALIVE;

        if (request_shutdown) {
            // Tell the existing driver to unload itself. If it
            // acknowledges, escalate the exit code so the loader knows
            // it needs to give the kernel a moment before the new
            // mapper goes in.
            if (send_ipc(mem, g_probe_ipc_storage, HOST_CMD_SHUTDOWN, HOST_PROBE_SHUTDOWN_BUDGET_MS)) {
                exit_code = HOST_PROBE_EXIT_DRIVER_SHUTDOWN;
            }
            // If SHUTDOWN didn't come back as SUCCESS we leave the
            // exit code at DRIVER_ALIVE — the loader treats that as
            // "there's a driver in the way that won't go quietly"
            // and decides for itself.
        }
    }

    // Burn the magic before returning so the kernel's discovery thread doesn't
    // latch onto this buffer after the probe exits (the static page may remain
    // mapped in the freelist briefly before reuse).
    memset(g_probe_ipc_storage, 0, HOST_IPC_TOTAL_SIZE);
    _mm_sfence();

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
