// FiguraBridge: the vanilla player pose Figura copies onto Head/Body/arm/leg parts (VanillaModelData). Bedrock's own
// player animations are different, so this is a port of Java Edition's HumanoidModel.setupAnim (1.20.1) + the
// PlayerRenderer arm-pose rules, evaluated from PlayerState. Output already in Figura's convention
// (PartData.updateFromPart: pos (x, y, -z), rot (deg -xRot, deg -yRot, deg zRot)).
#include "figura/core/Vanilla.h"

#include <algorithm>
#include <cmath>

namespace bf::figura {

namespace {
constexpr float PI = 3.14159265358979323846f;

struct MP { // ModelPart
    float x = 0, y = 0, z = 0, xRot = 0, yRot = 0, zRot = 0;
    bool  visible = true;
};

enum class ArmPose { Empty, Item, Block, BowAndArrow, ThrowSpear, CrossbowCharge, CrossbowHold, Spyglass, TootHorn, Brush };

bool twoHanded(ArmPose p) { return p == ArmPose::BowAndArrow || p == ArmPose::CrossbowCharge || p == ArmPose::CrossbowHold; }

float wrapDeg(float d) {
    d = std::fmod(d, 360.0f);
    if (d >= 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}
float rotlerpRad(float a, float from, float to) {
    float d = std::fmod(to - from + PI, 2 * PI);
    if (d < 0) d += 2 * PI;
    d -= PI;
    return from + a * d;
}

ArmPose armPose(PlayerState const& p, bool mainHand) {
    ItemState const& it = mainHand ? p.mainHand : p.offHand;
    if (it.empty()) return ArmPose::Empty;
    bool usingThis = p.usingItem && (p.activeHandOff != mainHand) && p.useItemRemaining > 0;
    if (usingThis) {
        std::string const& a = it.useAction;
        if (a == "BLOCK") return ArmPose::Block;
        if (a == "BOW") return ArmPose::BowAndArrow;
        if (a == "SPEAR") return ArmPose::ThrowSpear;
        if (a == "CROSSBOW") return ArmPose::CrossbowCharge;
        if (a == "SPYGLASS") return ArmPose::Spyglass;
        if (a == "TOOT_HORN") return ArmPose::TootHorn;
        if (a == "BRUSH") return ArmPose::Brush;
    } else if (!p.swinging && it.id == "minecraft:crossbow" && it.charged) {
        return ArmPose::CrossbowHold;
    }
    return ArmPose::Item;
}

void crossbowHold(MP& r, MP& l, MP const& head, bool rightArm) {
    MP& a = rightArm ? r : l;
    MP& b = rightArm ? l : r;
    a.yRot = (rightArm ? -0.3f : 0.3f) + head.yRot;
    b.yRot = (rightArm ? 0.6f : -0.6f) + head.yRot;
    a.xRot = -PI / 2 + head.xRot + 0.1f;
    b.xRot = -1.5f + head.xRot;
}
void crossbowCharge(MP& r, MP& l, PlayerState const& p, bool rightArm) {
    MP& a = rightArm ? r : l;
    MP& b = rightArm ? l : r;
    a.yRot = rightArm ? -0.8f : 0.8f;
    a.xRot = -0.97079635f;
    b.xRot = a.xRot;
    float f  = float(std::max(1, p.activeItem.maxUseDuration > 0 ? 25 : 25));
    float f1 = std::clamp(float(p.activeItem.maxUseDuration - p.useItemRemaining), 0.0f, f);
    float f2 = f1 / f;
    b.yRot = (rightArm ? 0.4f : -0.4f) + (rightArm ? 0.85f : -0.85f) * f2;
    b.xRot = b.xRot + (-PI / 2 - b.xRot) * f2; // Mth.lerp(f2, b.xRot, -PI/2)
}

void poseArm(bool right, ArmPose pose, MP& rArm, MP& lArm, MP const& head, PlayerState const& p) {
    MP& arm = right ? rArm : lArm;
    float s = right ? 1.0f : -1.0f;
    switch (pose) {
    case ArmPose::Empty: arm.yRot = 0; break;
    case ArmPose::Block: arm.xRot = arm.xRot * 0.5f - 0.9424779f; arm.yRot = -s * (PI / 6); break;
    case ArmPose::Item: arm.xRot = arm.xRot * 0.5f - PI / 10; arm.yRot = 0; break;
    case ArmPose::ThrowSpear: arm.xRot = arm.xRot * 0.5f - PI; arm.yRot = 0; break;
    case ArmPose::BowAndArrow:
        rArm.yRot = -0.1f + head.yRot - (right ? 0 : 0.4f);
        lArm.yRot = 0.1f + head.yRot + (right ? 0.4f : 0);
        rArm.xRot = -PI / 2 + head.xRot;
        lArm.xRot = -PI / 2 + head.xRot;
        break;
    case ArmPose::CrossbowCharge: crossbowCharge(rArm, lArm, p, right); break;
    case ArmPose::CrossbowHold: crossbowHold(rArm, lArm, head, right); break;
    case ArmPose::Brush: arm.xRot = arm.xRot * 0.5f - PI / 5; arm.yRot = 0; break;
    case ArmPose::Spyglass:
        arm.xRot = std::clamp(head.xRot - 1.9198622f - (p.crouching ? 0.2617994f : 0), -2.4f, 3.3f);
        arm.yRot = head.yRot - s * 0.2617994f;
        break;
    case ArmPose::TootHorn:
        arm.xRot = std::clamp(head.xRot, -1.2f, 1.2f) - 1.4835298f;
        arm.yRot = head.yRot - s * (PI / 6);
        break;
    }
}

void bob(MP& part, float age, float scale) {
    part.zRot += scale * (std::cos(age * 0.09f) * 0.05f + 0.05f);
    part.xRot += scale * std::sin(age * 0.067f) * 0.05f;
}

VanillaPart toFigura(MP const& m) {
    VanillaPart v;
    v.pos     = {m.x, m.y, -m.z};
    v.rot     = {toDeg(-m.xRot), toDeg(-m.yRot), toDeg(m.zRot)};
    v.scale   = {1, 1, 1};
    v.visible = m.visible;
    return v;
}
} // namespace

VanillaPose computeVanillaPose(PlayerState const& p, float delta, bool playerVisible) {
    MP head, body, rArm{-5, 2, 0}, lArm{5, 2, 0}, rLeg{-1.9f, 12, 0}, lLeg{1.9f, 12, 0};
    float limbSwing  = p.walkPos - p.walkSpeed * (1 - delta);
    float limbAmount = std::min(1.0f, p.prevWalkSpeed + (p.walkSpeed - p.prevWalkSpeed) * delta);
    float age        = float(p.age) + delta;
    float bodyYaw    = p.prevBodyYaw + wrapDeg(p.bodyYaw - p.prevBodyYaw) * delta;
    float headYaw    = p.yaw;
    float netHeadYaw = wrapDeg(headYaw - bodyYaw);
    float headPitch  = p.prevPitch + (p.pitch - p.prevPitch) * delta;
    bool  riding     = p.hasVehicle();
    if (riding) limbSwing = limbAmount = 0;
    bool  fallFlying = p.gliding;
    float swimAmount = p.swimming ? 1.0f : 0.0f;

    head.yRot = netHeadYaw * (PI / 180);
    if (fallFlying) head.xRot = -PI / 4;
    else if (swimAmount > 0) head.xRot = rotlerpRad(swimAmount, head.xRot, p.swimming ? -PI / 4 : headPitch * (PI / 180));
    else head.xRot = headPitch * (PI / 180);
    body.yRot = 0;
    rArm.z = 0; rArm.x = -5;
    lArm.z = 0; lArm.x = 5;
    float f = 1;
    if (fallFlying) {
        f = float(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y + p.velocity.z * p.velocity.z) / 0.2f;
        f = f * f * f;
    }
    if (f < 1) f = 1;
    rArm.xRot = std::cos(limbSwing * 0.6662f + PI) * 2.0f * limbAmount * 0.5f / f;
    lArm.xRot = std::cos(limbSwing * 0.6662f) * 2.0f * limbAmount * 0.5f / f;
    rArm.zRot = lArm.zRot = 0;
    rLeg.xRot = std::cos(limbSwing * 0.6662f) * 1.4f * limbAmount / f;
    lLeg.xRot = std::cos(limbSwing * 0.6662f + PI) * 1.4f * limbAmount / f;
    rLeg.yRot = lLeg.yRot = rLeg.zRot = lLeg.zRot = 0;
    if (riding) {
        rArm.xRot += -PI / 5; lArm.xRot += -PI / 5;
        rLeg.xRot = -1.4137167f; rLeg.yRot = PI / 10; rLeg.zRot = 0.07853982f;
        lLeg.xRot = -1.4137167f; lLeg.yRot = -PI / 10; lLeg.zRot = -0.07853982f;
    }
    rArm.yRot = lArm.yRot = 0;
    bool    mainRight = !p.leftHanded;
    ArmPose mainPose  = armPose(p, true), offPose = armPose(p, false);
    ArmPose rightPose = mainRight ? mainPose : offPose, leftPose = mainRight ? offPose : mainPose;
    if (p.usingItem) {
        bool usedMain = !p.activeHandOff;
        if (usedMain == mainRight) poseArm(true, rightPose, rArm, lArm, head, p);
        else poseArm(false, leftPose, rArm, lArm, head, p);
    } else {
        bool flag4 = mainRight ? twoHanded(leftPose) : twoHanded(rightPose);
        if (mainRight != flag4) {
            poseArm(false, leftPose, rArm, lArm, head, p);
            poseArm(true, rightPose, rArm, lArm, head, p);
        } else {
            poseArm(true, rightPose, rArm, lArm, head, p);
            poseArm(false, leftPose, rArm, lArm, head, p);
        }
    }
    // setupAttackAnimation
    float attackTime = p.attackAnim;
    if (attackTime > 0) {
        bool leftAttack = (p.swingOffhand ? mainRight : !mainRight);
        MP&  part       = leftAttack ? lArm : rArm;
        body.yRot = std::sin(std::sqrt(attackTime) * (PI * 2)) * 0.2f;
        if (leftAttack) body.yRot *= -1.0f;
        rArm.z = std::sin(body.yRot) * 5.0f;
        rArm.x = -std::cos(body.yRot) * 5.0f;
        lArm.z = -std::sin(body.yRot) * 5.0f;
        lArm.x = std::cos(body.yRot) * 5.0f;
        rArm.yRot += body.yRot;
        lArm.yRot += body.yRot;
        lArm.xRot += body.yRot;
        float g = 1.0f - attackTime;
        g *= g; g *= g;
        g = 1.0f - g;
        float g1 = std::sin(g * PI);
        float g2 = std::sin(attackTime * PI) * -(head.xRot - 0.7f) * 0.75f;
        part.xRot -= g1 * 1.2f + g2;
        part.yRot += body.yRot * 2.0f;
        part.zRot += std::sin(attackTime * PI) * -0.4f;
    }
    if (p.crouching) {
        body.xRot = 0.5f; rArm.xRot += 0.4f; lArm.xRot += 0.4f;
        rLeg.z = 4.0f; lLeg.z = 4.0f; rLeg.y = 12.2f; lLeg.y = 12.2f;
        head.y = 4.2f; body.y = 3.2f; lArm.y = 5.2f; rArm.y = 5.2f;
    } else {
        body.xRot = 0; rLeg.z = 0.1f; lLeg.z = 0.1f; rLeg.y = 12.0f; lLeg.y = 12.0f;
        head.y = 0; body.y = 0; lArm.y = 2.0f; rArm.y = 2.0f;
    }
    if (rightPose != ArmPose::Spyglass) bob(rArm, age, 1.0f);
    if (leftPose != ArmPose::Spyglass) bob(lArm, age, -1.0f);
    if (swimAmount > 0) {
        // HumanoidModel swim stroke, simplified to the "arms forward" glide pose (swim animation timing needs the
        // attack/swim tick counters Bedrock doesn't expose); legs keep the walk swing.
        float a = swimAmount;
        lArm.xRot = rotlerpRad(a, lArm.xRot, -PI);
        rArm.xRot = rotlerpRad(a, rArm.xRot, -PI);
        lArm.zRot = rotlerpRad(a, lArm.zRot, 0);
        rArm.zRot = rotlerpRad(a, rArm.zRot, 0);
        lLeg.xRot = 0.3f * std::cos(limbSwing * 0.33333334f + PI);
        rLeg.xRot = 0.3f * std::cos(limbSwing * 0.33333334f);
    }
    head.visible = body.visible = rArm.visible = lArm.visible = rLeg.visible = lLeg.visible = playerVisible;
    VanillaPose out;
    out.head = toFigura(head);
    out.body = toFigura(body);
    out.rightArm = toFigura(rArm);
    out.leftArm = toFigura(lArm);
    out.rightLeg = toFigura(rLeg);
    out.leftLeg = toFigura(lLeg);
    MP cape;
    cape.y = 0; cape.z = 0; // FAKE_CAPE: Figura derives cape from the cloak; keep neutral
    out.cape = toFigura(cape);
    out.cape.visible = playerVisible;
    MP lEly{5, 0, 0}, rEly{-5, 0, 0};
    out.leftElytra = toFigura(lEly);
    out.rightElytra = toFigura(rEly);
    out.leftElytra.visible = out.rightElytra.visible = false;
    return out;
}

VanillaPart const* VanillaPose::get(ParentType t) const {
    switch (t) {
    case ParentType::Head: return &head;
    case ParentType::Body: return &body;
    case ParentType::LeftArm: return &leftArm;
    case ParentType::RightArm: return &rightArm;
    case ParentType::LeftLeg: return &leftLeg;
    case ParentType::RightLeg: return &rightLeg;
    case ParentType::Cape: return &cape;
    case ParentType::LeftElytra: return &leftElytra;
    case ParentType::RightElytra: return &rightElytra;
    default: return nullptr;
    }
}

} // namespace bf::figura
