# LyricsSky - live sky tooling (LyricsFlow 2.1 port)

LyricsFlow 2.1's sky look is one fragment shader, `resources/lyricsflow/shaders/live_sky.fsh` (374m, MIT, copied
unchanged). The core (`src/lyrics/core/Sky.*`) owns the preset table and packs the 16x1 parameter row exactly like
`LiveSkyShader.params()`; the tests pin the byte layout.

* `sky_params.cpp` - dumps the packed rows for every preset (JSON) for the harness.
* `render_sky.js` - runs the ORIGINAL shader in headless Chromium/WebGL2 with those rows -> PNGs
  (`yaw`/`pitch` `auto` faces the sun or moon). The panel thumbnails in `tools/LyricsPanel/sky_thumbs` come from it.
* `gen_sky_shaders.py` - mechanical GLSL -> HLSL for Renderer Foundation (`live_sky.hlsl`) plus a GLSL check build of
  the same body; rendering the check build with `SKY_FSH=tools/LyricsSky/out/live_sky_check.glsl` must match the
  original pixel for pixel (it does for all 29 presets: max diff 0).

```
g++ -std=c++20 -Isrc src/lyrics/core/*.cpp tools/LyricsSky/sky_params.cpp -o /tmp/sky_params
/tmp/sky_params 1000 > /tmp/skyparams.json
node tools/LyricsSky/render_sky.js /tmp/skyparams.json /tmp/sky 400 200 auto auto 70 all
python3 tools/LyricsSky/gen_sky_shaders.py
```
Layers (`src/lyrics/core/SkyFx.*`, from WorldSky): stars, constellations, shooting stars, storm lightning, weather
particles (rain, snow, blizzard, petals, embers, fireflies; sprites = `resources/lyricsflow/sky/sprites.png`, made by
running the jar's own `SkyPainter.sprites()`), "My picture" choice and the underground fade. `SkySystem` publishes
everything per frame for RF (`BF_CopySkyFrame`, BfLyrics.h).
* `sky_frame.cpp` + `compose_sky.py` - simulate the layers for a preset/camera and draw them over a dome render.
Still missing: the in-game hook (RF has to draw the dome where Bedrock draws its sky - needs the PC).
