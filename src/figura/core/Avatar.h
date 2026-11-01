// FiguraBridge avatar runtime: one per loaded Figura avatar. Port of Figura 0.1.6 Avatar + FiguraLuaRuntime +
// the per-frame part traversal of ImmediateAvatarRenderer, minus the drawing: frame() produces per-part Bedrock bone
// values (PartOut) that the host writes into the resource pack's Molang variables.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "figura/core/Anim.h"
#include "figura/core/Host.h"
#include "figura/core/Model.h"
#include "figura/core/Vanilla.h"

struct lua_State;

namespace bf::lyrics::json { class Value; }

namespace bf::figura {

struct TextureFile {
    int         index = -1;
    std::string figuraName;   // TextureAPI key, e.g. "model.Green_e" / "CustomTextures.Green_e"
    int         set   = -1;
    char        kind  = 'd';  // d e n s
    std::string file;         // path relative to the avatar bundle
    int         w = 0, h = 0;
};
struct TextureSet {
    std::string name;
    int         d = -1, e = -1; // TextureFile indices
};

/// Per part (= per Bedrock bone "p<index>"), written to Molang every frame by the host.
struct PartOut {
    BoneOut bone;
    bool    visible = true;
    int     primary = -1;     // TextureFile index forced as primary, -1 = part default
    int     secondary = -1;   // TextureFile index forced as secondary (emissive), -1 = part default
    float   opacity = 1;
    Vec3    color{1, 1, 1};
};

struct KeybindState {
    std::string name, key;
    bool        enabled = true, gui = false, down = false;
    int         ref = 0;      // Lua table (the Keybind object) in the registry
};

class Avatar final : public IAnimScripts {
public:
    explicit Avatar(IHost* host);
    ~Avatar() override;
    Avatar(Avatar const&)            = delete;
    Avatar& operator=(Avatar const&) = delete;

    /// Load a compiled avatar bundle (tools/FiguraBridge/compile.py OUT/avatar + OUT/rig.json).
    bool load(std::filesystem::path const& bundleDir, std::filesystem::path const& rigJson, std::string* err);
    /// 20 Hz game tick: ENTITY_INIT (first), WORLD_TICK, TICK, keybind edges.
    void tick();
    /// Every render frame. delta = partial tick.
    void frame(float delta);
    /// Run a ping as if it came back from the server.
    void runPing(std::string const& name, int argsRef);

    // ---- state (public for the Lua API translation units) ----
    IHost*                                     host;
    PlayerState                                player;
    lua_State*                                 L = nullptr;
    std::string                                name, author, color;
    std::filesystem::path                      dir;
    std::vector<std::unique_ptr<Part>>         parts;      // index = original pre-order index; new parts appended
    Part*                                      root = nullptr;
    std::vector<std::unique_ptr<Animation>>    animations;
    std::vector<TextureFile>                   textureFiles;
    std::vector<TextureSet>                    textureSets;
    std::map<std::string, std::string>         scripts;    // name -> source
    std::map<std::string, std::string>         soundEvents;// figura sound name -> bedrock event
    std::vector<std::string>                   autoScripts;
    std::vector<std::string>                   autoAnims;
    std::vector<PartOut>                       out;        // per part, after frame()
    std::vector<Vec3>                          compiledPivots; // Bedrock bone pivots (part pivots at load)
    int                                        nbtRef = 0; // Lua table: avatar:getNBT()
    std::shared_ptr<lyrics::json::Value>       nbtJson;    // the compiled avatar NBT
    bool                                       scriptError = false;
    bool                                       loaded = false, userInit = false;
    std::string                                lastError;
    AnimClock                                  clock;
    float                                      tickDelta = 0;
    std::string                                renderMode = "RENDER";
    // vanilla_model: visibility per vanilla part name ("HEAD", "PLAYER" ...), unset = visible
    std::map<std::string, bool>                vanillaVisible;
    std::map<std::string, std::string>         renderer;   // renderer API simple settings (as text)
    float                                      shadowRadius = -1;
    std::vector<KeybindState>                  keybinds;
    VanillaPose                                vanillaPose;
    int64_t                                    instructionLimit = 50'000'000;
    int64_t                                    instructionsUsed = 0;
    std::function<void(std::string const&)>    onError;

    // events
    void        callEvent(char const* name, int nargs); // args already pushed; pops them
    Part*       addPart(std::string const& n, Part* parent);
    Animation*  findAnimation(std::string const& mdl, std::string const& n);
    int         fileIndexByFiguraName(std::string const& n) const;
    void        reportError(std::string const& msg);
    bool        runChunk(int nargs, int nresults); // pcall with traceback + limits; false on error (reported)

    // IAnimScripts
    int    compileKeyframe(std::string const& chunk, std::string const& src, bool* isLiteral, double* literal) override;
    double evalKeyframe(int handle, float delta, Animation* anim, std::string const& chunk) override;
    int    compileCode(std::string const& chunk, std::string const& src) override;
    void   runCode(int handle, Animation* anim) override;
    bool   ready() const override { return L && !scriptError && loaded; }

private:
    bool buildFromNbt(lyrics::json::Value const& nbt, lyrics::json::Value const& rig, std::string* err);
    Part* readPart(lyrics::json::Value const& p, Part* parent);
    void  readAnimData(Part* part, lyrics::json::Value const& anim);
    bool  initLua(std::string* err);
    void  initScripts();
    void  traverse(Part* p, TextureOverride prim, TextureOverride sec, bool parentVisible);
    void  applyAnimations();
    void  clearAnimations();
};

/// The Avatar owning a Lua state.
Avatar* avatarOf(lua_State* L);

/// Registers every Figura API into the avatar's Lua state (LuaApi*.cpp).
void openFiguraApis(Avatar& a);
/// Lua-side part of the API (events, pings, action_wheel, keybinds, config ...), see Prelude.cpp.
char const* figuraPreludeSource();

} // namespace bf::figura
