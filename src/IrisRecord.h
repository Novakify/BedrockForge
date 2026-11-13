// Renderer Foundation - IrisBridge M2 recording API (D3D12 types; include after <d3d12.h>). See IrisBridge.h.
#pragma once
#include <utility>
#include <vector>

#include "IrisBridge.h"

namespace rf::iris {

// ---- M2 terrain pass: Bedrock's RenderChunk draws are recorded during the frame (UiCapture) and replayed at the hook
// through the pack's gbuffers_terrain (Bedrock variant, tools/IrisBridge BEDROCK_VS_HEAD) into its colortex targets.
struct TerrainDraw {
    ID3D12PipelineState*  pso = nullptr;
    ID3D12RootSignature*  sig = nullptr;
    D3D12_INDEX_BUFFER_VIEW  ib{};
    UINT                     nVb = 0;
    D3D12_VERTEX_BUFFER_VIEW vb[8]{};
    UINT idxCount = 0, inst = 0, firstIdx = 0, firstInst = 0;
    INT  baseVtx = 0;
    std::vector<std::pair<unsigned, unsigned long long>> cbvs;   // root index -> GPU VA
    std::vector<std::pair<unsigned, unsigned long long>> tables; // root index -> GPU descriptor handle
};
void recordTerrain(ID3D12GraphicsCommandList* list, TerrainDraw&& d);
/// Inline terrain: draw this chunk through the pack in place of the game's draw. 0 = untouched (draw vanilla),
/// 1 = game state changed but not drawn (restore, then draw vanilla), 2 = drawn (restore, skip vanilla).
int drawTerrainInline(ID3D12GraphicsCommandList* list, TerrainDraw const& d, size_t gameRtv, ID3D12Resource* gameRt, size_t gameDsv,
                      rf::sky::Calls const& calls);
/// A RenderChunk pipeline was created: its input layout + root signature.
void noteTerrainPso(ID3D12PipelineState* pso, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& d);
/// Any root signature was created: where its vertex b0 CBV and pixel t0 table are.
void noteRootSig(ID3D12RootSignature* rs, void const* blob, size_t size);

} // namespace rf::iris
