#pragma once
#include <string>

namespace Hwid {
    // Returns a 64-char HMAC-SHA256 hex fingerprint. Empty string on failure.
    std::string GetHWID();

    struct HardwareDetails {
        std::string cpu;   // CPU brand string e.g. "Intel(R) Core(TM) i7-12700K"
        std::string gpu;   // Primary GPU description e.g. "NVIDIA GeForce RTX 3080"
        int         ramGb; // Total physical RAM rounded to nearest GB
    };

    // Collects human-readable hardware info for display in HWID reset requests.
    HardwareDetails GetHardwareDetails();
}
