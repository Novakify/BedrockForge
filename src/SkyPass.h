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
#include <string>
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
                  VvSky = 4,
                  Terrain = 5 }; // RenderChunk (vanilla or a Bedrock Forge pack): marks that the world is being drawn // Vibrant Visuals: StarsForwardPBR / SunMoonForwardPBR main-view draws (sky drawn there)
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
void noteTerrainDraw();
float novakMatchK();   // Novak Glass on: how far the match is (0 the start .. 1 the endgame, from the living sky); -1 off                // a block draw went into the world (lock-free)
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
    void (*setRts)(ID3D12GraphicsCommandList*, size_t rt, size_t ds) = nullptr; // OMSetRenderTargets, one target (unhooked)
};
/// Called right after the game drew its sky (kind Dome) on `list`: draws the live dome + sky quads over it and
/// restores the game's pipeline / topology / vertex buffers. Returns false when it could not (state unknown etc.).
bool afterGameSky(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
/// Called at Bedrock's cloud draw (after the opaque terrain, depth bound): weather particles, depth-tested.
bool fxFallbackWanted(ID3D12GraphicsCommandList* list, size_t dsv);
void noteTargetsBound(size_t rt0, size_t dsv);
bool worldClearColour(size_t rtv, float const* game, float out[4]); // the world target's clear: the atmosphere's colour below the horizon // every OMSetRenderTargets the game makes (for the looking-down fallback) // looking down (no cloud draw): true at the frame's second clear of the world's depth buffer
bool fallbackWorldFx(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls, size_t dsvClear);
bool afterGameClouds(ID3D12GraphicsCommandList* list, GameState const& st, Calls const& calls);
void noteHidden(); // a vanilla sun / moon / stars / clouds draw was skipped

// ---- World FX standalone sky (no LyricsForge needed): drawn through the same stage-2 hooks.
// 0 = off, 1 = Cloud Sea (shaders/cloud_sea.hlsl). Wins over a LyricsForge sky while on.
void        setWorldSky(int kind);
int         worldSky();
void        setWorldSkyDeck(float offsetBlocks); // cloud deck height, relative to the automatic one
float       worldSkyDeck();
std::string worldSkyStatus();
/// World FX screen pass (shaders/worldfx_pass.hlsl) at Bedrock's cloud draw while the Cloud Sea is on:
/// sun shadows from the depth buffer, soft occlusion, the warm rim. Strengths 0..1; debug 0 off, 1 shadows, 2 occlusion.
struct WorldFxLook {
    bool shadows = true; float shadow = 0.70f; float ao = 0.55f; float rim = 1.0f; int debug = 0; float rays = 1.0f; float clouds = 1.0f; float ink = 0.0f; float glass = 0.0f; /* Novak Glass rim (0 off) */
    // the match (Bedrock Forge MatchFx): living sky, team lights + bed beacons, block shards. phase < 0 = follow the match
    bool living = true; float teams = 1.0f; float shards = 1.0f; float phase = -1.0f; float waves = 1.0f; float trails = 1.0f; bool beds = true; bool kills = true; int bedStyle = 0; int deathScene = -1; float killSfx = 0.8f; /* death scene sounds (Finishers / Sable Ragdolls oggs) 0..1 */ float tension = -1.0f; float skyRate = 2.0f; /* Novak: the sky + city are shaded coarser than the blocks: 0 full, 1 = 2x1, 2 = 2x2 */ float slow = 1.0f; float air = 1.0f; bool vitals = true; /* your health: heartbeat edges; your kill run: warmth */ /* living world: sea light from below, pollen / fireflies / stardust */ /* review: block / kill / win moments play this much slower (1 = real time) */ /* < 0: follow the match */ /* -1 shuffle, 0..13 one scene */ // 0 Novak bed, 1 your Figura sitting on it
};
/// what RF last read from Bedrock Forge's match bridge (for the F6 panel)
void testKill();
void testImpact();  // F6: a critical hit's impact frame on whatever stands 3 blocks ahead
void testVictory(); // F6: the fireworks show
void testVitals();  // F6: 10 s of falling health and a kill run (the look only - the heartbeat sound is Bedrock Forge's)
void testBed();  // F6: a test bed in front of you that stands, is attacked and breaks (26 s)
std::string matchStatus();
WorldFxLook& worldFxLook();
std::string  worldFxStatus();
float        worldFxGpuMs();        // GPU time of the World FX pass (smoothed), 0 until measured
bool         worldFxBenchRunning(); // the A/B bench (config/wfxbench.txt) is running
void         loadWorldFxSettings(std::filesystem::path const& configDir);
void         saveWorldFxSettings();
bool         setAtmosphere(int which);
void         toggleAtmosphere();         // Ctrl+F6: flips the atmosphere and shows a note on screen for a few seconds
bool         atmosphereNote(std::string& text, float& alpha);   // that note, while it shows   // 0 Cloud Sea, 1 Novak Glass: RF look + Bedrock Forge's block shaderpack

} // namespace rf::sky
