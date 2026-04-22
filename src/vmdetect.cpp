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
#include "security/obf.h"

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

    static const std::vector<std::string> known = {
        OBF_S("KVMKVMKVM"),
        OBF_S("VMwareVMware"),
        OBF_S("VBoxVBoxVBox"),
        OBF_S("XenVMMXenVMM"),
        OBF_S("prl hyperv"),
        OBF_S("TCGTCGTCGTCG"),
    };

    std::string lower = outVendor;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& k : known) {
        std::string ks = k;
        std::transform(ks.begin(), ks.end(), ks.begin(), ::tolower);
        if (lower.find(ks) != std::string::npos)
            return true;
    }
    return false;
}

// ── 3. Registry key check ─────────────────────────────────────────────────
struct RegEntry { HKEY hive; std::wstring path; };

bool CheckRegistryKeys()
{
    static const std::vector<RegEntry> keys = {
        { HKEY_LOCAL_MACHINE, OBF_W(L"SOFTWARE\\VMware, Inc.\\VMware Tools") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SOFTWARE\\Oracle\\VirtualBox Guest Additions") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\VBoxGuest") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\VBoxMouse") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\VBoxService") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\VBoxSF") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\VBoxVideo") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmci") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmhgfs") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmmouse") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmrawdsk") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmusbmouse") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmvss") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vm3dmp") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmxnet") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SYSTEM\\CurrentControlSet\\Services\\vmx_svga") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"HARDWARE\\ACPI\\DSDT\\VBOX__") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"HARDWARE\\ACPI\\FADT\\VBOX__") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"HARDWARE\\ACPI\\RSDT\\VBOX__") },
        { HKEY_LOCAL_MACHINE, OBF_W(L"SOFTWARE\\Microsoft\\Virtual Machine\\Guest\\Parameters") },
    };

    for (const auto& e : keys) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(e.hive, e.path.c_str(), 0, KEY_READ, &hk) == ERROR_SUCCESS) {
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

bool MatchProcessList(HANDLE snap, const std::vector<std::wstring>& list, std::string& outName)
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

        for (const auto& candidate : list) {
            if (lower == candidate) {
                outName = WtoA(pe.szExeFile);
                return true;
            }
        }
    } while (Process32NextW(snap, &pe));

    return false;
}

bool CheckVMProcesses(HANDLE snap, std::string& outName)
{
    static const std::vector<std::wstring> vmProcs = {
        OBF_W(L"vmtoolsd.exe"),
        OBF_W(L"vmwaretray.exe"),
        OBF_W(L"vmwareuser.exe"),
        OBF_W(L"vboxservice.exe"),
        OBF_W(L"vboxtray.exe"),
        OBF_W(L"vboxclient.exe"),
        OBF_W(L"xenservice.exe"),
        OBF_W(L"qemu-ga.exe"),
        OBF_W(L"prl_tools.exe"),
        OBF_W(L"prl_cc.exe"),
        OBF_W(L"vmacthlp.exe"),
        OBF_W(L"vmnat.exe"),
        OBF_W(L"vmnetdhcp.exe"),
    };
    return MatchProcessList(snap, vmProcs, outName);
}

// ── 5. Known VM service names ─────────────────────────────────────────────
bool CheckVMServices()
{
    static const std::vector<std::wstring> vmSvcs = {
        OBF_W(L"VBoxGuest"), OBF_W(L"VBoxMouse"), OBF_W(L"VBoxService"),
        OBF_W(L"VBoxSF"),    OBF_W(L"VBoxVideo"),
        OBF_W(L"vmci"),      OBF_W(L"vmhgfs"),    OBF_W(L"vmmouse"),
        OBF_W(L"VMTools"),
        OBF_W(L"vmvss"),     OBF_W(L"vmx_svga"),  OBF_W(L"vmxnet"),
        OBF_W(L"xenevtchn"), OBF_W(L"xenfilt"),   OBF_W(L"xennet"),
        OBF_W(L"xenpci"),    OBF_W(L"xensvc"),    OBF_W(L"xenvbd"),
        OBF_W(L"prl_strg"),
    };

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return false;

    bool found = false;
    for (const auto& name : vmSvcs) {
        if (found) break;
        SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_STATUS);
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
    HMODULE ntdll = GetModuleHandleW(OBF_C(L"ntdll.dll"));
    if (!ntdll) return false;

    auto NtQIP = reinterpret_cast<pfnNtQIP>(
        GetProcAddress(ntdll, OBF_A("NtQueryInformationProcess")));
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
    static const std::vector<std::wstring> dbgProcs = {
        // Debuggers
        OBF_W(L"x64dbg.exe"),
        OBF_W(L"x32dbg.exe"),
        OBF_W(L"ollydbg.exe"),
        OBF_W(L"windbg.exe"),
        OBF_W(L"windbg64.exe"),
        OBF_W(L"dbgview.exe"),
        OBF_W(L"idaq.exe"),
        OBF_W(L"idaq64.exe"),
        OBF_W(L"idaw.exe"),
        OBF_W(L"idaw64.exe"),
        OBF_W(L"ida.exe"),
        OBF_W(L"ida64.exe"),
        OBF_W(L"radare2.exe"),
        OBF_W(L"r2.exe"),
        OBF_W(L"cutter.exe"),
        OBF_W(L"binaryninja.exe"),
        OBF_W(L"ghidra.exe"),
        OBF_W(L"ghidrarun.exe"),

        // Monitoring / analysis
        OBF_W(L"procmon.exe"),
        OBF_W(L"procmon64.exe"),
        OBF_W(L"procexp.exe"),
        OBF_W(L"procexp64.exe"),
        OBF_W(L"processhacker.exe"),
        OBF_W(L"systeminformer.exe"),
        OBF_W(L"wireshark.exe"),
        OBF_W(L"fiddler.exe"),
        OBF_W(L"fiddler4.exe"),
        OBF_W(L"charles.exe"),
        OBF_W(L"httpdebugger.exe"),
        OBF_W(L"tcpview.exe"),
        OBF_W(L"apimonitor.exe"),
        OBF_W(L"apimonitor-x64.exe"),
        OBF_W(L"apimonitor-x86.exe"),
        OBF_W(L"regmon.exe"),
        OBF_W(L"filemon.exe"),

        // Memory / RE tools
        OBF_W(L"cheatengine.exe"),
        OBF_W(L"cheatengine-x86_64.exe"),
        OBF_W(L"cheatengine-i386.exe"),
        OBF_W(L"scylla_x64.exe"),
        OBF_W(L"scylla_x86.exe"),
        OBF_W(L"importrec.exe"),
        OBF_W(L"imprec.exe"),
        OBF_W(L"lordpe.exe"),
        OBF_W(L"reshacker.exe"),
        OBF_W(L"hxd.exe"),
        OBF_W(L"hexeditor.exe"),

        // Sandboxes / auto-analysis
        OBF_W(L"vmsrvc.exe"),
        OBF_W(L"vmusrvc.exe"),
        OBF_W(L"peid.exe"),
        OBF_W(L"exeinfope.exe"),
        OBF_W(L"die.exe"),
        OBF_W(L"pestudio.exe"),
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

    if (CheckIsDebuggerPresent()) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Debugger attached (IsDebuggerPresent)"));
    }

    if (CheckRemoteDebugger()) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Remote debugger detected (CheckRemoteDebuggerPresent)"));
    }

    if (CheckDebugPort()) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Kernel debug port active (NtQueryInformationProcess ProcessDebugPort)"));
    }

    if (CheckHeapFlags()) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Debug heap flags set (PEB HeapForceFlags != 0)"));
    }

    if (CheckHardwareBreakpoints()) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Hardware breakpoint(s) set (GetThreadContext Dr0-Dr3)"));
    }

    HANDLE snap = TakeProcessSnapshot();

    std::string toolName;
    if (CheckDebuggerProcesses(snap, toolName)) {
        result.isDebugger = true;
        result.triggers.push_back(OBF_S("Analysis tool running: ") + toolName);
    }

    if (CheckCPUIDHypervisorBit()) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("CPUID hypervisor bit set (ECX bit 31)"));
    }

    std::string vendor;
    if (CheckHypervisorVendorString(vendor)) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("Hypervisor vendor string: ") + vendor);
    }

    if (CheckRegistryKeys()) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("VM registry key present"));
    }

    std::string vmProcName;
    if (CheckVMProcesses(snap, vmProcName)) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("VM process running: ") + vmProcName);
    }

    CloseHandle(snap);

    if (CheckVMServices()) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("VM service present"));
    }

    if (CheckMacAddress()) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("All NICs have VM MAC address OUI"));
    }

    if (CheckRDTSCTiming()) {
        result.isVMEnv = true;
        result.triggers.push_back(OBF_S("RDTSC timing anomaly (VM exit overhead)"));
    }

    result.isVM = result.isDebugger || result.isVMEnv;
    return result;
}

} // namespace VMDetect
