#pragma once
// Renderer Foundation - full-screen post effects on the D3D12 swapchain image (bloom, chromatic aberration, CAS,
// camera motion blur). Bedrock Forge shaderpacks request them through BF_GetPostFx() (BedrockForgeCore.dll);
// RF reads that every frame, records the passes into the overlay's command list before ImGui, and shows them in F6.
#include <d3d12.h>

namespace rf::postfx {

/// Record the post passes. `bb` is in PRESENT state on entry; when this returns true it left `bb` in RENDER_TARGET
/// with `bbRtv` bound (the caller draws ImGui and transitions back to PRESENT), false = nothing was recorded.
bool record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE bbRtv);
/// After the list that `record` wrote into was executed on `queue`.
void submitted(ID3D12CommandQueue* queue);
/// Free the size-dependent resources (swapchain resize / shutdown). Waits for the GPU to finish with them.
void release();
/// F6 section.
void drawUi();

} // namespace rf::postfx
