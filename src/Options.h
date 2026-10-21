// Renderer Foundation - options (modified from BetterRenderDragon's Options.h; see Notice.h for licence/credits).
// Changes: stored in the mod's config folder (GDK builds have no UWP LocalState), new shader/graphics options,
// pending-action flags are atomics because the overlay (Present) and the client thread both touch them.
#pragma once
#include <atomic>
#include <filesystem>
#include <string>

class IOption {
public:
    virtual ~IOption()       = default;
    virtual void record()    = 0;
    virtual bool isChanged() = 0;
};

template <typename T>
class Option : public IOption {
public:
    Option() = default;
    Option(T const& value) : value(value), prevValue(value) {}
    void     record() override { prevValue = value; }
    bool     isChanged() override { return value != prevValue; }
    T&       get() { return value; }
    T const& get() const { return value; }
    void     set(T const& v) { value = v; }
    T*       ptr() { return &value; }
    operator T() const { return value; }
    Option&  operator=(T const& v) {
        value = v;
        return *this;
    }

private:
    T value{};
    T prevValue{};
};

namespace Options {
// overlay
extern Option<bool> showImGui;
extern Option<int>  uiKey;            // Win32 virtual-key code (default F6)
extern Option<int>  reloadShadersKey; // 0 = none
extern Option<bool> performanceEnabled;

// shaders (MaterialBinLoader in upstream)
extern Option<bool>        materialBinLoaderEnabled;
extern Option<bool>        redirectShaders;        // material.bin from the active resource packs
extern Option<std::string> shaderPack;             // folder under <mod>/shaderpacks, "" = none
extern Option<bool>        autoReloadOnPackChange; // experimental
extern bool                reloadShadersAvailable;
extern std::atomic_bool    reloadShaders;          // request, consumed on the client thread

// graphics (replaces upstream Vanilla2Deferred: on 1.26 Vibrant Visuals is a real graphics mode)
extern Option<bool>     vibrantVisualsEnabled;
extern bool             vibrantVisualsAllowed;   // refreshed from the game
extern std::atomic_bool applyGraphicsMode;       // request, consumed on the client thread

extern std::filesystem::path optionsFile;

bool init(std::filesystem::path const& configDir);
bool load();
bool save();
void record();
bool isDirty();
} // namespace Options
