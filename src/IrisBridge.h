// Renderer Foundation - IrisBridge runtime, milestone M1 "post chain" (plan: Bedrock Forge docs/ARKE_STUDY.md).
// Original code for Renderer Foundation / Bedrock Forge (Novak). Runs a Java Iris / OptiFine shaderpack, translated
// offline by Bedrock Forge's tools/IrisBridge (GLSL -> SPIR-V -> HLSL -> signed DXIL + runtime.txt), inside the game's
// own frame:
//   * at Bedrock's cloud draw (the opaque world + mobs are done, depth intact) the world colour is imported into the
//     pack's colortex0 (linear) and the depth copied into depthtex0/1
//   * the pack's deferred*, composite* and final programs run in order with Iris semantics: colortexN ping-pong
//     (a pass reads the old buffer while writing the new), RENDERTARGETS/DRAWBUFFERS, buffer formats and clears from
//     the pack, its custom uniforms (shaders.properties expressions, UniformVM), Iris built-in uniforms from the game
//     camera; final writes straight into the game's colour target, then the game carries on (water, hand, HUD).
// M1 limits (said openly): no gbuffers / shadow pass yet, so a pack's normals / material masks / shadow maps read as
// empty (shadows = fully lit); colortex sizes are full screen; Fancy (classic) renderer only.
// Bundles live in <mod>/irisbridge/<name>/ (runtime.txt + world*/ *.dxil). F6 > IrisBridge.
#pragma once
#include <cstddef>
#include <filesystem>

#include "SkyPass.h"

struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace rf::iris {

struct FrameTargets {
    ID3D12Resource* rt = nullptr;  // the game's colour target at the cloud draw
    size_t          rtv = 0;       // its RTV (CPU handle)
    ID3D12Resource* ds = nullptr;  // the game's depth (D24S8)
    size_t          dsv = 0;
    unsigned        rtState = 0, dsState = 0; // their current D3D12_RESOURCE_STATES
};

void init(std::filesystem::path const& modDir);
/// A bundle is loaded and switched on (cheap).
bool enabled();
/// Record the pack's post chain. false = nothing recorded (not ready, unsupported target...). Restores the game's
/// pipeline, root state, targets, viewport and topology before returning.
bool run(ID3D12GraphicsCommandList* list, rf::sky::GameState const& st, rf::sky::Calls const& calls, FrameTargets const& t);
/// F6 section.
void drawUi();

/// IrisBridge is on and its pack has a terrain program for the current dimension (cheap). The recording API
/// (D3D12 types) is in IrisRecord.h.
bool wantsTerrain();
void onPresent(); // frame boundary: which of the frame's views gets inline terrain

} // namespace rf::iris
