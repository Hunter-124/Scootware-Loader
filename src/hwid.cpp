#include "hwid.h"
#include "security/obf.h"
#include <windows.h>
#include <iphlpapi.h>
#include <intrin.h>
#include <bcrypt.h>
#include <dxgi.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "dxgi.lib")

namespace Hwid {

    // XOR-obfuscated HMAC-SHA256 key for HWID fingerprinting.
    // Stored as (plaintext XOR mask) so the key does not appear as a
    // readable string in the binary. Plaintext: "Sc00tw4r3-HW1D-S3cr3t-K3y-32byte"
    // This key is CLIENT-ONLY: the server never needs to know it.
    // To rotate: pick a new 32-byte secret, XOR against a new mask, update both arrays.
    static const uint8_t g_hwidXored[32] = {
        0x8D,0xCE,0x8E,0xDF,0x66,0x43,0x62,0x0A,
        0xA9,0x91,0x96,0xA7,0x23,0x70,0x7B,0x2B,
        0xA9,0xDF,0xAC,0xC3,0x66,0x19,0x1D,0x4B,
        0xE3,0x91,0xED,0xC2,0x70,0x4D,0x22,0x1D
    };
    static const uint8_t g_hwidMask[32] = {
        0xDE,0xAD,0xBE,0xEF,0x12,0x34,0x56,0x78,
        0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78,
        0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78,
        0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78
    };

    // Decode the XOR-obfuscated HMAC key into a temporary buffer.
    // Kept volatile to discourage compiler from optimising this into a constant.
    static void DecodeHwidKey(uint8_t out[32]) {
        for (volatile int i = 0; i < 32; ++i)
            out[i] = g_hwidXored[i] ^ g_hwidMask[i];
    }

    // Retrieve CPU vendor + processor signature via CPUID.
    static std::string GetCpuId() {
        int regs[4] = {};
        __cpuid(regs, 0);
        char vendor[13] = {};
        memcpy(vendor + 0, &regs[1], 4);
        memcpy(vendor + 4, &regs[3], 4);
        memcpy(vendor + 8, &regs[2], 4);
        vendor[12] = '\0';
        __cpuid(regs, 1);
        char sig[16] = {};
        snprintf(sig, sizeof(sig), "%08X", (unsigned int)regs[0]);
        return std::string(vendor) + std::string(sig);
    }

    // Volume serial of the system drive.
    static std::string GetDiskSerial() {
        DWORD serial = 0;
        if (!GetVolumeInformationA(OBF_A("C:\\"), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
            return OBF_S("00000000");
        char buf[16] = {};
        snprintf(buf, sizeof(buf), "%08X", serial);
        return std::string(buf);
    }

    // MAC address of the first non-loopback adapter.
    static std::string GetMacAddress() {
        ULONG bufLen = sizeof(IP_ADAPTER_INFO);
        std::vector<BYTE> buf(bufLen);
        if (GetAdaptersInfo(reinterpret_cast<PIP_ADAPTER_INFO>(buf.data()), &bufLen) == ERROR_BUFFER_OVERFLOW)
            buf.resize(bufLen);
        PIP_ADAPTER_INFO adapter = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        if (GetAdaptersInfo(adapter, &bufLen) != NO_ERROR)
            return "000000000000";
        while (adapter) {
            if (adapter->AddressLength == 6) {
                char mac[18] = {};
                snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
                    adapter->Address[0], adapter->Address[1],
                    adapter->Address[2], adapter->Address[3],
                    adapter->Address[4], adapter->Address[5]);
                return std::string(mac);
            }
            adapter = adapter->Next;
        }
        return "000000000000";
    }

    // HMAC-SHA256 via Windows CNG with a runtime-decoded key.
    static std::string HmacSha256Hex(const uint8_t* key, size_t keyLen, const std::string& data) {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD cbHash = 0, cbHashObj = 0, cbData = 0;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
            return "";

        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbHashObj, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return "";
        }
        if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return "";
        }

        std::vector<BYTE> hashObj(cbHashObj), hash(cbHash);

        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), cbHashObj,
            (PUCHAR)key, (ULONG)keyLen, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return "";
        }
        if (BCryptHashData(hHash, (PUCHAR)data.c_str(), (ULONG)data.size(), 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return "";
        }
        if (BCryptFinishHash(hHash, hash.data(), cbHash, 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return "";
        }

        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        std::ostringstream oss;
        for (BYTE b : hash)
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        return oss.str();
    }

    std::string GetHWID() {
        // Combine hardware identifiers into a single string.
        std::string raw = GetCpuId() + "|" + GetDiskSerial() + "|" + GetMacAddress();

        // Decode the obfuscated HMAC key at runtime.
        uint8_t key[32];
        DecodeHwidKey(key);

        // HMAC-SHA256(key, raw_hardware) -> 64-char hex fingerprint.
        std::string result = HmacSha256Hex(key, 32, raw);

        // Zero key material before returning.
        SecureZeroMemory(key, sizeof(key));
        return result;
    }

    // CPU brand string via extended CPUID (leaves 0x80000002-4).
    static std::string GetCpuBrand() {
        char brand[49] = {};
        int regs[4] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, 0x80000002 + i);
            memcpy(brand + i * 16 + 0,  &regs[0], 4);
            memcpy(brand + i * 16 + 4,  &regs[1], 4);
            memcpy(brand + i * 16 + 8,  &regs[2], 4);
            memcpy(brand + i * 16 + 12, &regs[3], 4);
        }
        brand[48] = '\0';
        // Trim leading spaces
        const char* p = brand;
        while (*p == ' ') ++p;
        return std::string(p);
    }

    // Primary GPU description via DXGI adapter enumeration.
    static std::string GetGpuName() {
        IDXGIFactory* factory = nullptr;
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
            return OBF_S("Unknown GPU");

        IDXGIAdapter* adapter = nullptr;
        std::string name = OBF_S("Unknown GPU");
        if (SUCCEEDED(factory->EnumAdapters(0, &adapter))) {
            DXGI_ADAPTER_DESC desc = {};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                // Convert wide string to narrow
                char narrow[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, narrow, sizeof(narrow), nullptr, nullptr);
                name = std::string(narrow);
            }
            adapter->Release();
        }
        factory->Release();
        return name;
    }

    // Total physical RAM in GB.
    static int GetRamGb() {
        MEMORYSTATUSEX ms = {};
        ms.dwLength = sizeof(ms);
        if (!GlobalMemoryStatusEx(&ms)) return 0;
        // Round to nearest GB
        return static_cast<int>((ms.ullTotalPhys + (1ULL << 29)) >> 30);
    }

    HardwareDetails GetHardwareDetails() {
        HardwareDetails d;
        d.cpu   = GetCpuBrand();
        d.gpu   = GetGpuName();
        d.ramGb = GetRamGb();
        return d;
    }
}
