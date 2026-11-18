// FiguraBridge: Java Edition vanilla player pose (HumanoidModel.setupAnim) for Figura's vanilla-parented parts.
#pragma once
#include "figura/core/Host.h"
#include "figura/core/Model.h"

namespace bf::figura {

struct VanillaPose {
    VanillaPart head, body, leftArm, rightArm, leftLeg, rightLeg, cape, leftElytra, rightElytra;
    VanillaPart const* get(ParentType t) const;
};

/// delta = partial tick (0..1). playerVisible = vanilla_model.PLAYER visibility (VanillaModelData PartData.visible).
VanillaPose computeVanillaPose(PlayerState const& p, float delta, bool playerVisible);

} // namespace bf::figura
