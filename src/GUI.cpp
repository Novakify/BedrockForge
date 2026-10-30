// Renderer Foundation - overlay windows (modified from BetterRenderDragon's GUI.cpp; see Notice.h).
// Changes: 1.26 sections (Shaders with shaderpack picker + resource-pack shaders + reload, Graphics with Vibrant
// Visuals), status/diagnostics window, options autosave kept from upstream, About credits the upstream project.
#include "GUI.h"
#include "SpatialConfigScreen.h"

#include <string>
#include <vector>

#include <imgui.h>

#include "ll/api/thread/ClientThreadExecutor.h"

#include "Graphics.h"
#include "MaterialLoader.h"
#include "Notice.h"
#include "Options.h"
#include "Overlay.h"
#include "PostFx.h"
#include "LyricsPass.h"
#include "SpatialPanel.h"
#include "UiCapture.h"

namespace {
void runOnClientThread() {
    ll::thread::ClientThreadExecutor::getDefault().execute([] { rf::graphics::tick(); });
}

void updateOptions() {
    static float saveTimer = 0.0f;
    if (RfOptions::isDirty()) saveTimer = 3.0f; // upstream: save 3 s after the last change
    RfOptions::record();
    if (saveTimer > 0.0f) {
        saveTimer -= ImGui::GetIO().DeltaTime;
        if (saveTimer <= 0.0f) RfOptions::save();
    }
}

void shadersSection() {
    if (!RfOptions::materialBinLoaderEnabled || !ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    ImGui::Checkbox("Load shaders from resource packs", RfOptions::redirectShaders.ptr());
    ImGui::SetItemTooltip("renderer/materials/*.material.bin inside your active resource packs replace the game's shaders.\n"
                          "Converted Java packs that ship Bedrock shaders work this way.");

    static std::vector<std::string> packs = rf::materials::listShaderpacks();
    std::string current = RfOptions::shaderPack.get().empty() ? "(none)" : RfOptions::shaderPack.get();
    if (ImGui::BeginCombo("Shaderpack", current.c_str())) {
        if (ImGui::Selectable("(none)", RfOptions::shaderPack.get().empty())) {
            RfOptions::shaderPack = std::string{};
            rf::materials::setShaderpack("");
        }
        for (auto& p : packs)
            if (ImGui::Selectable(p.c_str(), p == RfOptions::shaderPack.get())) {
                RfOptions::shaderPack = p;
                rf::materials::setShaderpack(p);
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) packs = rf::materials::listShaderpacks();

    if (RfOptions::reloadShadersAvailable) {
        bool busy = RfOptions::reloadShaders;
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Reload shaders")) {
            RfOptions::reloadShaders = true;
            runOnClientThread();
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(experimental on 1.26)");
    }
    ImGui::Checkbox("Reload automatically when resource packs change", RfOptions::autoReloadOnPackChange.ptr());

    auto s = rf::materials::stats();
    ImGui::Text("Materials opened: %ld   from packs: %ld   from shaderpack: %ld", s.seen, s.fromPacks, s.fromShaderpack);
    ImGui::Unindent();
}

void graphicsSection() {
    if (!ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    if (ImGui::Checkbox("Vibrant Visuals", RfOptions::vibrantVisualsEnabled.ptr())) {
        RfOptions::applyGraphicsMode = true;
        runOnClientThread();
    }
    if (!RfOptions::vibrantVisualsAllowed) {
        ImGui::SameLine();
        ImGui::TextDisabled("(not allowed on this device/server)");
    }
    ImGui::Text("Status: %s", rf::graphics::status().c_str());
    ImGui::Unindent();
}
} // namespace

void initializeImGui() {
    ImGui::StyleColorsDark();
    auto& st          = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding  = 4.0f;
}

void updateImGui() {
    static bool showAbout = false, showLog = false;
    static int  frame     = 0;
    if (++frame % 120 == 0) runOnClientThread(); // refresh "Vibrant Visuals allowed" and pending requests
    if (RfOptions::reloadShaders) runOnClientThread();

    updateOptions();
    ImGui::NewFrame();
    rf::panel::drawOverlay();
    rf::sgconfig::draw();
    if (RfOptions::showImGui) {
        auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(RF_NAME, RfOptions::showImGui.ptr(), ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Performance", nullptr, RfOptions::performanceEnabled.ptr());
                    ImGui::MenuItem("Shaders", nullptr, RfOptions::materialBinLoaderEnabled.ptr());
                    ImGui::MenuItem("Recently served materials", nullptr, &showLog);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About")) showAbout = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Text("%s %s  |  MC %s  |  F6 to hide", RF_NAME, RF_VERSION, RF_TARGET_VERSION);
            ImGui::TextDisabled("%s  %s", rf::overlay::rendererType().c_str(), rf::overlay::gpuName().c_str());
            ImGui::Separator();

            if (RfOptions::performanceEnabled && ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                static float fps = io.Framerate, dt = io.DeltaTime, timer = 0.5f;
                if ((timer -= io.DeltaTime) <= 0) {
                    fps   = io.Framerate;
                    dt    = io.DeltaTime;
                    timer = 0.5f;
                }
                ImGui::Indent();
                ImGui::Text("FPS: %.1f   Frame time: %.1f ms", fps, dt * 1000.0f);
                ImGui::Unindent();
            }
            shadersSection();
            graphicsSection();
            rf::postfx::drawUi();
            rf::uicapture::drawUi();
            rf::panel::drawUi();
            rf::lyrics::drawUi();
        }
        ImGui::End();

        if (showLog) {
            if (ImGui::Begin("Renderer Foundation - Served materials", &showLog)) {
                for (auto& l : rf::materials::lastServed()) ImGui::TextUnformatted(l.c_str());
            }
            ImGui::End();
        }
        if (showAbout) {
            if (ImGui::Begin("Renderer Foundation - About", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%s %s", RF_NAME, RF_VERSION);
                ImGui::Text("A modernized fork of %s", RF_BASED_ON);
                ImGui::Text("Original work (c) ddf8196 and the BetterRenderDragon contributors");
                ImGui::TextUnformatted(RF_UPSTREAM_URL);
                ImGui::Text("Licensed under the GNU AGPL v3 - see LICENSE. Source must stay available.");
            }
            ImGui::End();
        }
    }
    ImGui::EndFrame();
}
