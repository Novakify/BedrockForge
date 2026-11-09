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
#include <string>
#include <vector>

struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_RESOURCE_BARRIER;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12RootSignature;

namespace rf::glassarmor {

void init(std::filesystem::path const& outDir, std::filesystem::path const& configDir);
/// At mod load, before the game makes its device: hook D3D12CreateDevice so the device's CreateRootSignature is
/// hooked from the very first root signature (the game builds its Actor signature at startup).
void installEarly();

// ---- M1a: glass armor on the game's own armor draws (F6 > Glass Armor > "Glass armor on") ----
bool enabled();
/// enabled() or a probe capture is armed: UiCapture tracks list PSOs, root tables, SRV copies and barriers for us.
bool tracking();
void noteBarriers(unsigned n, D3D12_RESOURCE_BARRIER const* b);
/// Called before every indexed draw while enabled: our glass PSO if this is an armor draw (game PSO in *game), else null.
ID3D12PipelineState* glassPsoFor(ID3D12GraphicsCommandList* list, ID3D12PipelineState** game);
/// UiCapture's CreateGraphicsPipelineState hook, after a successful create.
void notePso(ID3D12PipelineState* pso, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& d, uint64_t psHash);
/// true while a frame is being recorded (cheap, lock-free) - the command list hooks only call in then.
bool capturing();
/// M0b: while a capture is armed/recording, UiCapture tracks root tables + SRV creation for us.
bool wantRoot();
bool wantSrvs();
void noteSrv(size_t cpuHandle, ID3D12Resource* res);
/// CopyDescriptors* of CBV/SRV/UAV descriptors while a capture is armed: dst now views what src viewed.
void copySrv(size_t dst, size_t src);
void onSetPso(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso);
void onDraw(ID3D12GraphicsCommandList* list, unsigned vertsOrIndices, unsigned instances);
/// Once per Present (UiCapture::beforeOverlay).
void onPresent(ID3D12Device* device, ID3D12CommandQueue* queue);

// ---- M1b: deferred armor draws (UiCapture replays them after the actor pass) ----
bool                 isActorPso(ID3D12PipelineState* pso);
ID3D12PipelineState* listPso(ID3D12GraphicsCommandList* list);
void                 noteDsv(size_t handle, ID3D12Resource* res);
ID3D12Resource*      dsvResource(size_t handle);
void                 noteRootSig(ID3D12RootSignature* rs, void const* blob, size_t size);
/// The game's root signature + root SRVs scene/depth (t0/t1 space9) + root CBV (b0 space9) + root SRV last frame (t2), cached.
ID3D12RootSignature* extendedRoot(ID3D12RootSignature* game, unsigned* firstExtra);
struct FlushRes { unsigned long long scene = 0, depth = 0, params = 0, prev = 0; int slot = -1; };
/// Copies the bound colour + depth targets into buffers the glass shader reads; false = can't this time.
bool beginFlush(ID3D12GraphicsCommandList* list, ID3D12Resource* rt, ID3D12Resource* ds, FlushRes& out);
void endFlush(ID3D12GraphicsCommandList* list, FlushRes const& fr);

// ---- M2: LyricsForge-reactive glass ----
/// Overlay present, right after the lyric glyphs / sky pass, before ImGui: copy the finished frame (lyrics included)
/// so the glass can reflect it next frame. bb is in RENDER_TARGET state and stays so.
void afterLyrics(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb);
/// F6 section.
void drawUi();

// ---- shared with World FX (rf::worldfx, WorldFx.h): read-only views of what the probe trackers know ----
struct PsoView {
    uint64_t    ps = 0, vs = 0;
    std::string vsOut, inputs, targets;
    void*       root = nullptr;
};
bool psoView(ID3D12PipelineState* pso, PsoView& out);
/// "RenderChunk.material.bin Transparent Fragment Direct3D_SM65", or "?" until the names are learned / when the shader
/// is not one of the game's. Shaders of materials RF served from a pack end in " [pack]" / " [shaderpack]".
std::string shaderName(uint64_t hash);
/// MaterialLoader served a replaced .material.bin (resource pack / shaderpack): name its shaders too, so a pipeline
/// whose shaders were replaced (LyricsLight replaces RenderChunk's pixel AND vertex shaders) can still be told apart.
void learnServedMaterial(std::filesystem::path const& file, std::wstring_view fileName, char const* source);
void        learnShaderNames(); // on a worker thread, once
bool        shaderNamesReady();
struct SrvView {
    ID3D12Resource* res = nullptr; // not owned
    unsigned        w = 0, h = 0, fmt = 0, mips = 0;
};
bool srvView(size_t cpuHandle, SrvView& out);
/// The biggest textures that ever got an SRV (by area, one entry per resource) - report only, may be stale.
std::vector<SrvView> largestTextures(size_t n);
/// Last transition StateAfter seen for a resource (barriers are noted while tracking()), else `fallback`.
unsigned stateOf(ID3D12Resource* r, unsigned fallback);

} // namespace rf::glassarmor
