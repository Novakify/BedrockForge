// Renderer Foundation - overlay windows (modified from BetterRenderDragon's GUI.cpp; see Notice.h).
// Changes: 1.26 sections (Shaders with shaderpack picker + resource-pack shaders + reload, Graphics with Vibrant
// Visuals), status/diagnostics window, options autosave kept from upstream, About credits the upstream project.
#include "GUI.h"

#include <string>
#include <vector>

#include <imgui.h>

#include "ll/api/thread/ClientThreadExecutor.h"

#include "Graphics.h"
#include "MaterialLoader.h"
#include "Notice.h"
#include "Options.h"
#include "Overlay.h"

namespace {
void runOnClientThread() {
    ll::thread::ClientThreadExecutor::getDefault().execute([] { rf::graphics::tick(); });
}

void updateOptions() {
    static float saveTimer = 0.0f;
    if (Options::isDirty()) saveTimer = 3.0f; // upstream: save 3 s after the last change
    Options::record();
    if (saveTimer > 0.0f) {
        saveTimer -= ImGui::GetIO().DeltaTime;
        if (saveTimer <= 0.0f) Options::save();
    }
}

void shadersSection() {
    if (!Options::materialBinLoaderEnabled || !ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    ImGui::Checkbox("Load shaders from resource packs", Options::redirectShaders.ptr());
    ImGui::SetItemTooltip("renderer/materials/*.material.bin inside your active resource packs replace the game's shaders.\n"
                          "Converted Java packs that ship Bedrock shaders work this way.");

    static std::vector<std::string> packs = rf::materials::listShaderpacks();
    std::string current = Options::shaderPack.get().empty() ? "(none)" : Options::shaderPack.get();
    if (ImGui::BeginCombo("Shaderpack", current.c_str())) {
        if (ImGui::Selectable("(none)", Options::shaderPack.get().empty())) {
            Options::shaderPack = std::string{};
            rf::materials::setShaderpack("");
        }
        for (auto& p : packs)
            if (ImGui::Selectable(p.c_str(), p == Options::shaderPack.get())) {
                Options::shaderPack = p;
                rf::materials::setShaderpack(p);
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) packs = rf::materials::listShaderpacks();

    if (Options::reloadShadersAvailable) {
        bool busy = Options::reloadShaders;
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Reload shaders")) {
            Options::reloadShaders = true;
            runOnClientThread();
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(experimental on 1.26)");
    }
    ImGui::Checkbox("Reload automatically when resource packs change", Options::autoReloadOnPackChange.ptr());

    auto s = rf::materials::stats();
    ImGui::Text("Materials opened: %ld   from packs: %ld   from shaderpack: %ld", s.seen, s.fromPacks, s.fromShaderpack);
    ImGui::Unindent();
}

void graphicsSection() {
    if (!ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    if (ImGui::Checkbox("Vibrant Visuals", Options::vibrantVisualsEnabled.ptr())) {
        Options::applyGraphicsMode = true;
        runOnClientThread();
    }
    if (!Options::vibrantVisualsAllowed) {
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
    if (Options::reloadShaders) runOnClientThread();

    updateOptions();
    ImGui::NewFrame();
    if (Options::showImGui) {
        auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(RF_NAME, Options::showImGui.ptr(), ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Performance", nullptr, Options::performanceEnabled.ptr());
                    ImGui::MenuItem("Shaders", nullptr, Options::materialBinLoaderEnabled.ptr());
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

            if (Options::performanceEnabled && ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
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
