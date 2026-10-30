# LyricsPreview: look at presets without the game

Runs the real LyricsFlow port core (`src/lyrics/core`: placement, animation, glyph quads, falling-letter physics)
through a scripted 6 s walk in a fake voxel world and renders it with the same maths as RF's `shaders/glyph.hlsl`
(BF camera estimate path, no depth test). Used to tune presets against the 4 reference videos.

What it proves: layout, sizes, colours, fonts, wall fitting, letters landing. What it can't prove: anything about the
game itself (camera hook, draw point, HUD order, real blocks). Those still need the PC.

```
g++ -std=c++20 -O2 -Isrc src/lyrics/core/*.cpp tools/LyricsPreview/preview.cpp -o lyrics_preview
./lyrics_preview . neon_arena arena /tmp/na 6 20          # preset[,overlay]  field|arena  out dir  seconds  fps
python3 tools/LyricsPreview/render_clip.py /tmp/na na.mp4 640 --sky dusk          # day | dusk | night
python3 tools/LyricsPreview/render_clip.py /tmp/na na.png 640 --sky dusk --still 80
```

Scenes: `field` = flat grass + a few blocks, `arena` = walls on three sides + a step (for WALL mode).
