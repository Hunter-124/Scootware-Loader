#pragma once
#include <string>
#include <vector>
#include "../hwid.h"

namespace Api {
    struct ProductAccess {
        std::string productId;
        std::string expiresAt;
        std::string tier;
        bool hasAccess;
    };

    struct AuthResponse {
        bool success;
        std::string token;
        std::string message;
        std::vector<ProductAccess> subscriptions;
    };

    struct HwidResetResponse {
        bool success;
        std::string message;
    };

    // Authenticate and return user subscriptions.
    AuthResponse Login(const std::string& username, const std::string& password);

    // Stream a specific asset from the backend into a decrypted byte buffer.
    // hwid must be the value from Hwid::GetHWID() — sent as X-HWID header and
    // used to derive the AES-256-GCM decryption key for the encrypted response.
    // Returns pair<PlaintextBuffer, ExpectedAllocationSize>
    std::pair<std::vector<uint8_t>, size_t> StreamAsset(const std::string& productId, const std::string& assetType, const std::string& token, const std::string& hwid);

    // Submit a HWID reset request after a 403 HWID mismatch on StreamAsset.
    // newHwid    — the HWID of this machine (the one that was rejected)
    // newDetails — human-readable hardware of this machine
    // token      — the session token from login
    HwidResetResponse SubmitHwidResetRequest(const std::string& token, const std::string& newHwid, const Hwid::HardwareDetails& newDetails);

    // Returns true if the last StreamAsset call failed with a HWID mismatch (HTTP 403).
    bool LastStreamWasHwidMismatch();

    // Report VM / debugger detection to the backend so it shows in the admin logs.
    // vmDetected      — true if any VM-environment check triggered
    // debuggerDetected — true if any anti-debug check triggered
    // hwid            — machine hardware fingerprint (may be empty if not yet generated)
    // triggers        — list of specific detection vector labels (e.g. "VM process running: vmtoolsd.exe")
    void ReportDetection(bool vmDetected, bool debuggerDetected,
                         const std::string& hwid,
                         const std::vector<std::string>& triggers);
}
