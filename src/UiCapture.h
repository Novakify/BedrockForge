// Renderer Foundation - Spatial GUI port, milestone M0 "capture spike" (docs/PORT_SPATIALGUI.md in Bedrock Forge).
// Question it answers: can we get Bedrock's UI (inventory screen) as an image by copying the frame right before the
// UI pass and again at Present ("method 1")?
//   * every pipeline whose pixel shader is a vanilla UI shader (config/ui_shaders.txt: FNV-1a 64 of the DXIL, made
//     from the game's UI* / Gameface* materials) is tagged at creation
//   * on request (F7 or the F6 button) the next frame is watched: right before the first draw with a UI pipeline, the
//     bound render target is copied ("A"); at Present the finished back buffer is copied ("B")
//   * A, B and |B-A| are written as .bmp with a report (which target the UI draws to, how many UI draws/targets,
//     changed pixels + bounding box, verdict) to <mod>/logs/uicapture/<time>/
// Everything is inert unless "uiCaptureSpike": true in RendererFoundation.json (the hooks aren't even installed).
#pragma once
#include <filesystem>
#include <string>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;

namespace rf::uicapture {

struct Targets {
    void* createGraphicsPso = nullptr; // ID3D12Device::CreateGraphicsPipelineState (vtable 10)
    void* createStreamPso   = nullptr; // ID3D12Device2::CreatePipelineState (vtable 47), counted only
    void* createRtv         = nullptr; // ID3D12Device::CreateRenderTargetView (vtable 20)
    void* setPso            = nullptr; // ID3D12GraphicsCommandList::SetPipelineState (vtable 25)
    void* omSetRts          = nullptr; // ID3D12GraphicsCommandList::OMSetRenderTargets (vtable 46)
    void* drawInstanced     = nullptr; // ID3D12GraphicsCommandList::DrawInstanced (vtable 12)
    void* drawIndexed       = nullptr; // ID3D12GraphicsCommandList::DrawIndexedInstanced (vtable 13)
    // M0.5: resolve RTV handles that were never seen at CreateRenderTargetView
    void* createHeap        = nullptr; // ID3D12Device::CreateDescriptorHeap (vtable 14)
    void* copyDescs         = nullptr; // ID3D12Device::CopyDescriptors (vtable 23)
    void* copyDescsSimple   = nullptr; // ID3D12Device::CopyDescriptorsSimple (vtable 24)
    void* resourceBarrier   = nullptr; // ID3D12GraphicsCommandList::ResourceBarrier (vtable 26)
};

/// Load config/ui_shaders.txt; output goes under outDir.
void init(std::filesystem::path const& configDir, std::filesystem::path const& outDir);
/// Install the hooks (only when the option is on).
bool install(Targets const& t);
void uninstall();
/// Capture the next frame (F7 / F6 button).
void request();
/// Learn the back-buffer RTV handles (tracking frames only, no capture/report) - used by the M1 panel.
/// Spatial GUI: UI pipelines whose alpha blending was switched to "over" at creation (v0.12).
long uiAlphaFixedCount();
void requestLearn();
/// Called by the overlay at Present before it records anything; `bb` is the current back buffer (PRESENT state).
void beforeOverlay(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* bb);
/// F6 section.
void drawUi();
/// Write a small text file into the capture output folder (diagnostics).
void writeText(char const* name, char const* text);
/// Flicker diagnostics: draws that reached the screen while the UI went to the panel target.
std::string leakNote();

} // namespace rf::uicapture
