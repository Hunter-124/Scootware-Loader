#pragma once
#include <string>
#include <vector>

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

    // Authenticate and return user subscriptions
    AuthResponse Login(const std::string& username, const std::string& password);

    // Stream a specific asset from the backend into a byte buffer
    // Returns pair<PayloadBuffer, ExpectedAllocationSize>
    std::pair<std::vector<uint8_t>, size_t> StreamAsset(const std::string& productId, const std::string& assetType, const std::string& token);
}
