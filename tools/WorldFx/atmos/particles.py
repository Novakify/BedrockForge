"""World FX particles (by Novak): critical hits become short sparks thrown off the blade - thin streaks along their
motion, white-gold at birth cooling to ember orange - instead of the vanilla stars. Few, quick, readable in PvP."""
from __future__ import annotations

import json

import numpy as np
from PIL import Image


def spark_texture(path):
    a = np.zeros((16, 16, 4), np.uint8)
    y, x = np.mgrid[0:16, 0:16] + 0.5
    core = np.exp(-((x - 8) / 1.2) ** 2) * np.clip(1 - np.abs(y - 8) / 8, 0, 1) ** 0.6
    a[..., :3] = 255
    a[..., 3] = (np.clip(core, 0, 1) * 255).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(a, "RGBA").save(path)


def crit(hot, cool):
    return {"format_version": "1.10.0", "particle_effect": {
        "description": {"identifier": "minecraft:critical_hit_emitter",
                        "basic_render_parameters": {"material": "particles_alpha", "texture": "textures/worldfx/spark"}},
        "components": {
            "minecraft:emitter_lifetime_once": {"active_time": 0.1},
            "minecraft:emitter_rate_steady": {"spawn_rate": 260, "max_particles": 22},
            "minecraft:emitter_shape_point": {"offset": [0, "Math.random(-0.9, -0.5)", 0],
                                              "direction": ["Math.random(-0.8, 0.8)", "Math.random(-0.2, 1.0)", "Math.random(-0.8, 0.8)"]},
            "minecraft:particle_appearance_billboard": {
                "size": ["0.022", "0.11 + variable.particle_random_1 * 0.07"],
                "facing_camera_mode": "lookat_direction",
                "direction": {"mode": "derive_from_velocity", "min_speed_threshold": 0.01},
                "uv": {"texture_width": 16, "texture_height": 16, "uv": [0, 0], "uv_size": [16, 16]}},
            "minecraft:particle_appearance_tinting": {"color": {"gradient": {
                "0.0": hot + [1], "0.45": cool + [0.9], "1.0": cool + [0.0]},
                "interpolant": "variable.particle_age / variable.particle_lifetime"}},
            "minecraft:particle_initial_speed": "Math.random(7.0, 13.0)",
            "minecraft:particle_lifetime_expression": {"max_lifetime": "Math.random(0.18, 0.32)"},
            "minecraft:particle_motion_dynamic": {"linear_acceleration": [0, -14, 0], "linear_drag_coefficient": 7}}}}


def export(pack, hot=(1.0, 0.93, 0.72), cool=(1.0, 0.52, 0.18)):
    spark_texture(pack / "textures" / "worldfx" / "spark.png")
    (pack / "particles").mkdir(parents=True, exist_ok=True)
    (pack / "particles" / "critical_hit.json").write_text(json.dumps(crit(list(hot), list(cool)), indent=1))
