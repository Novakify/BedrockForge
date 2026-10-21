#pragma once
// Bedrock Forge client - module framework (v1)
//
// "Everything is a module": every client feature is a Module with a category, an on/off state, an optional
// toggle key and typed settings. The registry persists all of it to mods/BedrockForgeCore/config/modules.json
// and the ClickGUI (Right Shift) lists it. Modules marked `planned` are on the roadmap (see
// docs/REFERENCE_CLIENT_ANALYSIS.md) and show greyed out in the menu so the blueprint is visible in game.

#include <functional>
#include <memory>
#include <string>
#include <vector>

class MinecraftUIRenderContext;
class ScreenView;
class IClientInstance;
class LocalPlayer;

namespace bedrock_forge::client {

enum class Category { Hud, Combat, Camera, Visual, Movement, World, Misc };
inline constexpr Category kCategories[] = {Category::Hud, Category::Combat, Category::Camera, Category::Visual,
                                           Category::Movement, Category::World, Category::Misc};
char const* categoryName(Category c);

struct Setting {
    enum class Kind { Toggle, Slider } kind = Kind::Toggle;
    std::string id, label;
    bool        on    = false;
    float       value = 0, min = 0, max = 1, step = 0.1f;
};

/// Everything a module's HUD callback may draw with.
struct Frame {
    MinecraftUIRenderContext& ctx;
    ScreenView&               view;
    IClientInstance&          client;
    LocalPlayer*              player; // null on menus / while loading
    float                     width, height;
    std::vector<std::string>& infoLines; // shared top-left info panel
};

struct Module {
    std::string id, name, description;
    Category    category = Category::Misc;
    bool        enabled  = false;
    bool        planned  = false; // roadmap entry, not implemented yet
    int         key      = 0;     // Windows VK toggle key, 0 = none
    bool        favorite = false; // pinned to the top of its list (middle click in the menu)
    std::vector<Setting>          settings;
    std::function<void(Frame&)>   hud;      // called every HUD frame while enabled
    std::function<void(bool)>     onToggle; // called after enabled changes

    Setting*    setting(std::string const& sid);
    float       num(std::string const& sid, float fallback = 0) { auto* s = setting(sid); return s ? s->value : fallback; }
    bool        flag(std::string const& sid, bool fallback = false) { auto* s = setting(sid); return s ? s->on : fallback; }
    Module&     toggleSetting(std::string sid, std::string label, bool on);
    Module&     slider(std::string sid, std::string label, float value, float min, float max, float step);
};

class Registry {
public:
    static Registry& get();
    Module& add(std::string id, std::string name, Category cat, std::string description, bool enabledByDefault = false);
    Module& plan(std::string id, std::string name, Category cat, std::string description);
    Module* find(std::string const& id);
    std::vector<std::unique_ptr<Module>>& all() { return mModules; }
    void setEnabled(Module& m, bool on);
    bool on(std::string const& id) { auto* m = find(id); return m && m->enabled && !m->planned; }

    void load(std::string const& path);
    void save() const;

private:
    std::vector<std::unique_ptr<Module>> mModules;
    std::string                          mPath;
};

/// Register every built-in module (Modules.cpp).
void registerBuiltins();
/// Install event listeners + hooks that drive the modules (Modules.cpp). Returns listener count added.
void installRuntime();
void uninstallRuntime();

} // namespace bedrock_forge::client
