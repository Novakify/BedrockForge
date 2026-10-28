// Renderer Foundation - options (modified from BetterRenderDragon's Options.h; see Notice.h for licence/credits).
// Changes: stored in the mod's config folder (GDK builds have no UWP LocalState), new shader/graphics options,
// pending-action flags are atomics because the overlay (Present) and the client thread both touch them.
#pragma once
#include <atomic>
#include <filesystem>
#include <string>

class IRfOption {
public:
    virtual ~IRfOption()       = default;
    virtual void record()    = 0;
    virtual bool isChanged() = 0;
};

template <typename T>
class RfOption : public IRfOption {
public:
    RfOption() = default;
    RfOption(T const& value) : value(value), prevValue(value) {}
    void     record() override { prevValue = value; }
    bool     isChanged() override { return value != prevValue; }
    T&       get() { return value; }
    T const& get() const { return value; }
    void     set(T const& v) { value = v; }
    T*       ptr() { return &value; }
    operator T() const { return value; }
    RfOption& operator=(T const& v) {
        value = v;
        return *this;
    }

private:
    T value{};
    T prevValue{};
};

namespace RfOptions {
// overlay
extern RfOption<bool> showImGui;
extern RfOption<int>  uiKey;            // Win32 virtual-key code (default F6)
extern RfOption<int>  reloadShadersKey; // 0 = none
extern RfOption<bool> performanceEnabled;

// shaders (MaterialBinLoader in upstream)
extern RfOption<bool>        materialBinLoaderEnabled;
extern RfOption<bool>        redirectShaders;        // material.bin from the active resource packs
extern RfOption<std::string> shaderPack;             // folder under <mod>/shaderpacks, "" = none
extern RfOption<bool>        autoReloadOnPackChange; // experimental
extern bool                reloadShadersAvailable;
extern std::atomic_bool    reloadShaders;          // request, consumed on the client thread

// graphics (replaces upstream Vanilla2Deferred: on 1.26 Vibrant Visuals is a real graphics mode)
extern RfOption<bool>     vibrantVisualsEnabled;
extern bool             vibrantVisualsAllowed;   // refreshed from the game
extern std::atomic_bool applyGraphicsMode;       // request, consumed on the client thread

// Spatial GUI M0 capture spike (UiCapture.h): off = its hooks are not installed at all
extern RfOption<bool> uiCaptureSpike;
extern RfOption<int>  uiCaptureKey; // Win32 virtual-key code (default F7)

extern std::filesystem::path optionsFile;

bool init(std::filesystem::path const& configDir);
bool load();
bool save();
void record();
bool isDirty();
} // namespace RfOptions
