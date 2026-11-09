// Renderer Foundation - World FX, milestone M0 "world probe" (plan: docs/WORLD_FX.md in Bedrock Forge).
// World FX = never-seen-before world looks for Hive Bed Wars / Sky Wars (cel outlines, refraction glass, void lens,
// heat haze ...), drawn client-side inside the game's own frame like Glass Armor. M0 answers, from ONE frame:
//   * which pipelines draw the world's blocks (RenderChunk family): pixel / vertex shader names, the vertex shader
//     output signature (what a replacement pixel shader has to accept), targets, blend, depth, draw counts
//   * which texture is the terrain atlas (size, format, mips, array) - read back, saved and searched for the
//     World FX tag test pack's fingerprints (where each tagged texture sits in the atlas)
//   * the world -> UI boundary: the colour target the world ends up in and the depth buffer it was drawn with,
//     copied right before the frame's first UI draw (cel outlines / refraction need exactly these)
// F6 > World FX > "Probe the next frame". Output: <mod>/logs/worldfx/<time>/ report.txt, *.raw + *.json headers,
// *.bmp previews. Bedrock Forge's tools/WorldFx/analyze_probe.py turns a probe folder into the M1/M2 verdict.
// Inert until a probe is asked for: it rides on the hooks UiCapture / Glass Armor install ("uiCaptureSpike": true).
#pragma once
#include <cstddef>
#include <filesystem>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12PipelineState;

namespace rf::worldfx {

void init(std::filesystem::path const& outDir);
/// Warm-up or recording frame of a probe: Glass Armor's trackers (root tables, SRV copies, barriers) run meanwhile.
bool tracking();
/// The probe frame is being recorded (cheap, lock-free).
bool recording();
void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso);
void onSetRts(ID3D12GraphicsCommandList* list, size_t rt0, size_t dsv);
/// Before every game draw while recording.
void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned instances);
/// Once per Present (UiCapture::beforeOverlay).
void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue);
/// F6 section.
void drawUi();

} // namespace rf::worldfx
