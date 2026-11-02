// Renderer Foundation - LyricsFlow port: live sky (BF_CopySkyFrame, BfLyrics.h) drawn with shaders/sky.hlsl.
// Original code for Renderer Foundation.
//
// STAGE 1 (this file): a PREVIEW at Present - F6 > "LyricsFlow sky (preview)" draws the dome / picture plus the
// star, lightning and weather quads into a window in the corner (or full screen), from BF's camera. It proves the
// BF -> RF data, the shaders and the images in game without touching the game's frame.
// STAGE 2 (needs the PC): draw the same thing where Bedrock draws its sky (tag the Sky material's pipelines like
// UiCapture tags UI ones, record the dome right after the game's sky draw, restore the game's state).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D12PipelineState;
struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_VERTEX_BUFFER_VIEW;

namespace rf::sky {

void record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* bb, D3D12_CPU_DESCRIPTOR_HANDLE const& bbRtv, unsigned slot);
void drawUi(); // F6 section

// ---- stage 2: in place of Bedrock's sky (driven by UiCapture's hooks, needs "uiCaptureSpike": true)
// Bedrock 1.26.51 sky materials, by the FNV-1a 64 of their D3D12 pixel shaders (SM60 + SM65, from the game's
// Sky / Stars / SunMoon / Clouds .material.bin; see Bedrock Forge docs/lyricsflow/KICKOFF.md).
enum Kind : int { None = 0, Dome = 1, Hide = 2, Clouds = 3,
                  VvSky = 4 }; // Vibrant Visuals: StarsForwardPBR / SunMoonForwardPBR main-view draws (sky drawn there)
/// Vibrant Visuals has no sky draw to follow (DeferredShading lights the sky pixels): RF draws the live sky at the
/// first vanilla star / sun / moon draw of the frame, depth-tested so only pixels with nothing in front change.
bool claimVvFrame(); // true once per Present (the first VvSky draw of the frame gets our dome)
bool vvWanted();     // "Vibrant Visuals too" is on (F6)
int  kindOfHash(uint64_t psHash); // learned from the game's own sky materials first, then the 1.26.51 table
/// Learn the sky pixel-shader hashes from the materials the game really loads (SkyMaterialScan.h), so a game update
/// or a pack / shaderpack that replaces a sky material keeps working without a new RF build.
void learnGameMaterials(std::filesystem::path const& materialsDir);                                // at boot
void learnMaterialFile(std::filesystem::path const& file, std::wstring_view fileName, char const* source); // pack-served
/// A game pipeline with a sky pixel shader was created (desc = the game's, root signature kept for our pipelines).
void notePso(ID3D12PipelineState* pso, int kind, D3D12_GRAPHICS_PIPELINE_STATE_DESC const& desc);
int  kindOf(ID3D12PipelineState* pso); // any thread
bool wanted();                         // a LyricsFlow sky is active: UiCapture tracks list state + calls us
bool wantRoot();                       // "My picture" is on: also track the graphics root state
bool hideClouds();
struct GameState {
    ID3D12PipelineState*     pso = nullptr;
    bool                     haveTopo = false;
    int                      topo = 0; // D3D12_PRIMITIVE_TOPOLOGY
    unsigned                 nVb = 0;  // 0 = unknown (not tracked since the list was reset)
    D3D12_VERTEX_BUFFER_VIEW const* vb = nullptr;
    bool                     rootKnown = false; // the game's heaps + root signature + root args are tracked
};
struct Calls { // the ORIGINAL list functions (no hooks, so the tracked game state stays the game's)
    void (*setPso)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
    void (*setTopo)(ID3D12GraphicsCommandList*, int);
    void (*setVb)(ID3D12GraphicsCommandList*, unsigned, unsigned, D3D12_VERTEX_BUFFER_VIEW const*);
    void (*draw)(ID3D12GraphicsCommandList*, unsigned, unsigned, unsigned, unsigned);
    // own root signature ("My picture"); restoreRoot puts the game's heaps / signature / arguments back
    void (*setHeaps)(ID3D12GraphicsCommandList*, unsigned, ID3D12DescriptorHeap* const*);
    void (*setRootSig)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
    void (*setTable)(ID3D12GraphicsCommandList*, unsigned, unsigned long long);
    void (*setConsts)(ID3D12GraphicsCommandList*, unsigned, unsigned, void const*, unsigned);
    void (*restoreRoot)(ID3D12GraphicsCommandList*);
};
/// Called right after the game drew its sky (kind Dome) on `list`: draws the live dome + sky quads over it and
/// restores the game's pipeline / topology / vertex buffers. Returns false when it could not (state unknown etc.).
bool afterGameSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
/// Called at Bedrock's cloud draw (after the opaque terrain, depth bound): weather particles, depth-tested.
bool afterGameClouds(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
void noteHidden(); // a vanilla sun / moon / stars / clouds draw was skipped

} // namespace rf::sky
