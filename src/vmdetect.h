#pragma once
#include <string>
#include <vector>

namespace VMDetect {

    struct DetectionResult {
        bool isVM;                          // true if any indicator triggered
        std::vector<std::string> triggers;  // every detection vector that fired
        bool isDebugger;                    // true if any anti-debug check fired
        bool isVMEnv;                       // true if any VM-environment check fired

        // Convenience: comma-separated list of all triggers for logging
        std::string Summary() const;
    };

    // Run ALL checks (does not short-circuit) and return the combined result.
    DetectionResult Check();

} // namespace VMDetect
