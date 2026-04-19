/*
 * vmdetect.cpp  –  Multi-vector VM / sandbox + anti-debug detection
 *
 * VM Checks (in order):
 *  1.  CPUID hypervisor present bit  (ECX bit 31, leaf 0x1)
 *  2.  CPUID hypervisor vendor string (leaf 0x40000000)
 *  3.  Known VM registry keys
 *  4.  Known VM process names
 *  5.  Known VM service names
 *  6.  Suspicious MAC address OUI prefixes (VMware / VirtualBox / Hyper-V / QEMU)
 *  7.  RDTSC timing – VM exits inflate the delta
 *
 * Anti-Debug Checks (in order):
 *  8.  IsDebuggerPresent() (PEB.BeingDebugged)
 *  9.  CheckRemoteDebuggerPresent()
 *  10. NtQueryInformationProcess – ProcessDebugPort
 *  11. Heap flags (debug heap sets extra flags in the PEB heap header)
 *  12. Hardware breakpoints via GetThreadContext (Dr0-Dr3)
 *  13. Known debugger / analysis-tool process names
 */

#include "vmdetect.h"

#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")

// ---------------------------------------------------------------------------
// NtQueryInformationProcess typedef (not always exposed in headers)
// ---------------------------------------------------------------------------
typedef NTSTATUS(NTAPI* pfnNtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// ── Helper: wstring -> UTF-8 std::string ─────────────────────────────────
static std::string WtoA(const wchar_t* ws)
{
    int sz = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 1) return {};
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], sz, nullptr, nullptr);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
//  VM CHECKS
// ═══════════════════════════════════════════════════════════════════════════

// ── 1. CPUID hypervisor present bit ─────────────────────────────────────
// REMOVED: This bit is set on any Windows machine with Hyper-V, VBS,
// Credential Guard, or WSL2 active — i.e. virtually all modern Windows
// 10/11 installs.  It is not a reliable indicator of a VM environment.
// Kept as a stub so call-site numbering in comments stays consistent.
bool CheckCPUIDHypervisorBit()
{
    return false; // disabled — too many false positives on bare metal
}

// ── 2. CPUID hypervisor vendor string ────────────────────────────────────
// "Microsoft Hv" is intentionally excluded: Windows itself advertises it
// when VBS / Hyper-V / Credential Guard is active on bare-metal hardware.
bool CheckHypervisorVendorString(std::string& outVendor)
{
    int regs[4] = {};
    __cpuid(regs, 0x40000000);

    char vendor[13] = {};
    memcpy(vendor + 0, &regs[1], 4);
    memcpy(vendor + 4, &regs[2], 4);
    memcpy(vendor + 8, &regs[3], 4);
    vendor[12] = '\0';
    outVendor  = vendor;

    static const char* known[] = {
        "KVMKVMKVM",    // KVM (Linux)
        // "Microsoft Hv" excluded — native Windows VBS/Hyper-V on bare metal
        "VMwareVMware", // VMware
        "VBoxVBoxVBox", // VirtualBox
        "XenVMMXenVMM", // Xen
        "prl hyperv",   // Parallels
        "TCGTCGTCGTCG", // QEMU/TCG
        nullptr
    };

    std::string lower = outVendor;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (int i = 0; known[i]; ++i) {
        std::string k = known[i];
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (lower.find(k) != std::string::npos)
            return true;
    }
    return false;
}

// ── 3. Registry key check ─────────────────────────────────────────────────
struct RegEntry { HKEY hive; const wchar_t* path; };

bool CheckRegistryKeys()
{
    static const RegEntry keys[] = {
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\VMware, Inc.\\VMware Tools" },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox Guest Additions" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxGuest" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxMouse" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxService" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxSF" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxVideo" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmci" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmhgfs" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmmouse" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmrawdsk" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmusbmouse" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmvss" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vm3dmp" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmxnet" },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\vmx_svga" },
        { HKEY_LOCAL_MACHINE, L"HARDWARE\\ACPI\\DSDT\\VBOX__" },
        { HKEY_LOCAL_MACHINE, L"HARDWARE\\ACPI\\FADT\\VBOX__" },
        { HKEY_LOCAL_MACHINE, L"HARDWARE\\ACPI\\RSDT\\VBOX__" },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Virtual Machine\\Guest\\Parameters" },
        { nullptr, nullptr }
    };

    for (int i = 0; keys[i].path; ++i) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(keys[i].hive, keys[i].path, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
            RegCloseKey(hk);
            return true;
        }
    }
    return false;
}

// ── 4. Known VM process names ─────────────────────────────────────────────
// Snapshot is shared with the debugger process check (see below).
// Returns a snapshot handle the caller must close.
HANDLE TakeProcessSnapshot()
{
    return CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
}

bool MatchProcessList(HANDLE snap, const wchar_t* const* list, std::string& outName)
{
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(snap, &pe))
        return false;

    do {
        std::wstring lower = pe.szExeFile;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        for (int i = 0; list[i]; ++i) {
            if (lower == list[i]) {
                outName = WtoA(pe.szExeFile);
                return true;
            }
        }
    } while (Process32NextW(snap, &pe));

    return false;
}

bool CheckVMProcesses(HANDLE snap, std::string& outName)
{
    static const wchar_t* vmProcs[] = {
        L"vmtoolsd.exe",
        L"vmwaretray.exe",
        L"vmwareuser.exe",
        L"vboxservice.exe",
        L"vboxtray.exe",
        L"vboxclient.exe",
        L"xenservice.exe",
        L"qemu-ga.exe",
        L"prl_tools.exe",
        L"prl_cc.exe",
        L"vmacthlp.exe",
        L"vmnat.exe",
        L"vmnetdhcp.exe",
        nullptr
    };
    return MatchProcessList(snap, vmProcs, outName);
}

// ── 5. Known VM service names ─────────────────────────────────────────────
bool CheckVMServices()
{
    static const wchar_t* vmSvcs[] = {
        L"VBoxGuest", L"VBoxMouse", L"VBoxService", L"VBoxSF", L"VBoxVideo",
        L"vmci",      L"vmhgfs",    L"vmmouse",     L"VMTools",
        L"vmvss",     L"vmx_svga",  L"vmxnet",
        L"xenevtchn", L"xenfilt",   L"xennet",      L"xenpci", L"xensvc", L"xenvbd",
        L"prl_strg",
        nullptr
    };

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return false;

    bool found = false;
    for (int i = 0; vmSvcs[i] && !found; ++i) {
        SC_HANDLE svc = OpenServiceW(scm, vmSvcs[i], SERVICE_QUERY_STATUS);
        if (svc) { CloseServiceHandle(svc); found = true; }
    }

    CloseServiceHandle(scm);
    return found;
}

// ── 6. Suspicious MAC address OUI prefixes ───────────────────────────────
bool CheckMacAddress()
{
    static const unsigned char vmOUIs[][3] = {
        { 0x00, 0x05, 0x69 }, // VMware
        { 0x00, 0x0C, 0x29 }, // VMware
        { 0x00, 0x1C, 0x14 }, // VMware
        { 0x00, 0x50, 0x56 }, // VMware
        { 0x08, 0x00, 0x27 }, // VirtualBox
        { 0x00, 0x21, 0xF6 }, // VirtualBox (alt)
        { 0x00, 0x14, 0x4F }, // VirtualBox (alt)
        { 0x00, 0x03, 0xFF }, // Hyper-V / Virtual PC
        { 0x00, 0x15, 0x5D }, // Hyper-V
        { 0x00, 0x16, 0x3E }, // Xen / KVM (Red Hat)
        { 0x52, 0x54, 0x00 }, // QEMU/KVM (default)
        {}
    };

    ULONG bufLen = 0;
    GetAdaptersInfo(nullptr, &bufLen);
    if (bufLen == 0) return false;

    std::vector<BYTE> buf(bufLen);
    auto* pAdapt = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(pAdapt, &bufLen) != ERROR_SUCCESS)
        return false;

    // Count real vs VM adapters. Installing VMware/VirtualBox on bare metal
    // adds virtual host-only/NAT NICs with VM OUIs alongside the real NIC.
    // Only flag if EVERY adapter is a VM NIC (no real hardware NIC present).
    int total = 0, vmCount = 0;
    for (auto* a = pAdapt; a; a = a->Next) {
        if (a->AddressLength < 3) continue;
        ++total;
        for (int i = 0; vmOUIs[i][0] || vmOUIs[i][1] || vmOUIs[i][2]; ++i) {
            if (a->Address[0] == vmOUIs[i][0] &&
                a->Address[1] == vmOUIs[i][1] &&
                a->Address[2] == vmOUIs[i][2]) {
                ++vmCount;
                break;
            }
        }
    }
    return total > 0 && vmCount == total;
}

// ── 7. RDTSC timing ──────────────────────────────────────────────────────
// VM exits for CPUID inflate the delta.  Use 10 000 000 cycles as the
// threshold — at 3 GHz that is ~3.3 ms, absurd on real hardware but
// comfortably hit by most hypervisors.  The old 1 M threshold fired on
// bare-metal machines with frequency scaling / TSC ratio adjustments.
bool CheckRDTSCTiming()
{
    constexpr unsigned long long kThreshold = 10'000'000ULL;
    unsigned long long minDelta = UINT64_MAX;

    for (int n = 0; n < 5; ++n) {
        int dummy[4];
        unsigned long long t0 = __rdtsc();
        __cpuid(dummy, 0);
        unsigned long long t1 = __rdtsc();
        unsigned long long d  = t1 - t0;
        if (d < minDelta) minDelta = d;
    }
    return minDelta > kThreshold;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ANTI-DEBUG CHECKS
// ═══════════════════════════════════════════════════════════════════════════

// ── 8. IsDebuggerPresent (PEB.BeingDebugged) ─────────────────────────────
bool CheckIsDebuggerPresent()
{
    return IsDebuggerPresent() != FALSE;
}

// ── 9. CheckRemoteDebuggerPresent ────────────────────────────────────────
bool CheckRemoteDebugger()
{
    BOOL dbg = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &dbg);
    return dbg != FALSE;
}

// ── 10. NtQueryInformationProcess – ProcessDebugPort (0x07) ──────────────
// A non-zero debug port means a kernel-mode debugger is attached.
bool CheckDebugPort()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;

    auto NtQIP = reinterpret_cast<pfnNtQIP>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!NtQIP) return false;

    ULONG_PTR debugPort = 0;
    NTSTATUS  status    = NtQIP(GetCurrentProcess(),
                                0x07 /*ProcessDebugPort*/,
                                &debugPort,
                                sizeof(debugPort),
                                nullptr);

    return NT_SUCCESS(status) && (debugPort != 0);
}

// ── 11. Heap flags ───────────────────────────────────────────────────────
// Only check ForceFlags — it is 0 on a normal heap and non-zero only when
// the debug heap is active (e.g. gflags +hpa or ntsd attached).  Checking
// Flags is too noisy: Windows itself sets extra bits (LFH, segment heap,
// ETW tagging) on perfectly normal processes.
bool CheckHeapFlags()
{
#ifdef _WIN64
    ULONG_PTR peb        = __readgsqword(0x60);
    ULONG_PTR heapBase   = *reinterpret_cast<ULONG_PTR*>(peb + 0x30);
    DWORD     forceFlags = *reinterpret_cast<DWORD*>(heapBase + 0x74);
#else
    ULONG_PTR peb        = __readfsdword(0x30);
    ULONG_PTR heapBase   = *reinterpret_cast<ULONG_PTR*>(peb + 0x18);
    DWORD     forceFlags = *reinterpret_cast<DWORD*>(heapBase + 0x44);
#endif
    return forceFlags != 0;
}

// ── 12. Hardware breakpoints via GetThreadContext ─────────────────────────
// Dr0-Dr3 are the debug address registers. A non-zero value means a
// hardware breakpoint has been set by a debugger.
bool CheckHardwareBreakpoints()
{
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (!GetThreadContext(GetCurrentThread(), &ctx))
        return false;

    return ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3;
}

// ── 13. Known debugger / analysis-tool process names ─────────────────────
bool CheckDebuggerProcesses(HANDLE snap, std::string& outName)
{
    static const wchar_t* dbgProcs[] = {
        // Debuggers
        L"x64dbg.exe",
        L"x32dbg.exe",
        L"ollydbg.exe",
        L"windbg.exe",
        L"windbg64.exe",
        L"dbgview.exe",      // Sysinternals DebugView
        L"idaq.exe",         // IDA Pro 32-bit
        L"idaq64.exe",       // IDA Pro 64-bit
        L"idaw.exe",
        L"idaw64.exe",
        L"ida.exe",
        L"ida64.exe",
        L"radare2.exe",
        L"r2.exe",
        L"cutter.exe",       // Cutter (radare2 GUI)
        L"binaryninja.exe",  // Binary Ninja
        L"ghidra.exe",
        L"ghidrarun.exe",

        // Monitoring / analysis
        L"procmon.exe",      // Process Monitor
        L"procmon64.exe",
        L"procexp.exe",      // Process Explorer
        L"procexp64.exe",
        L"processhacker.exe",
        L"systeminformer.exe",
        L"wireshark.exe",
        L"fiddler.exe",
        L"fiddler4.exe",
        L"charles.exe",
        L"httpdebugger.exe",
        L"tcpview.exe",
        L"apimonitor.exe",
        L"apimonitor-x64.exe",
        L"apimonitor-x86.exe",
        L"regmon.exe",
        L"filemon.exe",

        // Memory / RE tools
        L"cheatengine.exe",
        L"cheatengine-x86_64.exe",
        L"cheatengine-i386.exe",
        L"scylla_x64.exe",
        L"scylla_x86.exe",
        L"importrec.exe",
        L"imprec.exe",
        L"lordpe.exe",
        L"reshacker.exe",
        L"hxd.exe",
        L"hexeditor.exe",

        // Sandboxes / auto-analysis
        L"vmsrvc.exe",
        L"vmusrvc.exe",
        L"peid.exe",
        L"exeinfope.exe",
        L"die.exe",          // Detect-It-Easy
        L"pestudio.exe",

        nullptr
    };

    return MatchProcessList(snap, dbgProcs, outName);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace VMDetect {

std::string DetectionResult::Summary() const {
    if (triggers.empty()) return "none";
    std::string out;
    for (size_t i = 0; i < triggers.size(); ++i) {
        if (i) out += "; ";
        out += triggers[i];
    }
    return out;
}

DetectionResult Check()
{
    DetectionResult result{};
    result.isVM       = false;
    result.isDebugger = false;
    result.isVMEnv    = false;

    // ── Anti-debug checks ─────────────────────────────────────────────────

    // 8. IsDebuggerPresent
    if (CheckIsDebuggerPresent()) {
        result.isDebugger = true;
        result.triggers.push_back("Debugger attached (IsDebuggerPresent)");
    }

    // 9. Remote debugger
    if (CheckRemoteDebugger()) {
        result.isDebugger = true;
        result.triggers.push_back("Remote debugger detected (CheckRemoteDebuggerPresent)");
    }

    // 10. Debug port via NtQueryInformationProcess
    if (CheckDebugPort()) {
        result.isDebugger = true;
        result.triggers.push_back("Kernel debug port active (NtQueryInformationProcess ProcessDebugPort)");
    }

    // 11. Heap flags (debug heap)
    if (CheckHeapFlags()) {
        result.isDebugger = true;
        result.triggers.push_back("Debug heap flags set (PEB HeapForceFlags != 0)");
    }

    // 12. Hardware breakpoints (Dr0-Dr3)
    if (CheckHardwareBreakpoints()) {
        result.isDebugger = true;
        result.triggers.push_back("Hardware breakpoint(s) set (GetThreadContext Dr0-Dr3)");
    }

    // ── Process snapshot (shared by checks 4 and 13) ──────────────────────
    HANDLE snap = TakeProcessSnapshot();

    // 13. Known debugger / analysis-tool processes
    std::string toolName;
    if (CheckDebuggerProcesses(snap, toolName)) {
        result.isDebugger = true;
        result.triggers.push_back("Analysis tool running: " + toolName);
    }

    // ── VM-environment checks ─────────────────────────────────────────────

    // 1. CPUID hypervisor bit (disabled — see stub comment)
    if (CheckCPUIDHypervisorBit()) {
        result.isVMEnv = true;
        result.triggers.push_back("CPUID hypervisor bit set (ECX bit 31)");
    }

    // 2. CPUID hypervisor vendor string
    std::string vendor;
    if (CheckHypervisorVendorString(vendor)) {
        result.isVMEnv = true;
        result.triggers.push_back("Hypervisor vendor string: " + vendor);
    }

    // 3. VM registry keys
    if (CheckRegistryKeys()) {
        result.isVMEnv = true;
        result.triggers.push_back("VM registry key present");
    }

    // 4. VM process names (reuse snapshot)
    std::string vmProcName;
    if (CheckVMProcesses(snap, vmProcName)) {
        result.isVMEnv = true;
        result.triggers.push_back("VM process running: " + vmProcName);
    }

    CloseHandle(snap);

    // 5. VM services
    if (CheckVMServices()) {
        result.isVMEnv = true;
        result.triggers.push_back("VM service present");
    }

    // 6. VM MAC address OUI
    if (CheckMacAddress()) {
        result.isVMEnv = true;
        result.triggers.push_back("All NICs have VM MAC address OUI");
    }

    // 7. RDTSC timing anomaly
    if (CheckRDTSCTiming()) {
        result.isVMEnv = true;
        result.triggers.push_back("RDTSC timing anomaly (VM exit overhead)");
    }

    result.isVM = result.isDebugger || result.isVMEnv;
    return result;
}

} // namespace VMDetect
