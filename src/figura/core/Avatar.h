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
    std::filesystem::path                      dataDir;    // file API root (host sets it; empty = file API disabled)
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
    // bone solve: Bedrock bones form the COMPILED hierarchy, scripts can move/copy parts anywhere. Each frame every
    // part records its model-space matrix on its bone; bones are then solved parents-first as
    // local = inverse(world of the compiled parent bone) * model matrix (the identity case = the part's own matrix).
    std::vector<int>                           boneParent;   // compiled parent bone, -1 = root
    std::vector<Mat4>                          boneModel, boneWorld;
    std::vector<char>                          boneClaimed;
    std::map<int, std::vector<int>>            dupSlots;     // source bone -> free-floating dup bones (rig "dups")
    int                                        compiledParts = 0;
    void solveBones();
    bool separateType(ParentType t) const;
    int                                        nbtRef = 0; // Lua table: avatar:getNBT()
    std::shared_ptr<lyrics::json::Value>       nbtJson;    // the compiled avatar NBT
    bool                                       scriptError = false;
    bool                                       loaded = false, userInit = false;
    std::string                                lastError;
    AnimClock                                  clock;
    float                                      tickDelta = 0;
    float                                      cameraYaw = 0, cameraPitch = 0; // camera look (Java degrees), for Camera parts
    bool                                       cameraSet = false;
    std::map<std::string, double>              tuning;     // host-provided knobs (config/figura/tuning.json)
    double tune(char const* k, double d) const { auto it = tuning.find(k); return it == tuning.end() ? d : it->second; }
    std::string                                renderMode = "RENDER";
    // vanilla_model: visibility per vanilla part name ("HEAD", "PLAYER" ...), unset = visible
    std::map<std::string, bool>                vanillaVisible;
    std::map<std::string, std::string>         renderer;   // renderer API simple settings (as text)
    float                                      shadowRadius = -1;
    // ---- render settings (RendererAPI) ----
    // Written by LuaApiEntity.cpp (renderer / vanilla_model / nameplate APIs); the host reads them to drive Bedrock
    // rendering. Unset optionals = Figura default (no override).
    struct RenderSettings {
        std::optional<float> shadowRadius, fov;
        bool                 renderFire = true, renderVehicle = true, renderCrosshair = true, forcePaperdoll = false,
                             renderHUD = true;
        std::optional<Vec3>  cameraPos, cameraPivot, cameraOffsetPivot, cameraRot, cameraOffsetRot, outlineColor, eyeOffset;
        std::optional<Mat4>  cameraMatrix;       // setCameraMatrix (when given a Matrix4)
        int                  cameraMatrixRef = 0, cameraNormalRef = 0; // Lua registry refs of the values as set
        std::string          postEffect;         // "shaders/post/<x>.json" resource path, empty = none
        std::optional<Vec2>  crosshairOffset;
        std::string          primaryFireTexture, secondaryFireTexture;
        std::optional<bool>  renderLeftArm, renderRightArm, upsideDown, rootRotation;
        std::optional<Vec4>  blockOutlineColor;
    } render;
    struct VanillaOverride { // vanilla_model.<PART> settings (VanillaPart): unset = vanilla
        std::optional<bool> visible;
        std::optional<Vec3> pos, rot, scale, offsetRot, offsetScale;
    };
    std::map<std::string, VanillaOverride> vanillaOverrides; // by part name ("HEAD", "RIGHT_ITEM" ...), leaf parts only
    struct NameplateSettings { // nameplate.CHAT / ENTITY / LIST
        std::optional<std::string> chatText, entityText, listText; // raw text (may be JSON)
        bool                       entityVisible = true, shadow = false, outline = false;
        std::optional<Vec3>        pivot, pos, scale;
        std::optional<uint32_t>    background, outlineColor;  // ARGB / RGB
        std::optional<int>         light;                     // packed block | sky << 16
    } nameplate;
    // ---- end render settings ----
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
    void traverse(Part* p, TextureOverride prim, TextureOverride sec, bool parentVisible, Mat4 const& parentMat, bool separate = false);
    void  applyCameraTransform(Part* p, Mat4 const& parentMat);
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
