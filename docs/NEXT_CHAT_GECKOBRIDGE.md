# Next-chat prompt: GeckoBridge (research branch only)

Paste this into a new chat:

---
Project Bedrock Forge - GeckoBridge research. Repo Novakify/BedrockForge (private); RF code on branch `cinematic`
(Cinematic Interactions + Spatial GUI ports are done - read docs/NEXT_SESSION.md and docs/PORT_CINEMATIC.md first for
how RF is built, installed and tested on my PC). GeckoBridge = a GeckoLib 4/5 compatibility subsystem so Java mods'
GeckoLib models/animations can run on Bedrock.

Rules:
- RESEARCH BRANCH ONLY: create `research/geckobridge` from main, never push to main, keep pushing the branch.
- Research first, no engine hooks until the plan is agreed: GeckoLib's .geo.json / .animation.json formats vs
  Bedrock's own entity geometry + animation + render controller formats (they share Blockbench roots), what converts
  offline (a converter that writes a Bedrock resource/behaviour pack) and what needs a runtime piece (molang
  differences, GeckoLib keyframe easing, animation controllers, bone render layers, emissive/glow layers).
- I prefer a hybrid: offline conversion + a small runtime in RF/BedrockForge only where needed.
- Pick 2-3 real GeckoLib mods (models + animations in their jars) as test cases; list the features each uses.
- Deliverables: docs/PORT_GECKOBRIDGE.md (format mapping table, what's 1:1 / approximate / impossible, the converter
  design, the runtime design, milestones, risks), plus a tiny proof: one GeckoLib model + idle animation converted and
  shown in game on my PC (computer use is approved: keys only in the world, pause menu is safe; ask before changing
  my game controls).
- Update docs/NEXT_SESSION.md as you go. Credit original authors; ask me about licences/permissions before shipping
  anything from a mod.
---
