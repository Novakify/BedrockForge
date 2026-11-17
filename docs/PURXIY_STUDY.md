# Purxiy Loader study (2026-11-17)

Source: Purxiy Loader 0.1.0-prelaunch.20261114 (Java Fabric 1.21.11, "visuals" edition), sent to Novak by its
makers for his own client. The jar's own metadata says All-Rights-Reserved; the bundled notes record Novak's
statement of permission (unverified, not a relicence). Personal use only. Code is JNIC-native (compiled to a
28 MB native blob) + obfuscated + signed integrity index: NOT decompiled, NOT bypassed. What we use is the readable
design layer: purxiy-loader.json (65 module definitions with settings), names/descriptions, the look.
Fonts it ships are free on their own: Inter (OFL), Fira Sans Extra Condensed (OFL), Lilita One (OFL), Lucide icons
(ISC) - take them from upstream, keep the licence files.

## Not taken
- FakePay / FakeStats (fake /pay messages and sidebar money "for clips") and Roles (fake staff prefixes): fake
  records made to look real. Not building these.
- Region Map (Donut SMP specific), Garage (cars), Knives - low value for Bedrock PvP.

## What we already have (src/client)
FPS, CPS, Coordinates, Direction, Speed, Clock, Session Timer, Keystrokes, Armor/Potion HUD, Crosshair, Zoom,
Freelook, Time Changer, Low Fire, Block Outline, Better Nametags, Movable HUD, Cosmetics, Hit Color/Particles,
plus our own: LyricsForge (Purxiy's "Spotify Player with lyrics" = ours), glass UI, Glass Armor (= their "Glass"),
living skies (= their "Sky"), World FX / Cloud Sea (= their Haze/Mist/Ambience), IrisBridge shaders (they have none).

## Gaps worth taking (original implementations, Bedrock-native, in priority order)
1. PvP FX set (RF world-space passes, visual only, readable): Hit Splash, Ripple (hit wave + crit stars + light
   pillar), Shockwave (landing / explosions), Afterglow (kills dissolve into light), Place Animation (blocks grow in),
   Jump Circles. Every effect: palette, glow, duration, "other players" toggle.
2. Polish layer: HUD editor with snap guides + prevent-overlap + scroll-to-resize; Notifications toast on keybind
   toggles; Active Modules list (gradient); Motion settings (one speed + reduce-motion for all UI animation);
   Creamy Keys (keyboard / click / scroll sounds); Inter + Lucide in the glass UI; Watermark styles.
3. World moods: Chroma (world in grey, colour blooms where you walk - signature look), Cyberspace, Cute World,
   Weather (glassy rain / snow / embers), Bokeh depth of field. These sit naturally on RF World FX + IrisBridge.
4. Cosmetics: Aura, Halo, Wings, Trail, animated Cape (GeckoBridge / FiguraBridge can carry the models).
5. Target HUD (health / stats of who you aim at), Ping, Memory, Server, HUD Hider (per-element).

## Patterns to copy (design, not code)
- One module schema: id, name, icon, category, description, toggleable, defaultKey, onByDefault,
  settingDefinitions[{id, name, description, kind (Number/Boolean/Enum/Colour/Palette), default, min, max, step}].
  HUD elements share x/y (0..1), size, background (frosted), opacity. Lets the menu, the HUD editor, config save and
  presets all come from data.
- Every visual effect exposes the same knobs: Style, Palette, Colour, Glow, Speed/Duration, Other players.
