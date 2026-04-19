#pragma once
#include <string>

namespace VMDetect {

    struct DetectionResult {
        bool isVM;                  // true if any indicator triggered
        std::string reason;         // human-readable reason (first trigger wins)
    };

    // Run all checks and return the combined result.
    DetectionResult Check();

} // namespace VMDetect
