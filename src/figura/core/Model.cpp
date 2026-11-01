// FiguraBridge model - port of FiguraModelPart / PartCustomization / ParentType behaviour (Figura 0.1.6).
#include "figura/core/Model.h"

#include <cmath>
#include <cstring>

namespace bf::figura {

namespace {
struct PtInfo {
    char const* name;
    char const* aliases[4];
    bool        vanilla;  // has a VanillaModelProvider
    Vec3        offset;
    bool        pivot;    // isPivot
};
// org.figuramc.figura.model.ParentType, same order
PtInfo const kPt[] = {
    {"None", {"NONE"}, false, {}, false},
    {"Model", {"MODEL"}, false, {}, false},
    {"Head", {"HEAD"}, true, {}, false},
    {"Body", {"BODY"}, true, {}, false},
    {"LeftArm", {"LEFT_ARM"}, true, {5, 2, 0}, false},
    {"RightArm", {"RIGHT_ARM"}, true, {-5, 2, 0}, false},
    {"LeftLeggingPivot", {"LEFT_LEGGING_PIVOT"}, false, {}, true},
    {"RightLeggingPivot", {"RIGHT_LEGGING_PIVOT"}, false, {}, true},
    {"LeftLeg", {"LEFT_LEG"}, true, {1.9, 12, 0}, false},
    {"RightLeg", {"RIGHT_LEG"}, true, {-1.9, 12, 0}, false},
    {"LeftElytraPivot", {"LEFT_ELYTRA_PIVOT", "LeftWingPivot", "LEFT_WING_PIVOT"}, false, {}, true},
    {"RightElytraPivot", {"RIGHT_ELYTRA_PIVOT", "RightWingPivot", "RIGHT_WING_PIVOT"}, false, {}, true},
    {"LeftElytra", {"LEFT_ELYTRA", "LeftElytron", "LEFT_ELYTRON"}, true, {5, 0, 0}, false},
    {"RightElytra", {"RIGHT_ELYTRA", "RightElytron", "RIGHT_ELYTRON"}, true, {-5, 0, 0}, false},
    {"Cape", {"CAPE"}, true, {}, false},
    {"World", {"WORLD"}, false, {}, false},
    {"Hud", {"HUD", "HeadsUpDisplay"}, false, {}, false},
    {"Gui", {"GUI", "GraphicalUserInterface", "JraficalUserInterface"}, false, {}, false},
    {"Camera", {"CAMERA"}, false, {}, false},
    {"Billboard", {"BILLBOARD"}, false, {}, false},
    {"Skull", {"SKULL"}, false, {}, false},
    {"Portrait", {"PORTRAIT"}, false, {}, false},
    {"Arrow", {"ARROW"}, false, {}, false},
    {"Trident", {"TRIDENT"}, false, {}, false},
    {"Item", {"ITEM"}, false, {}, false},
    {"LeftItemPivot", {"LEFT_ITEM_PIVOT"}, false, {}, true},
    {"RightItemPivot", {"RIGHT_ITEM_PIVOT"}, false, {}, true},
    {"LeftSpyglassPivot", {"LEFT_SPYGLASS_PIVOT"}, false, {}, true},
    {"RightSpyglassPivot", {"RIGHT_SPYGLASS_PIVOT"}, false, {}, true},
    {"LeftParrotPivot", {"LEFT_PARROT_PIVOT"}, false, {}, true},
    {"RightParrotPivot", {"RIGHT_PARROT_PIVOT"}, false, {}, true},
    {"HelmetItemPivot", {"HELMET_ITEM_PIVOT"}, false, {}, true},
    {"HelmetPivot", {"HELMET_PIVOT"}, false, {}, true},
    {"ChestplatePivot", {"CHESTPLATE_PIVOT"}, false, {}, true},
    {"ChestplateBodyPivot", {"CHESTPLATE_BODY_PIVOT"}, false, {}, true},
    {"LeftShoulderPivot", {"LEFT_SHOULDER_PIVOT"}, false, {}, true},
    {"RightShoulderPivot", {"RIGHT_SHOULDER_PIVOT"}, false, {}, true},
    {"LeggingsPivot", {"LEGGINGS_PIVOT"}, false, {}, true},
    {"BeltPivot", {"BELT_PIVOT"}, false, {}, true},
    {"LeftBootPivot", {"LEFT_BOOT_PIVOT"}, false, {}, true},
    {"RightBootPivot", {"RIGHT_BOOT_PIVOT"}, false, {}, true},
};
static_assert(sizeof(kPt) / sizeof(kPt[0]) == size_t(ParentType::Count));
} // namespace

char const* parentTypeName(ParentType t) { return t < ParentType::Count ? kPt[int(t)].name : "None"; }
bool        parentTypeHasVanilla(ParentType t) { return t < ParentType::Count && kPt[int(t)].vanilla; }
Vec3        parentTypeOffset(ParentType t) { return t < ParentType::Count ? kPt[int(t)].offset : Vec3{}; }
bool        parentTypeIsPivot(ParentType t) { return t < ParentType::Count && kPt[int(t)].pivot; }

ParentType parentTypeFromName(std::string const& s, bool* ok) {
    for (int i = 0; i < int(ParentType::Count); i++)
        if (s == kPt[i].name) {
            if (ok) *ok = true;
            return ParentType(i);
        }
    if (ok) *ok = false;
    return ParentType::None;
}

ParentType parentTypeGet(std::string const& n) {
    for (int i = 0; i < int(ParentType::Count); i++) {
        if (n.rfind(kPt[i].name, 0) == 0) return ParentType(i);
        for (char const* a : kPt[i].aliases)
            if (a && n.rfind(a, 0) == 0) return ParentType(i);
    }
    return ParentType::None;
}

// ---------------------------------------------------------------- PartCustomization.recalculate
void Part::recalculate() {
    if (customMatrix) return;
    Mat4& m = positionMatrix;
    m.reset();
    m.translate(offsetPos.x - pivot.x - offsetPivot.x, offsetPos.y - pivot.y - offsetPivot.y,
                offsetPos.z - pivot.z - offsetPivot.z);
    stackScale = {offsetScale.x * scale.x * animScale.x, offsetScale.y * scale.y * animScale.y,
                  offsetScale.z * scale.z * animScale.z};
    m.scale(stackScale);
    if (type == PartType::Mesh) {
        m.rotateZ(rotation.z + offsetRot.z + animRot.z);
        m.rotateY(rotation.y + offsetRot.y + animRot.y);
        m.rotateX(rotation.x + offsetRot.x + animRot.x);
    } else {
        m.rotateZYX(rotation.x + offsetRot.x + animRot.x, rotation.y + offsetRot.y + animRot.y,
                    rotation.z + offsetRot.z + animRot.z);
    }
    m.translate(position.x + animPos.x + pivot.x + offsetPivot.x, position.y + animPos.y + pivot.y + offsetPivot.y,
                position.z + animPos.z + pivot.z + offsetPivot.z);
}

// ---------------------------------------------------------------- vanilla transforms
void Part::applyVanillaTransforms(VanillaPart const* d) {
    if (!d || !parentTypeHasVanilla(parentType)) return;
    customMatrix = false; // offset setters set needsMatrixRecalculation (a setMatrix() matrix is recomputed)
    hasPlayerCustomization = true;
    saveOffsetPivot = offsetPivot;
    saveOffsetPos   = offsetPos;
    saveOffsetRot   = offsetRot;
    saveOffsetScale = offsetScale;
    vanillaVisible  = d->visible;
    Vec3 defaultPivot = parentTypeOffset(parentType) - d->pos;
    if (!overrideVanillaScale()) {
        Vec3 newScale = d->scale.mul(offsetScale);
        defaultPivot  = defaultPivot.mul(newScale);
        offsetScale   = newScale;
    }
    if (!overrideVanillaPos()) {
        offsetPivot += defaultPivot;
        offsetPos += defaultPivot;
    }
    if (!overrideVanillaRot()) offsetRot += d->rot;
}

void Part::resetVanillaTransforms() {
    if (!parentTypeHasVanilla(parentType)) return;
    if (!overrideVanillaPos()) {
        offsetPivot = hasPlayerCustomization ? saveOffsetPivot : Vec3{};
        offsetPos   = hasPlayerCustomization ? saveOffsetPos : Vec3{};
    }
    if (!overrideVanillaRot()) offsetRot = hasPlayerCustomization ? saveOffsetRot : Vec3{};
    if (!overrideVanillaScale()) offsetScale = hasPlayerCustomization ? saveOffsetScale : Vec3{1, 1, 1};
    hasPlayerCustomization = false;
    vanillaVisible.reset();
}

// ---------------------------------------------------------------- animation channels (FiguraModelPart)
void Part::animPosition(Vec3 v, bool merge) {
    Vec3 n{-v.x, v.y, v.z};
    animPos = merge ? animPos + n : n;
    customMatrix = false;
}
void Part::animRotation(Vec3 v, bool merge) {
    Vec3 n{-v.x, -v.y, v.z};
    animRot = merge ? animRot + n : n;
    customMatrix = false;
}
void Part::animScaleSet(Vec3 v, bool merge) {
    animScale    = merge ? animScale.mul(v) : v;
    customMatrix = false;
}

bool Part::isChildOf(Part const* p) const {
    for (Part const* q = parent; q; q = q->parent)
        if (q == p) return true;
    return false;
}

std::string Part::path() const {
    if (!parent) return name;
    return parent->path() + "." + name;
}

Part* Part::child(std::string const& n) const {
    for (Part* c : children)
        if (c->name == n) return c;
    return nullptr;
}

bool Part::effectiveVisible() const {
    for (Part const* p = this; p; p = p->parent) {
        if (!p->visible) return false;
        if (p->vanillaVisible.has_value() && !*p->vanillaVisible) return false;
    }
    return true;
}

} // namespace bf::figura
