#pragma once
#include <vector>
#include <cstdint>

#include <string>

namespace RunPE {
    // Process hollowing: maps PE from memoryBuffer into a suspended child process
    bool Execute(const std::vector<uint8_t>& memoryBuffer, size_t allocationSize, const std::string& customHostPath = "");
}
