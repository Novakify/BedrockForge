// FiguraBridge model: FiguraModelPart + PartCustomization (Figura 0.1.6 org.figuramc.figura.model), ported.
// One Part per Figura model part (groups, cubes and meshes are all parts). The Bedrock side renders each part as one
// bone of the generated resource pack (tools/FiguraBridge/compile.py); computeOutputs() turns the Figura transform
// state into the per-bone values the pack reads.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "figura/core/FbMath.h"

namespace bf::figura {

class Animation;

enum class PartType : uint8_t { Group, Cube, Mesh };

/// Figura ParentType (order and names identical to org.figuramc.figura.model.ParentType).
enum class ParentType : uint8_t {
    None, Model, Head, Body, LeftArm, RightArm, LeftLeggingPivot, RightLeggingPivot, LeftLeg, RightLeg,
    LeftElytraPivot, RightElytraPivot, LeftElytra, RightElytra, Cape, World, Hud, Gui, Camera, Billboard, Skull,
    Portrait, Arrow, Trident, Item, LeftItemPivot, RightItemPivot, LeftSpyglassPivot, RightSpyglassPivot,
    LeftParrotPivot, RightParrotPivot, HelmetItemPivot, HelmetPivot, ChestplatePivot, ChestplateBodyPivot,
    LeftShoulderPivot, RightShoulderPivot, LeggingsPivot, BeltPivot, LeftBootPivot, RightBootPivot,
    Count
};
char const*  parentTypeName(ParentType t);
ParentType   parentTypeFromName(std::string const& s, bool* ok = nullptr); // ParentType.valueOf (exact name)
ParentType   parentTypeGet(std::string const& groupName);                 // ParentType.get (prefix rule)
bool         parentTypeHasVanilla(ParentType t);                          // provider != null
Vec3         parentTypeOffset(ParentType t);
bool         parentTypeIsPivot(ParentType t);

/// Figura RenderTypes subset names (kept as strings, the Bedrock pack maps them to materials).
enum class TexKind : uint8_t { Default, Custom, Skin, Cape, Elytra, Resource, Primary, Secondary, Specular, Normal };

struct TextureOverride {
    bool        set   = false;
    TexKind     kind  = TexKind::Default;
    int         file  = -1;     // index into Avatar::textureFiles for CUSTOM (and the avatar's own textures)
    std::string resource;       // RESOURCE path (not renderable yet: logged)
    void*       tex   = nullptr; // CUSTOM: the Lua Texture object (LuaTex.cpp Tex), valid while the Lua state lives
};

/// Figura model.rendering.Vertex (floats, like Figura).
struct Vertex {
    float x = 0, y = 0, z = 0, u = 0, v = 0, nx = 0, ny = 0, nz = 0;
};

/// One vanilla model part pose (VanillaModelData.PartData), already in Figura's convention.
struct VanillaPart {
    Vec3 pos;
    Vec3 rot;
    Vec3 scale{1, 1, 1};
    bool visible = true;
};

struct Part {
    // identity / tree
    int                  index  = -1; // pre-order index = Bedrock bone "p<index>"
    std::string          name;
    Part*                parent = nullptr;
    std::vector<Part*>   children; // current children (Lua can move/remove)
    PartType             type      = PartType::Group;
    ParentType           parentType = ParentType::None;
    int                  defaultSet = -1;   // texture set of the part's own faces (first face)
    bool                 removed   = false;

    // PartCustomization
    Vec3   position, rotation, scale{1, 1, 1}, pivot, offsetPivot, offsetPos, offsetRot, offsetScale{1, 1, 1};
    Vec3   animPos, animRot, animScale{1, 1, 1};
    Vec3   stackScale{1, 1, 1};
    Vec3   color{1, 1, 1}, color2{1, 1, 1};
    std::optional<float> alpha;
    std::optional<int>   light, overlay;
    bool   visible = true;
    std::optional<bool> vanillaVisible;
    std::string primaryRenderType, secondaryRenderType; // empty = default
    TextureOverride primaryTexture, secondaryTexture;
    Mat4   positionMatrix;
    bool   customMatrix = false; // setMatrix()
    Vec3   uvOffset;             // setUV (unused by Bedrock yet) = uvMatrix translation
    double uvMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // PartCustomization.uvMatrix (FiguraMat3, row-major)

    // animation bookkeeping (FiguraModelPart)
    int  lastAnimationPriority = INT32_MIN;
    bool animated              = false;
    int  animationOverride     = 0;

    // vanilla transforms (applyVanillaTransforms / resetVanillaTransforms)
    bool hasPlayerCustomization = false;
    Vec3 saveOffsetPivot, saveOffsetPos, saveOffsetRot, saveOffsetScale{1, 1, 1};

    // Lua callbacks (preRender/midRender/postRender), Lua registry refs; 0 = none
    int preRender = 0, midRender = 0, postRender = 0;

    // render tasks by name (text/item/block/sprite/entity) - kept for the API; Bedrock draws none yet
    std::map<std::string, int> tasks;

    // geometry info (FiguraModelPartReader), filled lazily from the avatar NBT by the ModelPart Lua API
    bool                              geomInit = false;
    std::map<int, std::deque<Vertex>> vertices;        // texture set index -> vertices (4 per face)
    std::vector<int>                  textureSets;     // FiguraModelPart.textures (avatar texture set indices)
    int                               textureWidth = -1, textureHeight = -1;

    // ---- Figura behaviour ----
    void recalculate();                    // PartCustomization.recalculate -> positionMatrix
    bool overrideVanillaRot() const { return (animationOverride & 1) == 1; }
    bool overrideVanillaPos() const { return (animationOverride & 2) == 2; }
    bool overrideVanillaScale() const { return (animationOverride & 4) == 4; }
    void applyVanillaTransforms(VanillaPart const* data);
    void resetVanillaTransforms();
    void animPosition(Vec3 v, bool merge);
    void animRotation(Vec3 v, bool merge);
    void animScaleSet(Vec3 v, bool merge);
    bool isChildOf(Part const* p) const;
    std::string path() const;              // "models.model.Body"
    Part*       child(std::string const& n) const;
    bool        effectiveVisible() const;  // visible && every ancestor visible (and vanilla visibility)
};

} // namespace bf::figura
