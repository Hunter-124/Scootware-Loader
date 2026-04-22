#include "image_loader.h"
#include "security/obf.h"

#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <objbase.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace ImageLoader {

    struct Texture {
        ID3D11ShaderResourceView* srv = nullptr;
        int width  = 0;
        int height = 0;
    };

    struct DecodedImage {
        std::string             key;
        int                     width  = 0;
        int                     height = 0;
        std::vector<uint8_t>    pixels; // RGBA8
    };

    struct Job {
        std::string key;
        std::string path; // path or full URL
    };

    static ID3D11Device*           g_device = nullptr;
    static ID3D11DeviceContext*    g_ctx    = nullptr;

    static std::mutex                                       g_cacheMx;
    static std::unordered_map<std::string, Texture>         g_cache;        // ready-on-GPU
    static std::unordered_set<std::string>                  g_pending;      // requested or in-flight

    static std::mutex                       g_jobsMx;
    static std::condition_variable          g_jobsCv;
    static std::queue<Job>                  g_jobs;
    static std::atomic<bool>                g_workerExit{false};
    static std::thread                      g_worker;

    static std::mutex                       g_doneMx;
    static std::queue<DecodedImage>         g_done;

    // -------------------------------------------------------------------------
    //  HTTP fetch
    // -------------------------------------------------------------------------
    static bool ParseUrl(const std::string& url,
                         std::wstring& host, INTERNET_PORT& port,
                         std::wstring& path, bool& secure)
    {
        // Defaults: relative to scootware.us
        host   = OBF_W(L"scootware.us");
        port   = 443;
        secure = true;

        std::string in = url;
        if (in.rfind("http://",  0) == 0 || in.rfind("https://", 0) == 0) {
            secure        = (in.rfind("https://", 0) == 0);
            size_t scheme = in.find("://");
            std::string rest = in.substr(scheme + 3);
            size_t slash = rest.find('/');
            std::string hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
            std::string p        = (slash == std::string::npos) ? "/"  : rest.substr(slash);

            size_t colon = hostPort.find(':');
            std::string h = (colon == std::string::npos) ? hostPort : hostPort.substr(0, colon);
            port = secure ? 443 : 80;
            if (colon != std::string::npos) {
                try { port = (INTERNET_PORT)std::stoi(hostPort.substr(colon + 1)); } catch (...) {}
            }
            host = std::wstring(h.begin(), h.end());
            path = std::wstring(p.begin(), p.end());
        } else {
            std::string p = in.empty() || in[0] != '/' ? "/" + in : in;
            path = std::wstring(p.begin(), p.end());
        }
        return !host.empty() && !path.empty();
    }

    static bool HttpGet(const std::string& url, std::vector<uint8_t>& out) {
        std::wstring   host, path;
        INTERNET_PORT  port;
        bool           secure;
        if (!ParseUrl(url, host, port, path, secure)) return false;

        HINTERNET hSession = WinHttpOpen(OBF_C(L"ScootwareLoader/1.0"),
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, OBF_C(L"GET"), path.c_str(),
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                secure ? WINHTTP_FLAG_SECURE : 0);
        bool ok = false;
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL))
            {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(hRequest,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                    WINHTTP_NO_HEADER_INDEX);
                if (status >= 200 && status < 300) {
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                        size_t base = out.size();
                        out.resize(base + avail);
                        DWORD read = 0;
                        if (!WinHttpReadData(hRequest, out.data() + base, avail, &read)) break;
                        out.resize(base + read);
                    }
                    ok = true;
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
    }

    // -------------------------------------------------------------------------
    //  WIC decode → BGRA8 buffer
    // -------------------------------------------------------------------------
    static IWICImagingFactory* g_wic = nullptr;

    static bool EnsureWic() {
        if (g_wic) return true;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&g_wic));
        return SUCCEEDED(hr);
    }

    static bool Decode(const std::vector<uint8_t>& bytes, DecodedImage& out) {
        if (!EnsureWic() || bytes.empty()) return false;

        IWICStream* stream = nullptr;
        if (FAILED(g_wic->CreateStream(&stream))) return false;
        if (FAILED(stream->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size()))) {
            stream->Release(); return false;
        }

        IWICBitmapDecoder* decoder = nullptr;
        if (FAILED(g_wic->CreateDecoderFromStream(stream, nullptr,
                                                  WICDecodeMetadataCacheOnLoad, &decoder))) {
            stream->Release(); return false;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(decoder->GetFrame(0, &frame))) {
            decoder->Release(); stream->Release(); return false;
        }

        IWICFormatConverter* conv = nullptr;
        if (FAILED(g_wic->CreateFormatConverter(&conv))) {
            frame->Release(); decoder->Release(); stream->Release(); return false;
        }
        if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                    WICBitmapDitherTypeNone, nullptr,
                                    0.0, WICBitmapPaletteTypeMedianCut))) {
            conv->Release(); frame->Release(); decoder->Release(); stream->Release(); return false;
        }

        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);

        // Cap very large images so we don't waste memory on a 4K product photo.
        const UINT MAX_DIM = 1024;
        if (w > MAX_DIM || h > MAX_DIM) {
            float s = (float)MAX_DIM / (float)((w > h) ? w : h);
            UINT nw = (UINT)((float)w * s);
            UINT nh = (UINT)((float)h * s);

            IWICBitmapScaler* scaler = nullptr;
            if (SUCCEEDED(g_wic->CreateBitmapScaler(&scaler))) {
                if (SUCCEEDED(scaler->Initialize(conv, nw, nh, WICBitmapInterpolationModeFant))) {
                    UINT cnt = nw * nh * 4;
                    out.pixels.resize(cnt);
                    if (SUCCEEDED(scaler->CopyPixels(nullptr, nw * 4, cnt, out.pixels.data()))) {
                        out.width = (int)nw;
                        out.height = (int)nh;
                        scaler->Release();
                        conv->Release(); frame->Release(); decoder->Release(); stream->Release();
                        return true;
                    }
                }
                scaler->Release();
            }
        }

        UINT cnt = w * h * 4;
        out.pixels.resize(cnt);
        bool ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, cnt, out.pixels.data()));
        out.width  = (int)w;
        out.height = (int)h;

        conv->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        return ok;
    }

    // -------------------------------------------------------------------------
    //  Worker
    // -------------------------------------------------------------------------
    static void WorkerMain() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        while (!g_workerExit.load()) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(g_jobsMx);
                g_jobsCv.wait(lk, [] { return g_workerExit.load() || !g_jobs.empty(); });
                if (g_workerExit.load()) break;
                job = std::move(g_jobs.front());
                g_jobs.pop();
            }

            std::vector<uint8_t> bytes;
            if (!HttpGet(job.path, bytes)) continue;

            DecodedImage img;
            img.key = job.key;
            if (!Decode(bytes, img)) continue;

            std::lock_guard<std::mutex> lk(g_doneMx);
            g_done.push(std::move(img));
        }

        if (g_wic) { g_wic->Release(); g_wic = nullptr; }
        CoUninitialize();
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------
    void Init(ID3D11Device* device, ID3D11DeviceContext* ctx) {
        g_device = device;
        g_ctx    = ctx;
        g_workerExit = false;
        g_worker = std::thread(WorkerMain);
    }

    void Shutdown() {
        g_workerExit = true;
        g_jobsCv.notify_all();
        if (g_worker.joinable()) g_worker.join();

        std::lock_guard<std::mutex> lk(g_cacheMx);
        for (auto& kv : g_cache) {
            if (kv.second.srv) { kv.second.srv->Release(); kv.second.srv = nullptr; }
        }
        g_cache.clear();
    }

    void Request(const std::string& key, const std::string& path) {
        {
            std::lock_guard<std::mutex> lk(g_cacheMx);
            if (g_cache.count(key))   return;
            if (g_pending.count(key)) return;
            g_pending.insert(key);
        }
        {
            std::lock_guard<std::mutex> lk(g_jobsMx);
            g_jobs.push({ key, path });
        }
        g_jobsCv.notify_one();
    }

    ID3D11ShaderResourceView* Get(const std::string& key, int* outW, int* outH) {
        std::lock_guard<std::mutex> lk(g_cacheMx);
        auto it = g_cache.find(key);
        if (it == g_cache.end()) return nullptr;
        if (outW) *outW = it->second.width;
        if (outH) *outH = it->second.height;
        return it->second.srv;
    }

    void Pump() {
        if (!g_device) return;

        for (;;) {
            DecodedImage img;
            {
                std::lock_guard<std::mutex> lk(g_doneMx);
                if (g_done.empty()) return;
                img = std::move(g_done.front());
                g_done.pop();
            }

            if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) continue;

            D3D11_TEXTURE2D_DESC td = {};
            td.Width            = img.width;
            td.Height           = img.height;
            td.MipLevels        = 1;
            td.ArraySize        = 1;
            td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage            = D3D11_USAGE_DEFAULT;
            td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA srd = {};
            srd.pSysMem     = img.pixels.data();
            srd.SysMemPitch = img.width * 4;

            ID3D11Texture2D* tex = nullptr;
            if (FAILED(g_device->CreateTexture2D(&td, &srd, &tex)) || !tex) continue;

            ID3D11ShaderResourceView* srv = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format              = td.Format;
            sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            HRESULT hr = g_device->CreateShaderResourceView(tex, &sd, &srv);
            tex->Release();
            if (FAILED(hr) || !srv) continue;

            std::lock_guard<std::mutex> lk(g_cacheMx);
            g_cache[img.key] = { srv, img.width, img.height };
            g_pending.erase(img.key);
        }
    }
}
