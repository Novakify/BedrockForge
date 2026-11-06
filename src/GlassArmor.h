// Renderer Foundation - Glass Armor, milestone M0 "probe" (plan: docs/GLASS_ARMOR.md in Bedrock Forge).
// Question it answers: which of the game's pipelines draw armor, and what do their vertex shaders hand the pixel
// shader? (glass armor will run its own pixel shader on the game's own armor draw.)
//   * every pipeline the game creates is noted: pixel + vertex shader FNV-1a 64, vertex shader output signature,
//     render target / depth formats, depth test, blend, root signature
//   * F6 > Glass Armor: capture one frame wearing armor ("ON") and one without ("OFF"); the pipelines that only draw
//     in ON are the armor pipelines. Report -> <mod>/logs/glassarmor/<time>.txt
//   * pixel shader hashes are named from the game's .material.bin files (file + pass) so the report says
//     "Entity.material.bin / ..." instead of a bare number
// Rides on UiCapture's hooks (needs "uiCaptureSpike": true, same as Spatial GUI). Inert until a capture is asked for,
// apart from noting pipelines at creation.
#pragma once
#include <cstdint>
#include <filesystem>

struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;

namespace rf::glassarmor {

void init(std::filesystem::path const& outDir);
/// UiCapture's CreateGraphicsPipelineState hook, after a successful create.
void notePso(ID3D12PipelineState* pso, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& d, uint64_t psHash);
/// true while a frame is being recorded (cheap, lock-free) - the command list hooks only call in then.
bool capturing();
/// M0b: while a capture is armed/recording, UiCapture tracks root tables + SRV creation for us.
bool wantRoot();
bool wantSrvs();
void noteSrv(size_t cpuHandle, ID3D12Resource* res);
void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso);
void onDraw(ID3D12GraphicsCommandList* list, unsigned vertsOrIndices, unsigned instances);
/// Once per Present (UiCapture::beforeOverlay).
void onPresent();
/// F6 section.
void drawUi();

} // namespace rf::glassarmor
