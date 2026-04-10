#include "api.h"
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace Api {

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
        // Default to production domain
        ApiSettings s = { L"scootware.us", 443, true };

        // For local development, check if we can reach localhost:3000
        // In a real app, you might check a 'config.json' file here.
        // We'll add a simple check for a local file if it exists.
        if (GetFileAttributesA("dev_mode.txt") != INVALID_FILE_ATTRIBUTES) {
            s.host = L"localhost";
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

    HttpResponse PerformRequest(const std::string& method, const std::string& path, const std::string& postData = "", const std::string& token = "") {
        HttpResponse response = { false, "", 0, 0 };
        ApiSettings settings = GetSettings();
        
        HINTERNET hSession = WinHttpOpen(L"ScootwareLoader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return response;

        HINTERNET hConnect = WinHttpConnect(hSession, settings.host.c_str(), settings.port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return response;
        }

        std::wstring wPath(path.begin(), path.end());
        std::wstring wMethod(method.begin(), method.end());

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (settings.secure ? WINHTTP_FLAG_SECURE : 0));
        
        if (hRequest) {
            std::wstring headers = L"Content-Type: application/json\r\n";
            if (!token.empty()) {
                std::wstring wToken(token.begin(), token.end());
                headers += L"Authorization: Bearer " + wToken + L"\r\n";
            }

            BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), -1, (LPVOID)postData.c_str(), (DWORD)postData.length(), (DWORD)postData.length(), 0);
            
            if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

            if (bResults) {
                DWORD dwStatusCode = 0;
                DWORD dwSize = sizeof(dwStatusCode);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
                response.statusCode = dwStatusCode;
                
                std::cout << "[API] Request to " << path << " -> Status: " << dwStatusCode << "\n";

                dwSize = 0;
                DWORD dwDownloaded = 0;
                
                // Extract X-Allocation-Size header if present
                wchar_t headerBuffer[256];
                DWORD headerSize = sizeof(headerBuffer);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"X-Allocation-Size", headerBuffer, &headerSize, WINHTTP_NO_HEADER_INDEX)) {
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
        
        size_t start = json.find("\"", pos);
        if (start == std::string::npos) {
            // Might be a boolean or number
            size_t valStart = json.find_first_not_of(": ", pos + 1);
            size_t valEnd = json.find_first_of(",}", valStart);
            if (valStart == std::string::npos || valEnd == std::string::npos) return "";
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
        std::string jsonPayload = "{\"identifier\":\"" + escapedUser + "\",\"password\":\"" + escapedPass + "\"}";
        
        std::cout << "[API] Attempting login for: " << username << "...\n";

        // Using /api/auth/login route (hypothetical, matches standard forum patterns)
        HttpResponse resp = PerformRequest("POST", "/api/auth/login", jsonPayload);
        
        if (resp.success) {
            std::string userObj = GetJsonValue(resp.body, "user");
            if (!userObj.empty()) {
                res.success = true;
                res.token = GetJsonValue(resp.body, "token");
                res.message = "Login successful!";
                
                // Parse activeProducts
                std::string productsArray = GetJsonArray(resp.body, "activeProducts");
                if (!productsArray.empty()) {
                    size_t pos = 0;
                    while (true) {
                        size_t objStart = productsArray.find("{", pos);
                        if (objStart == std::string::npos) break;
                        size_t objEnd = productsArray.find("}", objStart);
                        if (objEnd == std::string::npos) break;

                        std::string obj = productsArray.substr(objStart, objEnd - objStart + 1);
                        
                        ProductAccess prod;
                        prod.productId = GetJsonValue(obj, "productId");
                        prod.expiresAt = GetJsonValue(obj, "expiresAt");
                        prod.tier      = GetJsonValue(obj, "tier");
                        prod.hasAccess = true;

                        if (!prod.productId.empty()) {
                            res.subscriptions.push_back(prod);
                        }
                        pos = objEnd + 1;
                    }
                }
                
                std::cout << "[API] Logged in successfully. Found " << res.subscriptions.size() << " products.\n";
            } else {
                res.message = "Invalid response from server";
            }
        } else {
            if (resp.statusCode == 401) {
                res.message = "Invalid username or password.";
            } else if (resp.statusCode == 400) {
                res.message = "Bad request (Invalid JSON format).";
            } else if (resp.statusCode == 404) {
                res.message = "API endpoint not found.";
            } else {
                res.message = "Could not connect to Scootware API server (Err: " + std::to_string(resp.statusCode) + ")";
            }
        }

        return res;
    }

    std::pair<std::vector<uint8_t>, size_t> StreamAsset(const std::string& productId, const std::string& assetType, const std::string& token) {
        std::cout << "[API] Fetching stream for " << productId << " (" << assetType << ")...\n";
        
        // Hypothetical route: /api/products/:productId/assets/primary/stream
        std::string path = "/api/products/" + productId + "/assets/stream?type=" + assetType;
        HttpResponse resp = PerformRequest("GET", path, "", token);
        
        if (resp.success && !resp.body.empty()) {
            std::vector<uint8_t> buffer(resp.body.begin(), resp.body.end());
            std::cout << "[API] Downloaded " << buffer.size() << " bytes.\n";
            return { buffer, resp.allocationSizeHeader };
        }
        
        return { {}, 0 };
    }
}
