// Renderer Foundation - Vibrant Visuals Live: VV's own lighting, reflections and volumetric fog follow RF's sky and
// the match, frame by frame. Original code for Renderer Foundation (by Novak).
//
// How: VV's deferred lighting pass (DeferredShading / DoDeferredShading), its sky-probe pass (SkyProbeDeferredShading,
// the reflections + image-based light the whole world is lit with) and its fog-volume pass (PopulateVolume / Populate)
// read their settings from one constant buffer each, written by the game into a mapped upload buffer just before the
// draw / dispatch is recorded. RF learns those pipelines from the game's own .material.bin files, finds the buffer
// through the root constant-buffer address, checks the layout against bgfx's uniform table (byte offsets below, MC
// 1.26.51) and rewrites a few fields in place before the command is recorded. The game keeps doing all the work.
//
// Resource packs can only key VV on time of day and biome; this keys it on anything RF knows (the match, the camera).
#pragma once
#include <cstdint>
#include <filesystem>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12PipelineState;

namespace rf::vvlive {

/// What the active RF sky wants VV to look like this frame (SkyPass writes it once per frame, the patch reads it).
struct Drive {
    bool  on = false;          // an RF sky that drives VV is active (Cloud Sea)
    float phase = 0.0f;        // the match's phase: 0 golden hour, 1 dusk, 2 moonlit night
    float zen[3]{}, hor[3]{};  // the sky's colour overhead / at the horizon (linear; only the hue is used)
    float key[3]{};            // the sun / moon light's colour (hue)
    float air[3]{};            // the fog's colour (hue)
    float deckTop = 0.0f;      // world y where the cloud deck's fog ends (density 0 above)
    float deckThick = 30.0f;   // blocks over which the fog thickens down to full
    float pulse = 0.0f;        // 0..1: a bed just broke - the air takes that team's colour for a moment
    float pulseCol[3]{};       // that team's colour (linear)
    float closeIn = 0.0f;      // 0..1: the final fight (one bed / team left): the clouds rise and thicken
};

void onGameDevice(ID3D12Device* dev);                                       // first game device: compute + Map hooks
void learn(std::filesystem::path const& materialsDir);                     // at boot: DeferredShading, SkyProbe, Populate
void loadConfig(std::filesystem::path const& configDir);                   // config/VvLive.txt
void onCreatedGraphicsPso(ID3D12PipelineState* pso, uint64_t psHash);      // UiCapture's CreateGraphicsPipelineState hook
void onSetPso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* pso);     // SetPipelineState (graphics and compute)
void onSetGraphicsCbv(ID3D12GraphicsCommandList* l, unsigned i, uint64_t va);
void beforeDraw(ID3D12GraphicsCommandList* l);                             // DrawInstanced / DrawIndexedInstanced
void setDrive(Drive const& d);
void onPresent();                                                          // once per frame
void gui();                                                                // F6 section
float skyGain();                                                           // the Cloud Sea sky's brightness in VV's HDR target
void logStatus();                                                          // one status block into the log (config/Test.txt: vv)
bool active();                                                             // patching right now (status)

} // namespace rf::vvlive
