#include "loader_ipc.h"

#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <vector>
#include <numeric>
#include <algorithm>

#include "../util/diaglog.h"

// Pulled from FINAL-DRV/shared_memory_ipc.h. CMakeLists.txt adds
// FINAL-DRV/ as a private include directory for the loader target so this
// stays a single source of truth — bumping IPC_VERSION there can't drift
// out of sync with the loader the way the duplicated constants in
// host/host.cpp historically did.
#include "shared_memory_ipc.h"

namespace {

// Static, page-aligned, in-image. See header comment for *why* a heap
// allocation does not work here. .bss zero-init means we still have to
// touch every page once before the driver scans (otherwise the PTEs are
// not present and translate_linearBE returns 0 for the unbacked pages);
// Init() does that with a memset.
__declspec(align(4096)) char g_ipc_storage[IPC_TOTAL_SIZE];

PIPC_MEMORY mem() {
    return reinterpret_cast<PIPC_MEMORY>(g_ipc_storage);
}

std::atomic<bool> g_initialised{false};

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

// Send a command on slot 0 and wait wall-clock-bounded. The driver's
// dispatch contract is "command != IDLE && status == IDLE → process";
// the three-step arming here exists so the driver never sees a stale
// command paired with a fresh status (see ipc_speed_test.cpp's comment
// for the torn-data scenario this avoids).
bool send_slot0_command(UINT32 cmd, uint32_t timeout_ms) {
    PIPC_SLOT s = &mem()->slots[0];

    s->command = CMD_IDLE;
    MemoryBarrier();
    s->process_id = ::GetCurrentProcessId();
    s->status     = STATUS_IPC_IDLE;
    MemoryBarrier();
    s->command    = cmd;

    // Make the writes visible to the driver's physical-memory read path
    // before we start polling. The driver walks a CR3 we built from the
    // EPROCESS dirbase and reads cached, so a clflush + sfence on our
    // side is the conservative belt-and-suspenders approach the host
    // probe also uses.
    for (size_t off = 0; off < 0x100; off += 64) {
        _mm_clflush(g_ipc_storage + off);
    }
    _mm_sfence();

    const uint64_t start_ns = now_ns();
    const uint64_t deadline_ns = start_ns + static_cast<uint64_t>(timeout_ms) * 1000000ull;

    // Phase 1: High-performance tight spin (budgeted up to 3ms for quick processing)
    const uint64_t phase1_deadline_ns = start_ns + 3000000ull;
    while (now_ns() < phase1_deadline_ns) {
        UINT32 st = s->status;
        if (st == STATUS_IPC_SUCCESS) return true;
        if (st == STATUS_IPC_ERROR)   return false;
        _mm_pause();
    }

    // Phase 2: Cooperative yield using SwitchToThread (budgeted up to 10ms)
    const uint64_t phase2_deadline_ns = start_ns + 10000000ull;
    while (now_ns() < phase2_deadline_ns && now_ns() < deadline_ns) {
        UINT32 st = s->status;
        if (st == STATUS_IPC_SUCCESS) return true;
        if (st == STATUS_IPC_ERROR)   return false;
        ::SwitchToThread();
    }

    // Phase 3: Sleep(1) for power/scheduler friendliness on longer ops
    while (now_ns() < deadline_ns) {
        UINT32 st = s->status;
        if (st == STATUS_IPC_SUCCESS) return true;
        if (st == STATUS_IPC_ERROR)   return false;
        ::Sleep(1);
    }
    return false;
}

} // namespace

namespace LoaderIpc {

bool Init() {
    if (g_initialised.load(std::memory_order_acquire)) {
        // Re-arm the header in case Release() burned the magic between
        // calls. Cheap and defensive.
        mem()->magic        = IPC_MAGIC;
        mem()->version      = IPC_VERSION;
        mem()->active_slots = IPC_MAX_SLOTS;
        for (size_t off = 0; off < 0x100; off += 64) {
            _mm_clflush(g_ipc_storage + off);
        }
        _mm_sfence();
        return true;
    }

    // Touch every page so the .bss demand-paged PTEs become present
    // before the driver's physical scan runs. Without this the kernel
    // would walk our PE range, hit unmapped PTEs at our static, and
    // skip past the would-be IPC page.
    std::memset(g_ipc_storage, 0, IPC_TOTAL_SIZE);

    PIPC_MEMORY m = mem();
    m->magic        = IPC_MAGIC;
    m->version      = IPC_VERSION;
    m->active_slots = IPC_MAX_SLOTS;

    PIPC_SLOT s = &m->slots[0];
    s->slot_state = SLOT_STATE_FREE;
    s->status     = STATUS_IPC_IDLE;
    s->command    = CMD_IDLE;
    s->process_id = ::GetCurrentProcessId();
    s->cr3_cached = 0;

    // Full flush so the kernel sees the magic without waiting for cache
    // eviction. The driver's IoAllocateMdl + MmMapLockedPagesSpecifyCache
    // path uses MmCached, so coherency is guaranteed once the line is
    // written back.
    _mm_sfence();
    for (size_t off = 0; off < 0x100; off += 64) {
        _mm_clflush(g_ipc_storage + off);
    }
    _mm_sfence();

    g_initialised.store(true, std::memory_order_release);

    LDIAG() << "[loader-ipc] armed at " << static_cast<void*>(g_ipc_storage)
            << " (magic=0x" << std::hex << IPC_MAGIC
            << " version=" << std::dec << IPC_VERSION << ")";
    return true;
}

bool Ping(uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) {
        return false;
    }
    return send_slot0_command(CMD_PING, timeout_ms);
}

bool RequestShutdown(uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) {
        return false;
    }
    return send_slot0_command(CMD_SHUTDOWN, timeout_ms);
}

bool WaitForDriverGone(uint32_t total_timeout_ms) {
    const ULONGLONG deadline = ::GetTickCount64() +
                               static_cast<ULONGLONG>(total_timeout_ms);
    while (::GetTickCount64() < deadline) {
        // Short per-poll budget so we react quickly once the driver
        // stops answering. 200 ms covers normal scheduler jitter while
        // keeping the worst-case wall time roughly == total_timeout_ms.
        if (!Ping(200)) {
            return true;
        }
        ::Sleep(50);
    }
    return false;
}

void Release() {
    if (!g_initialised.load(std::memory_order_acquire)) return;

    // Burn the magic so any subsequent driver re-scan walks past us and
    // picks up the cheat (scootware.exe) instead. We deliberately leave
    // the storage allocated — there is no benefit to freeing a 64 KiB
    // .bss region right before process exit, and any remaining MDL
    // mapping the driver still holds will simply read zeros (cmd=IDLE,
    // status=IDLE) until the driver notices our process exit and tears
    // it down on its own.
    std::memset(g_ipc_storage, 0, IPC_TOTAL_SIZE);
    _mm_sfence();
    _mm_clflush(g_ipc_storage);
    _mm_sfence();

    g_initialised.store(false, std::memory_order_release);
    LDIAG_LINE("[loader-ipc] released (magic burned)");
}

bool NotifyHandoff(uint32_t cheat_pid, uint64_t ipc_va, uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    if (cheat_pid == 0 || ipc_va == 0) return false;

    // Write handoff parameters into slot 0's cmd_data BEFORE arming the slot.
    // send_slot0_command only touches command/status/process_id, so pre-written
    // cmd_data survives to when the driver picks up the command.
    auto* s = &mem()->slots[0];
    s->cmd_data.handoff.target_pid = cheat_pid;
    s->cmd_data.handoff.reserved   = 0;
    s->cmd_data.handoff.ipc_va     = ipc_va;
    MemoryBarrier();

    bool ok = send_slot0_command(CMD_HANDOFF, timeout_ms);
    LDIAG() << "[loader-ipc] CMD_HANDOFF pid=" << cheat_pid
            << " va=0x" << std::hex << ipc_va << std::dec
            << " -> " << (ok ? "ACK" : "TIMEOUT/ERROR");
    return ok;
}


// ── HWID Spoofing ─────────────────────────────────────────────────────────────
//
// cmd_data is written BEFORE send_slot0_command arms the slot. The three-step
// protocol in send_slot0_command only touches command/status/process_id, so
// pre-written cmd_data survives to when the driver picks up the command.

bool HwidSave(uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    return send_slot0_command(CMD_HWID_SAVE, timeout_ms);
}

bool HwidSpoof(uint32_t components, uint64_t seed, uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    auto* s = &mem()->slots[0];
    s->cmd_data.hwid_cmd.components  = components;
    s->cmd_data.hwid_cmd.random_seed = seed;
    MemoryBarrier();
    return send_slot0_command(CMD_HWID_SPOOF, timeout_ms);
}

bool HwidRestore(uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    return send_slot0_command(CMD_HWID_RESTORE, timeout_ms);
}

bool HwidReroll(uint32_t components, uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    auto* s = &mem()->slots[0];
    s->cmd_data.hwid_cmd.components = components;
    MemoryBarrier();
    return send_slot0_command(CMD_HWID_REROLL, timeout_ms);
}

bool HwidQueryStatus(HwidStatus& out, uint32_t timeout_ms) {
    out = {};
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    if (!send_slot0_command(CMD_HWID_STATUS, timeout_ms)) return false;

    auto* s  = &mem()->slots[0];
    auto* hw = &s->cmd_data.hwid_cmd;
    out.ok      = true;
    out.state   = hw->state;
    out.spoofed = hw->active != 0;

    // HWID data is returned in the slot's data_buffer as IPC_HWID_DATA.
    const auto* d = reinterpret_cast<const IPC_HWID_DATA*>(s->data_buffer);
    memcpy(out.smbios_uuid,       d->smbios_uuid,         sizeof(out.smbios_uuid));
    memcpy(out.system_serial,     d->smbios_system_serial, sizeof(out.system_serial));
    memcpy(out.baseboard_serial,  d->smbios_baseboard_serial, sizeof(out.baseboard_serial));
    memcpy(out.machine_guid,      d->machine_guid,         sizeof(out.machine_guid));
    memcpy(out.mac_address,       d->mac_address,          sizeof(out.mac_address));
    memcpy(out.volume_serial,     d->volume_serial,        sizeof(out.volume_serial));
    // Null-terminate all strings defensively.
    out.system_serial[sizeof(out.system_serial)-1]       = '\0';
    out.baseboard_serial[sizeof(out.baseboard_serial)-1] = '\0';
    out.machine_guid[sizeof(out.machine_guid)-1]         = '\0';
    out.mac_address[sizeof(out.mac_address)-1]           = '\0';
    out.volume_serial[sizeof(out.volume_serial)-1]       = '\0';
    return true;
}

// ── Benchmarks ────────────────────────────────────────────────────────────────

static double qpc_to_ns() {
    static double v = 0.0;
    if (v == 0.0) {
        LARGE_INTEGER f; ::QueryPerformanceFrequency(&f);
        v = 1e9 / (double)f.QuadPart;
    }
    return v;
}

static uint64_t bench_now_ns() {
    LARGE_INTEGER c; ::QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * qpc_to_ns());
}

static BenchSample compute_bench_sample(std::vector<uint64_t>& s) {
    BenchSample out{};
    if (s.empty()) return out;
    std::sort(s.begin(), s.end());
    out.n      = s.size();
    out.min_us = s.front()                         / 1000.0;
    out.max_us = s.back()                          / 1000.0;
    out.p50_us = s[(size_t)(0.50 * (s.size()-1))] / 1000.0;
    out.p95_us = s[(size_t)(0.95 * (s.size()-1))] / 1000.0;
    out.p99_us = s[(size_t)(0.99 * (s.size()-1))] / 1000.0;
    out.avg_us = std::accumulate(s.begin(), s.end(), 0.0) / (double)s.size() / 1000.0;
    return out;
}

bool BenchPing(int iterations, BenchSample& out) {
    out = {};
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    std::vector<uint64_t> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        uint64_t t0 = bench_now_ns();
        if (!send_slot0_command(CMD_PING, 2000)) return false;
        samples.push_back(bench_now_ns() - t0);
    }
    out = compute_bench_sample(samples);
    return true;
}

bool BenchRwLatency(size_t size_bytes, int iterations,
                    BenchSample& write_out, BenchSample& read_out) {
    write_out = read_out = {};
    if (!g_initialised.load(std::memory_order_acquire)) return false;

    // Allocate a scratch buffer in our own process for the driver to R/W.
    // The driver reads/writes our process's memory; send_slot0_command already
    // sets process_id = GetCurrentProcessId() in the slot.
    std::vector<uint8_t> scratch(size_bytes, 0xA5);
    uint64_t addr = (uint64_t)scratch.data();

    auto* s  = &mem()->slots[0];
    auto* rw = &s->cmd_data.rw;
    std::vector<uint64_t> wr_ns, rd_ns;
    wr_ns.reserve(iterations);
    rd_ns.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        // Write
        size_t chunk = (std::min)(size_bytes, (size_t)IPC_SLOT_DATA_SIZE);
        memcpy(s->data_buffer, scratch.data(), chunk);
        rw->target_address = addr;
        rw->buffer_size    = chunk;
        rw->is_write       = 1;
        rw->use_cr3        = 0;
        MemoryBarrier();
        uint64_t t0 = bench_now_ns();
        if (!send_slot0_command(CMD_WRITE_MEMORY, 5000)) return false;
        wr_ns.push_back(bench_now_ns() - t0);

        // Read
        rw->target_address = addr;
        rw->buffer_size    = chunk;
        rw->is_write       = 0;
        rw->use_cr3        = 0;
        MemoryBarrier();
        uint64_t t1 = bench_now_ns();
        if (!send_slot0_command(CMD_READ_MEMORY, 5000)) return false;
        rd_ns.push_back(bench_now_ns() - t1);
    }

    write_out = compute_bench_sample(wr_ns);
    read_out  = compute_bench_sample(rd_ns);
    return true;
}

bool MemWrite(uint64_t addr, const void* src, size_t size,
              bool use_cr3, uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    if (size == 0 || size > IPC_SLOT_DATA_SIZE) return false;
    auto* s  = &mem()->slots[0];
    memcpy(s->data_buffer, src, size);
    auto* rw = &s->cmd_data.rw;
    rw->target_address = addr;
    rw->buffer_size    = size;
    rw->is_write       = 1;
    rw->use_cr3        = use_cr3 ? 1 : 0;
    MemoryBarrier();
    return send_slot0_command(CMD_WRITE_MEMORY, timeout_ms);
}

bool InjectDll(uint32_t target_pid,
               const void* dll_bytes,
               uint32_t dll_size,
               uint32_t alloc_mode,
               uint64_t* out_remote_base,
               uint32_t timeout_ms) {
    if (out_remote_base) *out_remote_base = 0;
    if (!g_initialised.load(std::memory_order_acquire)) {
        LDIAG_LINE("[loader-ipc] InjectDll: IPC not initialised");
        return false;
    }
    if (!target_pid || !dll_bytes || !dll_size) {
        LDIAG() << "[loader-ipc] InjectDll: rejected invalid args"
                << " (pid=" << target_pid << " size=" << dll_size
                << " ptr=" << dll_bytes << ")";
        return false;
    }
    // Driver caps CMD_INJECT_DLL at MM_MAX_DLL_SIZE (32 MiB) — fail fast
    // on this side so we don't burn an IPC round-trip on a request the
    // driver is going to reject.
    if (dll_size > 32u * 1024u * 1024u) {
        LDIAG() << "[loader-ipc] InjectDll: dll too large (" << dll_size
                << " > 32 MiB)";
        return false;
    }

    auto* s = &mem()->slots[0];
    s->cmd_data.inject.target_pid       = target_pid;
    s->cmd_data.inject.dll_usermode_ptr = reinterpret_cast<UINT64>(dll_bytes);
    s->cmd_data.inject.dll_size         = dll_size;
    s->cmd_data.inject.alloc_mode       = alloc_mode;
    MemoryBarrier();

    bool ok = send_slot0_command(CMD_INJECT_DLL, timeout_ms);
    UINT64 remote = ok ? s->cmd_data.result.result : 0;
    if (out_remote_base) *out_remote_base = remote;

    LDIAG() << "[loader-ipc] CMD_INJECT_DLL pid=" << target_pid
            << " size=" << dll_size
            << " alloc_mode=" << alloc_mode
            << " -> " << (ok ? "ACK" : "TIMEOUT/ERROR")
            << " remote_base=0x" << std::hex << remote << std::dec;
    return ok;
}

bool MemRead(uint64_t addr, void* dst, size_t size,
             bool use_cr3, uint32_t timeout_ms) {
    if (!g_initialised.load(std::memory_order_acquire)) return false;
    if (size == 0 || size > IPC_SLOT_DATA_SIZE) return false;
    auto* s  = &mem()->slots[0];
    auto* rw = &s->cmd_data.rw;
    rw->target_address = addr;
    rw->buffer_size    = size;
    rw->is_write       = 0;
    rw->use_cr3        = use_cr3 ? 1 : 0;
    MemoryBarrier();
    if (!send_slot0_command(CMD_READ_MEMORY, timeout_ms)) return false;
    memcpy(dst, s->data_buffer, size);
    return true;
}

} // namespace LoaderIpc
