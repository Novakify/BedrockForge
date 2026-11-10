// Renderer Foundation - World FX, milestone M0 "world probe" v2 (plan: docs/WORLD_FX.md in Bedrock Forge).
// World FX = never-seen-before world looks for Hive Bed Wars / Sky Wars (art-style worlds, refraction glass, backdrop
// scenery, match-reactive moments ...), drawn client-side inside the game's own frame like Glass Armor. M0 answers,
// from ONE frame:
//   * which pipelines draw the world's blocks (RenderChunk family, found by pixel OR vertex shader name so a
//     shaderpack that replaces the pixel shaders does not hide them): vertex shader output signature (what a
//     replacement pixel shader has to accept), targets, blend, depth, draw counts
//   * the ORDER the frame is drawn in (timeline of runs per command list) and the world's colour + depth at each
//     stage: before the first draw of the families that follow the terrain (the hand pass and the game's own blit
//     always, a few others), and right before the first UI draw - where is the world complete, and is its depth
//     still there? (cel outlines, ambient occlusion, refraction, backdrop compositing all need exactly that)
//   * which texture is the terrain atlas and where the World FX tag test pack's stamps sit in it. The atlas is
//     searched in memory; its PICTURE is only written when the F6 box is ticked (single-player tests) - on a server
//     the atlas holds the server pack's textures, which are not ours to save
// F6 > World FX > "Probe the next frame". Output: <mod>/logs/worldfx/<time>/ report.txt, *.raw + *.json headers,
// *.bmp previews, stamps.json. Bedrock Forge's tools/WorldFx/analyze_probe.py turns a probe folder into the verdict.
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
/// The World FX screen pass wants every list's colour / depth target handles (cheap: one map write per bind).
void setTargetsWanted(bool on);
bool targetsWanted();
bool listTargets(ID3D12GraphicsCommandList* list, size_t& rt0, size_t& dsv);
/// Before every game draw while recording.
void onDraw(ID3D12GraphicsCommandList* list, unsigned count, unsigned instances);
/// Once per Present (UiCapture::beforeOverlay).
void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue);
/// F6 section.
void drawUi();

} // namespace rf::worldfx
