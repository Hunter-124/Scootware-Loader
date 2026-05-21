#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include "../hwid.h"
#include "../security/driver_bringup.h"

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

    // Returns true if the last StreamAsset call failed with a HWID mismatch (HTTP 403).
    bool LastStreamWasHwidMismatch();
    // Returns the HTTP status code from the last StreamAsset call (0 if network error or not yet called).
    DWORD LastStreamStatusCode();
    // Returns the response body from the last failed StreamAsset call (useful for server error messages).
    const std::string& LastStreamErrorBody();

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

    // Fetch per-product mapper configuration from the server.
    // Returns server values on success; returns a default-initialised
    // MapperConfig on any network error, non-2xx status, or empty body so
    // the loader always has safe fallback values.
    DriverBringup::MapperConfig GetMapperConfig(const std::string& productId,
                                                const std::string& token);

    // Per-product configuration for the kernel-injection delivery path
    // (CMD_INJECT_DLL via the loader's in-image IPC).
    //
    // targetProcessName — image name of the host the streamed DLL should be
    //   injected into (e.g. "cs2.exe"). The loader resolves this to a PID
    //   via ProcessWait::PidFor right before issuing CMD_INJECT_DLL.
    //
    // allocMode — INJ_ALLOC_* constant; passed verbatim to the driver.
    //   Defaults to INJ_ALLOC_BETWEEN_LEGIT_MODULES (1) when the server
    //   omits the field.
    struct InjectConfig {
        std::string targetProcessName;
        uint32_t    allocMode = 1; // INJ_ALLOC_BETWEEN_LEGIT_MODULES
    };

    // Fetch per-product inject configuration from the server. Falls back
    // to a default-initialised InjectConfig on any non-2xx / empty body
    // (e.g. when the admin-panel field has not been populated yet) so the
    // loader still works end-to-end while the server side is being built
    // out — the caller is expected to refuse the injection when
    // targetProcessName comes back empty.
    InjectConfig GetInjectConfig(const std::string& productId,
                                 const std::string& token);

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

    // Submits an HWID reset request automatically
    HwidResetResponse SubmitHwidResetRequest(const std::string& token,
                                             const std::string& newHwid,
                                             const Hwid::HardwareDetails& newDetails);
}
