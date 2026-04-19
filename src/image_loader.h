#pragma once
#include <string>
#include <d3d11.h>

namespace ImageLoader {
    void Init(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    // Request an image to be loaded asynchronously.
    //  key:  unique cache identifier
    //  path: HTTP path on scootware.us (e.g. "/images/products/bodycam.jpg")
    //        OR a fully-qualified "https://host/path" URL.
    // Idempotent — repeated calls with the same key are ignored.
    void Request(const std::string& key, const std::string& path);

    // Returns the SRV (and optional pixel size) for a previously requested key,
    // or nullptr if the image is still loading or failed.
    ID3D11ShaderResourceView* Get(const std::string& key, int* outW = nullptr, int* outH = nullptr);

    // Drain the worker decode queue and create GPU textures.
    // Must be called from the UI / D3D thread once per frame.
    void Pump();
}
