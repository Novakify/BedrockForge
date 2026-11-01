# FiguraBridge headless tests
- `build_sim.sh [OBJDIR] [OUT]` builds `Sim` (fake host + scripted player) from src/figura/core.
- `Sim <compiled_dir> <scenario> <out.json> [frameEvery]`: scenarios idle walk run crouch jump swing sit all wheel coverage.
  Compiled dirs come from `python3 -I tools/FiguraBridge/compile.py AVATAR OUT --vanilla <bedrock-samples>/resource_pack`.
- `coverage`: runs `coverage.lua` (env COVERAGE overrides) which probes every method of every Figura 0.1.6 Lua type
  (`figura_api.json`, extracted from Figura's @LuaWhitelist methods) on a live instance and prints what's missing.
- `tools/FiguraBridge/render_sim.js` renders Sim output frames on the compiled geometry.
