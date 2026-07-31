#include "api.h"
#include "../security/obf.h"
#include "../security/driver_bringup.h"
#include "../util/diaglog.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace Api {

    // Set when StreamAsset receives HTTP 403 (HWID mismatch).
    static bool s_lastStreamHwidMismatch = false;
    // Last HTTP status code from StreamAsset (0 if network error).
    static DWORD s_lastStreamStatusCode = 0;
    // Last response body from StreamAsset error (for server-side error messages).
    static std::string s_lastStreamErrorBody;

    bool LastStreamWasHwidMismatch() { return s_lastStreamHwidMismatch; }
    DWORD LastStreamStatusCode() { return s_lastStreamStatusCode; }
    const std::string& LastStreamErrorBody() { return s_lastStreamErrorBody; }

    // Simple helper to perform a WinHttp request
    struct HttpResponse {
        bool success;
        std::string body;
        size_t allocationSizeHeader;
        DWORD statusCode;
    };

    struct ApiSettings {
        std::wstring host;
        INTERNET_PORT port;
        bool secure;
    };

    ApiSettings GetSettings() {
        ApiSettings s = { OBF_W(L"scootware.us"), 443, true };

        if (GetFileAttributesA(OBF_A("dev_mode.txt")) != INVALID_FILE_ATTRIBUTES) {
            s.host = OBF_W(L"localhost");
            s.port = 3000;
            s.secure = false;
        }

        return s;
    }

    std::string JsonEscape(const std::string& input) {
        std::string output;
        for (char c : input) {
            if (c == '\"') output += "\\\"";
            else if (c == '\\') output += "\\\\";
            else if (c == '\n') output += "\\n";
            else if (c == '\r') output += "\\r";
            else if (c == '\t') output += "\\t";
            else output += c;
        }
        return output;
    }

// UTF-8 → UTF-16 conversion.  The range-based wstring constructor
// zero-extends each char, which corrupts any non-ASCII character in
// paths, tokens, or HWIDs that happen to contain bytes > 0x7F.
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out((size_t)need, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

    HttpResponse PerformRequest(const std::string& method, const std::string& path, const std::string& postData = "", const std::string& token = "", const std::string& hwid = "") {
        HttpResponse response = { false, "", 0, 0 };
        ApiSettings settings = GetSettings();
        
        HINTERNET hSession = WinHttpOpen(OBF_C(L"ScootwareLoader/1.0"), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            LDIAG() << "[api] PerformRequest: WinHttpOpen failed";
            return response;
        }

        HINTERNET hConnect = WinHttpConnect(hSession, settings.host.c_str(), settings.port, 0);
        if (!hConnect) {
            LDIAG() << "[api] PerformRequest: WinHttpConnect failed port=" << settings.port;
            WinHttpCloseHandle(hSession);
            return response;
        }

        std::wstring wPath   = Utf8ToWide(path);
        std::wstring wMethod = Utf8ToWide(method);

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (settings.secure ? WINHTTP_FLAG_SECURE : 0));
        
        if (hRequest) {
            std::wstring headers = OBF_W(L"Content-Type: application/json\r\n");
            if (!token.empty()) {
                std::wstring wToken = Utf8ToWide(token);
                headers += OBF_W(L"Authorization: Bearer ") + wToken + OBF_W(L"\r\n");
            }
            if (!hwid.empty()) {
                std::wstring wHwid = Utf8ToWide(hwid);
                headers += OBF_W(L"X-HWID: ") + wHwid + OBF_W(L"\r\n");
            }

            BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), -1, (LPVOID)postData.c_str(), (DWORD)postData.length(), (DWORD)postData.length(), 0);
            
            if (!bResults) {
                DWORD err = ::GetLastError();
                LDIAG() << "[api] PerformRequest: WinHttpSendRequest failed err=" << err << " path=" << path;
            }

            if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

            if (!bResults && response.statusCode == 0) {
                DWORD err = ::GetLastError();
                LDIAG() << "[api] PerformRequest: WinHttpReceiveResponse failed err=" << err << " path=" << path;
            }

            if (bResults) {
                DWORD dwStatusCode = 0;
                DWORD dwSize = sizeof(dwStatusCode);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
                response.statusCode = dwStatusCode;
                
                LDIAG() << "[api] " << method << " " << path << " -> " << dwStatusCode;
                std::cout << OBF_S("[API] Request to ") << path << OBF_S(" -> Status: ") << dwStatusCode << "\n";

                dwSize = 0;
                DWORD dwDownloaded = 0;
                
                // Extract X-Allocation-Size header if present
                wchar_t headerBuffer[256];
                DWORD headerSize = sizeof(headerBuffer);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, OBF_C(L"X-Allocation-Size"), headerBuffer, &headerSize, WINHTTP_NO_HEADER_INDEX)) {
                    response.allocationSizeHeader = _wtoi64(headerBuffer);
                }

                do {
                    dwSize = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    if (dwSize == 0) break;

                    char* pszOutBuffer = new char[dwSize + 1];
                    if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                        pszOutBuffer[dwDownloaded] = '\0';
                        response.body.append(pszOutBuffer, dwDownloaded);
                    }
                    delete[] pszOutBuffer;
                } while (dwSize > 0);
                
                // If we get a 200 series response, we consider it success even if parsing issues occur later
                if (dwStatusCode >= 200 && dwStatusCode < 300) response.success = true;
            }
            WinHttpCloseHandle(hRequest);
        }

        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    // Crude JSON parser for the loader's specific needs
    std::string GetJsonValue(const std::string& json, const std::string& key) {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";
        
        size_t nextComma = json.find(",", pos);
        size_t nextBrace = json.find("}", pos);
        size_t endBoundary = json.length();
        if (nextComma != std::string::npos) endBoundary = (std::min)(endBoundary, nextComma);
        if (nextBrace != std::string::npos) endBoundary = (std::min)(endBoundary, nextBrace);

        size_t start = json.find("\"", pos);
        if (start == std::string::npos || start > endBoundary) {
            // Might be a boolean or number
            size_t valStart = json.find_first_not_of(": \t\r\n", pos + 1);
            size_t valEnd = json.find_first_of(",} \t\r\n", valStart);
            if (valStart == std::string::npos) return "";
            if (valEnd == std::string::npos) valEnd = json.length();
            return json.substr(valStart, valEnd - valStart);
        }
        
        size_t end = json.find("\"", start + 1);
        if (end == std::string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    }

    // Helper to extract an array string for a specific key e.g. "activeProducts": [...]
    std::string GetJsonArray(const std::string& json, const std::string& key) {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";

        size_t start = json.find("[", pos);
        if (start == std::string::npos) return "";

        int bracketCount = 1;
        size_t end = start + 1;
        while (bracketCount > 0 && end < json.length()) {
            if (json[end] == '[') bracketCount++;
            else if (json[end] == ']') bracketCount--;
            end++;
        }

        if (bracketCount == 0) {
            return json.substr(start, end - start);
        }
        return "";
    }

    AuthResponse Login(const std::string& username, const std::string& password) {
        AuthResponse res;
        res.success = false;

        std::string escapedUser = JsonEscape(username);
        std::string escapedPass = JsonEscape(password);
        std::string jsonPayload = OBF_S("{\"identifier\":\"") + escapedUser
                                + OBF_S("\",\"password\":\"") + escapedPass
                                + OBF_S("\"}");

        std::cout << OBF_S("[API] Attempting login for: ") << username << "...\n";

        HttpResponse resp = PerformRequest(OBF_S("POST"), OBF_S("/api/auth/login"), jsonPayload);

        if (resp.success) {
            std::string userObj = GetJsonValue(resp.body, OBF_S("user"));
            if (!userObj.empty()) {
                res.success = true;
                // The forum API uses Passport sessions and returns the
                // session id at the top level as `sessionId`. The Express
                // middleware in api-server/src/app.ts accepts that value
                // as the `Authorization: Bearer <sid>` token for any
                // subsequent loader / heartbeat call, so we treat it as
                // our bearer token. Older server builds returned `token`
                // instead, so fall back to that for backwards compat.
                res.token = GetJsonValue(resp.body, OBF_S("sessionId"));
                if (res.token.empty()) {
                    res.token = GetJsonValue(resp.body, OBF_S("token"));
                }
                res.message = OBF_S("Login successful!");
                res.avatarUrl = GetJsonValue(resp.body, OBF_S("avatarUrl"));

                std::string productsArray = GetJsonArray(resp.body, OBF_S("activeProducts"));
                if (!productsArray.empty()) {
                    size_t pos = 0;
                    while (true) {
                        size_t objStart = productsArray.find("{", pos);
                        if (objStart == std::string::npos) break;
                        size_t objEnd = productsArray.find("}", objStart);
                        if (objEnd == std::string::npos) break;

                        std::string obj = productsArray.substr(objStart, objEnd - objStart + 1);

                        ProductAccess prod;
                        prod.productId = GetJsonValue(obj, OBF_S("productId"));
                        prod.expiresAt = GetJsonValue(obj, OBF_S("expiresAt"));
                        prod.tier      = GetJsonValue(obj, OBF_S("tier"));
                        prod.hasAccess = true;

                        if (!prod.productId.empty()) {
                            res.subscriptions.push_back(prod);
                        }
                        pos = objEnd + 1;
                    }
                }

                std::cout << OBF_S("[API] Logged in successfully. Found ") << res.subscriptions.size() << OBF_S(" products.\n");
            } else {
                res.message = OBF_S("Invalid response from server");
            }
        } else {
            if (resp.statusCode == 401) {
                res.message = OBF_S("Invalid username or password.");
            } else if (resp.statusCode == 400) {
                res.message = OBF_S("Bad request (Invalid JSON format).");
            } else if (resp.statusCode == 404) {
                res.message = OBF_S("API endpoint not found.");
            } else {
                res.message = OBF_S("Could not connect to Scootware API server (Err: ") + std::to_string(resp.statusCode) + ")";
            }
        }

        return res;
    }

    // -----------------------------------------------------------------------
    // Asset encryption helpers
    // -----------------------------------------------------------------------

    // The AES-256 asset encryption secret should be set via the
    // ASSET_ENCRYPTION_SECRET environment variable as a hex string (64 hex chars = 32 bytes).
    static std::vector<uint8_t> GetAesSecret() {
        const char* hex = std::getenv("ASSET_ENCRYPTION_SECRET");
        if (!hex || std::strlen(hex) != 64) return {};
        std::vector<uint8_t> key(32);
        for (int i = 0; i < 32; i++) {
            std::string byteStr(hex + i * 2, 2);
            key[i] = static_cast<uint8_t>(std::strtol(byteStr.c_str(), nullptr, 16));
        }
        return key;
    }

    // HMAC-SHA256 returning raw bytes (for AES key derivation).
    static std::vector<uint8_t> HmacSha256Raw(const uint8_t* key, size_t keyLen, const std::string& data) {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD cbHash = 0, cbHashObj = 0, cbData = 0;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
            return {};
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbHashObj, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        std::vector<BYTE> hashObj(cbHashObj), hash(cbHash);
        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), cbHashObj, (PUCHAR)key, (ULONG)keyLen, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptHashData(hHash, (PUCHAR)data.c_str(), (ULONG)data.size(), 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptFinishHash(hHash, hash.data(), cbHash, 0) != 0) {
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::vector<uint8_t>(hash.begin(), hash.end());
    }

    // AES-256-GCM decryption via Windows CNG.
    // Input layout: [ 12-byte IV | ciphertext | 16-byte auth tag ]
    static std::vector<uint8_t> AesGcmDecrypt(
        const std::vector<uint8_t>& key32,
        const uint8_t* iv,
        const uint8_t* ciphertext, size_t cipherLen,
        const uint8_t* tag)
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_KEY_HANDLE hKey = nullptr;
        DWORD cbKeyObj = 0, cbData = 0;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
            return {};

        // Switch to GCM mode
        if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbKeyObj, sizeof(DWORD), &cbData, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }

        std::vector<BYTE> keyObj(cbKeyObj);
        if (BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), cbKeyObj,
            (PUCHAR)key32.data(), 32, 0) != 0) {
            BCryptCloseAlgorithmProvider(hAlg, 0); return {};
        }

        // Auth tag buffer — BCryptDecrypt needs a mutable copy
        std::vector<uint8_t> tagCopy(tag, tag + 16);

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce   = (PUCHAR)iv;
        authInfo.cbNonce   = 12;
        authInfo.pbTag     = tagCopy.data();
        authInfo.cbTag     = 16;

        std::vector<uint8_t> plaintext(cipherLen);
        ULONG cbPlaintext = 0;

        NTSTATUS status = BCryptDecrypt(
            hKey, (PUCHAR)ciphertext, (ULONG)cipherLen,
            &authInfo, nullptr, 0,
            plaintext.data(), (ULONG)cipherLen, &cbPlaintext, 0);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (status != 0) {
            std::cout << OBF_S("[API] AES-GCM decryption failed (status 0x") << std::hex << status << std::dec << OBF_S("). HWID mismatch or tampered data.\n");
            return {};
        }
        plaintext.resize(cbPlaintext);
        return plaintext;
    }

    // -----------------------------------------------------------------------

    std::pair<std::vector<uint8_t>, size_t> StreamAsset(const std::string& productId, const std::string& assetType, const std::string& token, const std::string& hwid) {
        LDIAG() << "[api] StreamAsset: product=" << productId << " type=" << assetType << " hwid_len=" << hwid.size();
        std::cout << OBF_S("[API] Fetching encrypted stream for ") << productId << " (" << assetType << ")...\n";
        s_lastStreamHwidMismatch = false;
        s_lastStreamStatusCode = 0;
        s_lastStreamErrorBody.clear();

        std::string path = OBF_S("/api/products/") + productId + OBF_S("/assets/stream?type=") + assetType;
        HttpResponse resp = PerformRequest(OBF_S("GET"), path, "", token, hwid);

        const size_t MIN_ENCRYPTED_LEN = 29;

        if (!resp.success) {
            s_lastStreamStatusCode = resp.statusCode;
            s_lastStreamErrorBody = resp.body;
                LDIAG() << "[api] StreamAsset FAILED: HTTP " << resp.statusCode
                    << " bodySize=" << resp.body.size()
                    << " bodyPreview=" << resp.body.substr(0, (std::min)(resp.body.size(), size_t(200)))
                    << " path=" << path;
            if (resp.statusCode == 403) {
                s_lastStreamHwidMismatch = true;
                std::cout << OBF_S("[API] HWID mismatch.\n");
            } else {
                std::cout << OBF_S("[API] Stream request failed (status ") << resp.statusCode << ").\n";
            }
            return { {}, 0 };
        }

        if (resp.body.size() < MIN_ENCRYPTED_LEN) {
            s_lastStreamStatusCode = resp.statusCode;
            s_lastStreamErrorBody = resp.body;
                LDIAG() << "[api] StreamAsset FAILED: response too small bodySize=" << resp.body.size()
                    << " minRequired=" << MIN_ENCRYPTED_LEN
                    << " bodyPreview=" << resp.body.substr(0, (std::min)(resp.body.size(), size_t(200)));
            std::cout << OBF_S("[API] Stream response too small to be valid encrypted data.\n");
            return { {}, 0 };
        }

        const uint8_t* raw      = reinterpret_cast<const uint8_t*>(resp.body.data());
        size_t          totalLen = resp.body.size();
        const uint8_t* iv        = raw;
        size_t          cipherLen = totalLen - 12 - 16;
        const uint8_t* ciphertext = raw + 12;
        const uint8_t* tag        = raw + 12 + cipherLen;

        // Derive AES-256 key = HMAC-SHA256(embedded_secret, hwid)
        std::vector<uint8_t> secret = GetAesSecret();
        std::vector<uint8_t> aesKey = HmacSha256Raw(secret.data(), secret.size(), hwid);
        SecureZeroMemory(secret.data(), secret.size());

        if (aesKey.empty()) {
            LDIAG() << "[api] StreamAsset FAILED: AES key derivation failed";
            std::cout << OBF_S("[API] Failed to derive AES key.\n");
            return { {}, 0 };
        }

        std::vector<uint8_t> plaintext = AesGcmDecrypt(aesKey, iv, ciphertext, cipherLen, tag);
        SecureZeroMemory(aesKey.data(), aesKey.size());

        if (plaintext.empty()) {
            LDIAG() << "[api] StreamAsset FAILED: AES-GCM decryption failed (possible HWID mismatch) cipherLen=" << cipherLen;
            return { {}, 0 };
        }

        LDIAG() << "[api] StreamAsset OK: " << plaintext.size() << " bytes decrypted";
        std::cout << OBF_S("[API] Decrypted ") << plaintext.size() << OBF_S(" bytes successfully.\n");
        return { plaintext, resp.allocationSizeHeader };
    }

    HwidResetResponse SubmitHwidResetRequest(const std::string& token, const std::string& newHwid, const Hwid::HardwareDetails& newDetails) {
        HwidResetResponse res = { false, "" };

        // Build a minimal JSON payload with the new machine's HWID + human-readable details
        std::string cpu = JsonEscape(newDetails.cpu);
        std::string gpu = JsonEscape(newDetails.gpu);
        std::string ram = std::to_string(newDetails.ramGb);
        std::string hwid = JsonEscape(newHwid);

        std::string payload =
            OBF_S("{\"newHwid\":\"") + hwid + OBF_S("\",")
          + OBF_S("\"newHwidDetails\":{\"cpu\":\"") + cpu
          + OBF_S("\",\"gpu\":\"") + gpu
          + OBF_S("\",\"ramGb\":") + ram + OBF_S("}}");

        HttpResponse resp = PerformRequest(OBF_S("POST"), OBF_S("/api/hwid-reset"), payload, token);

        if (resp.success) {
            res.success = true;
            // Prefer the server's own message (covers new/updated/idempotent cases).
            std::string serverMsg = GetJsonValue(resp.body, OBF_S("message"));
            res.message = serverMsg.empty()
                ? OBF_S("Reset request submitted. An admin will review it shortly.")
                : serverMsg;
        } else if (resp.statusCode == 401) {
            res.success = false;
            res.message = OBF_S("Session expired. Please log in again.");
        } else {
            res.success = false;
            std::string serverErr = GetJsonValue(resp.body, OBF_S("error"));
            res.message = serverErr.empty()
                ? OBF_S("Failed to submit reset request (Err: ") + std::to_string(resp.statusCode) + ")"
                : serverErr;
        }
        return res;
    }

    void ReportDetection(bool vmDetected, bool debuggerDetected,
                         const std::string& hwid,
                         const std::vector<std::string>& triggers)
    {
        // Build a semicolon-separated details string from all triggered vectors
        std::string details;
        for (size_t i = 0; i < triggers.size(); ++i) {
            if (i) details += "; ";
            details += triggers[i];
        }

        std::string safeHwid    = JsonEscape(hwid.empty() ? OBF_S("UNKNOWN") : hwid);
        std::string safeDetails = JsonEscape(details.empty() ? OBF_S("unknown") : details);

        std::string eventType = OBF_S("detection");
        if (vmDetected && debuggerDetected) eventType = OBF_S("vm_and_debugger_detected");
        else if (vmDetected)               eventType = OBF_S("vm_detected");
        else if (debuggerDetected)         eventType = OBF_S("debugger_detected");

        std::string payload =
            OBF_S("{\"hwid\":\"") + safeHwid + OBF_S("\",")
          + OBF_S("\"vmDetected\":") + (vmDetected ? OBF_S("true") : OBF_S("false")) + OBF_S(",")
          + OBF_S("\"debuggerDetected\":") + (debuggerDetected ? OBF_S("true") : OBF_S("false")) + OBF_S(",")
          + OBF_S("\"eventType\":\"") + eventType + OBF_S("\",")
          + OBF_S("\"details\":\"") + safeDetails + OBF_S("\"}");

        // Best-effort: fire and forget. No token needed — unauthenticated endpoint
        // exists specifically for pre-login loader reporting.
        PerformRequest(OBF_S("POST"), OBF_S("/api/auth/loader-event"), payload);
    }

    HeartbeatResponse Heartbeat(const std::string& token,
                                const std::string& hwid,
                                const std::string& productId)
    {
        HeartbeatResponse r{ false, 0, "" };

        // Anti-replay: server enforces |now - ts| < 30s and (userId, nonce)
        // uniqueness over a 5-min window. Both fields are mandatory.
        uint64_t nowMs = (uint64_t)GetTickCount64();
        uint64_t nonce = ((uint64_t)GetCurrentProcessId() << 32) ^ nowMs;

        std::string payload =
            OBF_S("{\"hwid\":\"")     + JsonEscape(hwid)      + OBF_S("\",")
          + OBF_S("\"productId\":\"") + JsonEscape(productId) + OBF_S("\",")
          + OBF_S("\"uptimeMs\":")    + std::to_string(nowMs) + OBF_S(",")
          + OBF_S("\"nonce\":")       + std::to_string(nonce) + OBF_S(",")
          + OBF_S("\"ts\":")          + std::to_string(nowMs) + OBF_S("}");

        HttpResponse resp = PerformRequest(
            OBF_S("POST"), OBF_S("/api/auth/heartbeat"), payload, token, hwid);

        r.statusCode = resp.statusCode;
        if (resp.success) {
            std::string ok = GetJsonValue(resp.body, OBF_S("ok"));
            r.success = (ok == "true" || resp.statusCode == 200);
            r.reason  = GetJsonValue(resp.body, OBF_S("reason"));
        } else {
            r.reason = GetJsonValue(resp.body, OBF_S("reason"));
        }
        return r;
    }

    DriverBringup::MapperConfig GetMapperConfig(const std::string& productId,
                                                const std::string& token)
    {
        DriverBringup::MapperConfig cfg; // default-initialised safe fallback

        std::string path = OBF_S("/api/products/") + productId + OBF_S("/mapper-config");
        HttpResponse resp = PerformRequest(OBF_S("GET"), path, "", token);

        if (!resp.success || resp.body.empty()) {
            std::cout << OBF_S("[API] GetMapperConfig failed (status ") << resp.statusCode
                      << OBF_S("), using defaults.\n");
            return cfg;
        }

        std::string pidStr  = GetJsonValue(resp.body, OBF_S("providerPublicId"));
        std::string scvStr  = GetJsonValue(resp.body, OBF_S("shellCodeVersion"));
        std::string dseStr  = GetJsonValue(resp.body, OBF_S("requireDseSuccess"));
        std::string objName = GetJsonValue(resp.body, OBF_S("driverObjectName"));
        std::string regPath = GetJsonValue(resp.body, OBF_S("driverRegistryPath"));

        if (!pidStr.empty()) {
            try { cfg.providerPublicId = std::stoi(pidStr); }
            catch (...) { std::cout << OBF_S("[API] GetMapperConfig: bad providerPublicId '") << pidStr << "'\n"; }
        }
        if (!scvStr.empty()) {
            try { cfg.shellCodeVersion = std::stoi(scvStr); }
            catch (...) { std::cout << OBF_S("[API] GetMapperConfig: bad shellCodeVersion '") << scvStr << "'\n"; }
        }
        if (!dseStr.empty())  cfg.requireDseSuccess = (dseStr == OBF_S("true"));
        if (!objName.empty()) cfg.driverObjectName  = objName;
        if (!regPath.empty()) cfg.driverRegistryPath = regPath;

        return cfg;
    }

    InjectConfig GetInjectConfig(const std::string& productId,
                                 const std::string& token)
    {
        InjectConfig cfg; // default-initialised safe fallback

        std::string path = OBF_S("/api/products/") + productId + OBF_S("/inject-config");
        HttpResponse resp = PerformRequest(OBF_S("GET"), path, "", token);

        if (!resp.success || resp.body.empty()) {
            std::cout << OBF_S("[API] GetInjectConfig failed (status ") << resp.statusCode
                      << OBF_S("), using defaults.\n");
            return cfg;
        }

        std::string nameStr = GetJsonValue(resp.body, OBF_S("targetProcessName"));
        std::string modeStr = GetJsonValue(resp.body, OBF_S("allocMode"));

        if (!nameStr.empty()) cfg.targetProcessName = nameStr;
        if (!modeStr.empty()) {
            try { cfg.allocMode = static_cast<uint32_t>(std::stoul(modeStr)); }
            catch (...) { std::cout << OBF_S("[API] GetInjectConfig: bad allocMode '") << modeStr << "'\n"; }
        }

        return cfg;
    }

    void ReportLoaderEvent(const std::string& eventType,
                           const std::string& productId,
                           const std::string& hwid,
                           const std::string& details,
                           const std::string& token)
    {
        std::string safeHwid    = JsonEscape(hwid.empty()    ? OBF_S("UNKNOWN") : hwid);
        std::string safeProduct = JsonEscape(productId);
        std::string safeDetails = JsonEscape(details);
        std::string safeType    = JsonEscape(eventType);

        std::string payload =
            OBF_S("{\"hwid\":\"") + safeHwid + OBF_S("\",")
          + OBF_S("\"vmDetected\":false,")
          + OBF_S("\"debuggerDetected\":false,")
          + OBF_S("\"eventType\":\"") + safeType + OBF_S("\",")
          + OBF_S("\"productId\":\"") + safeProduct + OBF_S("\",")
          + OBF_S("\"details\":\"") + safeDetails + OBF_S("\"}");

        // Fire-and-forget; token is optional and passed through when present so
        // the backend can attribute the event to the authenticated user.
        PerformRequest(OBF_S("POST"), OBF_S("/api/auth/loader-event"), payload, token);
    }
}
