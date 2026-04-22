#pragma once
#include <string>
#include <vector>
#include <windows.h>
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
        std::string avatarUrl;
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

    struct HeartbeatResponse {
        bool success;       // true == 2xx with {ok:true}; false otherwise
        DWORD statusCode;   // HTTP status (0 on network error)
        std::string reason; // server-supplied reason e.g. "expired", "revoked"
    };

    // Subscription heartbeat. The injected runtime calls this on a fixed
    // cadence; two consecutive failures on its side trigger a hard-kill so
    // that an expired subscription cannot be tunneled past via local DNS
    // tampering. The loader does not heartbeat itself — this helper exists
    // so both modules share a single network surface during the auth port.
    HeartbeatResponse Heartbeat(const std::string& token,
                                const std::string& hwid,
                                const std::string& productId);

    // Report VM / debugger detection to the backend so it shows in the admin logs.
    // vmDetected      — true if any VM-environment check triggered
    // debuggerDetected — true if any anti-debug check triggered
    // hwid            — machine hardware fingerprint (may be empty if not yet generated)
    // triggers        — list of specific detection vector labels (e.g. "VM process running: vmtoolsd.exe")
    void ReportDetection(bool vmDetected, bool debuggerDetected,
                         const std::string& hwid,
                         const std::vector<std::string>& triggers);

    // Product-launch lifecycle event reporter. Lets the admin panel show
    // attempts / successes / failures / hwid mismatches even when the loader
    // can't reach the stream endpoint (e.g., local network error).
    //
    // eventType — one of "load_attempt", "load_success", "load_failed",
    //             "hwid_mismatch"
    // productId — product being launched (e.g., "bo6")
    // hwid      — machine hardware fingerprint (may be empty)
    // details   — short human-readable explanation (may be empty)
    // token     — session bearer token (may be empty for pre-login cases)
    void ReportLoaderEvent(const std::string& eventType,
                           const std::string& productId,
                           const std::string& hwid,
                           const std::string& details,
                           const std::string& token);
}
