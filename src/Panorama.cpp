// Renderer Foundation - Main Menu panorama (see Panorama.h). Original code for Renderer Foundation.
#include "Panorama.h"

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace rf::pano {
namespace {
std::mutex gM;
int        gLastId = -1;
int        gPhase  = 0;        // 0 idle, 1 holding faces, 2 waiting for the copies, 3 writing
int        gFace   = 0;        // the face being held
int        gHeld   = 0;        // presents this face has been held
int64_t    gFaceAt = 0;        // when this face started (ms)
int64_t    msNow() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool       gDue    = false;    // grab the current face in the next world pass
int        gWait   = 0;        // presents since the last copy (phase 2)
float      gYaw0   = 0;        // your yaw when the capture began: the panorama's front
float      gBoost  = 1.0f;     // extra widening when the measured view came out under 90 degrees
bool       gHaveYaw = false;
char       gStatus[160] = "";
struct Face {
    ComPtr<ID3D12Resource> buf;
    UINT   w = 0, h = 0;
    UINT64 pitch = 0;
    bool   bgra = false;
    float  tanX = 1, tanY = 1;
    bool   ok = false;
} gFaces[6];
std::atomic<bool> gCapturing{false};

// face order of the title screen's cube: front, right, back, left, up, down (yaw + = turning right)
constexpr float kYawOff[6]   = {0.f, 90.f, 180.f, 270.f, 0.f, 0.f};
constexpr float kPitch[6]    = {0.f, 0.f, 0.f, 0.f, -89.5f, 89.5f};   // (exactly 90 makes the view matrix degenerate)
constexpr int   kSettle      = 12;  // presents to hold a face before copying it (the camera write lands a frame late)
constexpr int64_t kSettleMs  = 700; // and at least this long: chunks behind you are only built once they come into view

fs::path packDir() {
    wchar_t const* ad = _wgetenv(L"APPDATA");
    if (!ad) return {};
    return fs::path(ad) / L"Minecraft Bedrock" / L"Users" / L"Shared" / L"games" / L"com.mojang" / L"development_resource_packs" / L"BF Menu World";
}
void writeManifest(fs::path const& dir) {
    std::error_code ec;
    fs::create_directories(dir / "textures" / "ui", ec);
    if (fs::exists(dir / "manifest.json", ec)) return;
    std::ofstream(dir / "manifest.json") << R"JSON({
  "format_version": 2,
  "header": {
    "name": "BF Menu World",
    "description": "Your world behind the menus (Bedrock Forge Main Menu)",
    "uuid": "b7f1e4a2-6c3d-4e58-9a71-2f0d5c8e3b14",
    "version": [1, 0, 0],
    "min_engine_version": [1, 21, 0]
  },
  "modules": [{ "type": "resources", "uuid": "c2a8d6f0-1b47-4f93-8e25-7d6c4b1a9e30", "version": [1, 0, 0] }]
}
)JSON";
}
float toLin(uint8_t v) {
    float const c = float(v) / 255.f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
// Radiance RGBE, new-style run-length scanlines (literal runs only)
bool writeHdr(fs::path const& file, std::vector<float> const& rgb, int S) {
    std::ofstream o(file, std::ios::binary);
    if (!o) return false;
    o << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n\n-Y " << S << " +X " << S << "\n";
    std::vector<uint8_t> row(size_t(S) * 4);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float const* p = &rgb[(size_t(y) * S + x) * 3];
            float const m = std::max({p[0], p[1], p[2]});
            uint8_t* e = &row[size_t(x) * 4];
            if (m < 1e-32f) { e[0] = e[1] = e[2] = e[3] = 0; continue; }
            int ex = 0;
            float const mant = std::frexp(m, &ex) * 256.f / m;
            e[0] = uint8_t(std::clamp(p[0] * mant, 0.f, 255.f));
            e[1] = uint8_t(std::clamp(p[1] * mant, 0.f, 255.f));
            e[2] = uint8_t(std::clamp(p[2] * mant, 0.f, 255.f));
            e[3] = uint8_t(ex + 128);
        }
        uint8_t const head[4] = {2, 2, uint8_t(S >> 8), uint8_t(S & 255)};
        o.write(reinterpret_cast<char const*>(head), 4);
        for (int c = 0; c < 4; ++c)
            for (int x = 0; x < S;) {
                int const n = std::min(128, S - x);
                o.put(char(n));
                for (int k = 0; k < n; ++k) o.put(char(row[size_t(x + k) * 4 + c]));
                x += n;
            }
    }
    return bool(o);
}
// one face: the centre of the copy that spans exactly 90 degrees, resampled to S x S, linear light
std::vector<float> cropFace(Face const& f, uint8_t const* px, int S) {
    std::vector<float> out(size_t(S) * S * 3);
    float const hx = float(f.w) * 0.5f / std::max(f.tanX, 1e-3f);   // px per unit of tan along x / y
    float const hy = float(f.h) * 0.5f / std::max(f.tanY, 1e-3f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float const u = (float(x) + 0.5f) / float(S) * 2.f - 1.f, v = (float(y) + 0.5f) / float(S) * 2.f - 1.f;
            float const sx = float(f.w) * 0.5f + u * hx - 0.5f, sy = float(f.h) * 0.5f + v * hy - 0.5f;
            int const x0 = std::clamp(int(std::floor(sx)), 0, int(f.w) - 2), y0 = std::clamp(int(std::floor(sy)), 0, int(f.h) - 2);
            float const fx = std::clamp(sx - float(x0), 0.f, 1.f), fy = std::clamp(sy - float(y0), 0.f, 1.f);
            float acc[3]{};
            for (int k = 0; k < 4; ++k) {
                int const xx = x0 + (k & 1), yy = y0 + (k >> 1);
                float const w = ((k & 1) ? fx : 1.f - fx) * ((k >> 1) ? fy : 1.f - fy);
                uint8_t const* p = px + size_t(yy) * f.pitch + size_t(xx) * 4;
                float const r = toLin(f.bgra ? p[2] : p[0]), g = toLin(p[1]), b = toLin(f.bgra ? p[0] : p[2]);
                acc[0] += r * w; acc[1] += g * w; acc[2] += b * w;
            }
            float* o = &out[(size_t(y) * S + x) * 3];
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    return out;
}
void writeAll() {
    fs::path const dir = packDir();
    if (dir.empty()) { std::snprintf(gStatus, sizeof gStatus, "no APPDATA: can't find the game's packs"); return; }
    writeManifest(dir);
    int done = 0;
    for (int i = 0; i < 6; ++i) {
        Face& f = gFaces[i];
        if (!f.ok || !f.buf) continue;
        void* m = nullptr;
        D3D12_RANGE rr{0, size_t(f.pitch) * f.h};
        if (FAILED(f.buf->Map(0, &rr, &m)) || !m) continue;
        int const S = int(std::min<UINT>(1536u, std::min(f.w, f.h)));
        auto const img = cropFace(f, static_cast<uint8_t const*>(m), S);
        D3D12_RANGE none{0, 0};
        f.buf->Unmap(0, &none);
        bool ok = writeHdr(dir / "textures" / "ui" / ("panoramahdr_" + std::to_string(i) + ".hdr"), img, S);
        ok = writeHdr(dir / "textures" / "ui" / ("panoramahdr_alternate_" + std::to_string(i) + ".hdr"), img, S) && ok;
        done += ok;
        f.buf.Reset();
        f.ok = false;
    }
    std::snprintf(gStatus, sizeof gStatus, done == 6 ? "menu world saved: it shows from the next start" : "menu world: only %d of 6 faces saved", done);
}
} // namespace

void request(int id) {
    std::lock_guard l(gM);
    if (gLastId < 0) { gLastId = id; return; }   // the first value read is the old one, not a request
    if (id == gLastId || gPhase != 0) return;
    gLastId = id;
    gPhase = 1; gFace = 0; gHeld = 0; gFaceAt = msNow(); gDue = false; gHaveYaw = false; gBoost = 1.0f;
    for (auto& f : gFaces) { f.buf.Reset(); f.ok = false; }
    gCapturing = true;
    std::snprintf(gStatus, sizeof gStatus, "capturing the menu world...");
}
bool capturing() { return gCapturing.load(std::memory_order_relaxed); }

bool pose(float baseFovRad, float& yaw, float& pitch, float& fovScale) {
    std::lock_guard l(gM);
    if (gPhase != 1) return false;
    if (!gHaveYaw) { gYaw0 = yaw; gHaveYaw = true; }
    yaw   = gYaw0 + kYawOff[gFace];
    pitch = kPitch[gFace];
    // a little over 90 degrees (cropped back to exactly 90 from the real projection); the game's fov is vertical
    fovScale = baseFovRad > 0.1f ? std::min(2.6f, (95.f * 3.14159265f / 180.f) / baseFovRad * gBoost) : 1.f;
    return true;
}

void grab(ID3D12Device* dev, ID3D12GraphicsCommandList* list, ID3D12Resource* rt, D3D12_RESOURCE_DESC const& rd, bool bgra, float tanX,
          float tanY) {
    int face;
    {
        std::lock_guard l(gM);
        if (gPhase != 1 || !gDue) return;
        face = gFace;
        gDue = false;
    }
    if (tanY < 0.99f || tanX < 0.99f) {   // the view did not widen enough: widen a little more and hold the face again
        std::lock_guard l(gM);
        gHeld = 0;
        if (gBoost < 2.0f) gBoost *= 1.08f;
        else { gPhase = 0; gCapturing = false; std::snprintf(gStatus, sizeof gStatus, "menu world: the game would not widen the view to 90 degrees"); }
        return;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buf;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buf)))) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = rt; b.Transition.Subresource = 0;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = buf.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    src.pResource = rt; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    list->ResourceBarrier(1, &b);
    std::lock_guard l(gM);
    Face& f = gFaces[face];
    f.buf = buf; f.w = UINT(rd.Width); f.h = rd.Height; f.pitch = fp.Footprint.RowPitch; f.bgra = bgra; f.tanX = tanX; f.tanY = tanY; f.ok = true;
    // next face (or done)
    gHeld = 0; gFaceAt = msNow();
    if (++gFace >= 6) { gPhase = 2; gWait = 0; }
}

void onPresent() {
    std::lock_guard l(gM);
    if (gPhase == 1) {
        if (++gHeld >= kSettle && msNow() - gFaceAt >= kSettleMs) gDue = true;
    } else if (gPhase == 2) {
        if (++gWait >= 8) {   // the copies have long finished on the GPU
            gPhase = 3;
            gCapturing = false;
            std::thread([] {
                writeAll();
                std::lock_guard l2(gM);
                gPhase = 0;
            }).detach();
        }
    }
}
char const* status() { return gStatus; }

} // namespace rf::pano
