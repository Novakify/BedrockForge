// [Prism] Plain-language help for every module and the settings that need it, shown in the Right Shift menu:
// the "About" card at the top of a module's page and the second line under each setting.
// Keep each "about" to what it changes, how it works, and anything to know (cost, servers, what to pair it with).
#include <string>
#include <unordered_map>

namespace bedrock_forge::client {
namespace {
using Map = std::unordered_map<std::string, std::string>;

Map const kAbout = {
    // ---- HUD
    {"server_info", "Shows the world or server you're on and how many players are there. Updates as people join and leave."},
    {"memory", "Shows how much RAM the game is using right now. Handy for spotting packs or mods that eat memory."},
    {"watermark", "The Bedrock Forge mark in a corner of your screen. Purely cosmetic, drag it in the HUD editor."},
    {"notifications", "A small pop-up whenever a keybind turns a module on or off, so you always know what just changed."},
    {"active_modules", "A list of every module you have switched on, sorted by length, along the edge of your screen."},
    {"fps", "Your frames per second, with an optional live graph of frame times: spikes in the graph are the stutters you feel."},
    {"cps", "Clicks per second for the left and right mouse button, counted over the last second."},
    {"coordinates", "Your X, Y and Z position in the world."},
    {"direction", "Which way you're facing (N, NE...) and your exact yaw. The compass strip is a bar at the top that slides as you turn."},
    {"speed", "How fast you're moving across the ground, in blocks per second (jumping and falling don't count)."},
    {"clock", "Your computer's local time, so you don't have to tab out."},
    {"session", "How long you've been playing since the game started, and optionally how long in this world or server."},
    {"xp_info", "Your current level and how much XP you still need for the next one."},
    {"item_counter", "How many of the item in your hand you carry in total, across your whole inventory."},
    {"keystrokes", "Shows W A S D, Space and your mouse buttons lighting up as you press them. Popular for recording PvP."},
    {"armor_hud", "Durability of each armour piece you wear (and your held item) with its icon, so you know when something is about to break."},
    {"potion_hud", "Every active potion effect with its level and time left."},
    {"player_list", "Everyone on the server in a list at the top right, without holding the player list key."},
    {"crosshair", "Replaces Minecraft's crosshair with your own: pick a shape, size, colour and glow. It can spread while you move, kick when you swing, tint when you aim at a player and flash a marker on hits. Only shows where the normal crosshair would (first person, HUD on)."},
    {"name_tags", "Clean tags above nearby players with their name, a health bar and how far away they are. Like the game's own tags, players who sneak or are invisible get none."},
    {"debug_menu", "A Java-style F3 screen: position, chunk, facing, biome, light and performance details."},
    {"hud_hider", "Pick which parts of Minecraft's own HUD to hide (hotbar, hearts, hunger...) for clean screenshots and videos."},
    {"now_playing", "Your music on screen while you play: song, artist, cover art and synced lyrics from whatever player you use (Apple Music, Spotify...). Media keys work too."},
    // ---- Combat
    {"target_hud", "When you fight someone, a card shows their health, armour and how the fight is going (hits, combo, who's winning)."},
    {"reach_display", "Shows how far away the player or mob was when your last hit landed, in blocks."},
    {"combo", "Counts the hits you land in a row without taking a hit yourself. Resets when you get hit."},
    {"cps_cap", "Drops clicks above a limit you set, so you stay under a server's CPS rule. It only removes clicks, never adds them."},
    {"hit_color", "Changes the red flash mobs and players get when they're hit to a colour you pick."},
    // ---- Camera
    {"freelook", "Look around your character without changing where you're facing or walking: hold (or toggle) your key and the camera orbits you. The orbit stops at walls like the normal third-person camera. No key by default: pick one below."},
    {"zoom", "Hold the zoom key for a smooth, scope-like zoom. Turning slows down while zoomed so you can aim precisely."},
    {"fov_changer", "Scales your field of view beyond what the video settings allow, wider or narrower."},
    {"sensitivity", "Multiplies your mouse sensitivity on top of the game's own setting, for finer or faster aim."},
    {"view_bobbing", "Turns off the camera bobbing up and down while you walk."},
    {"hand_movement", "Moves and resizes the item in your hand in first person, for a cleaner view."},
    {"no_hurt_cam", "Stops the camera from tilting when you take damage."},
    {"motion_blur", "A smooth blur when you turn and move, like a real camera. With a World FX sky on, your hand and the HUD stay sharp. Costs a little GPU."},
    // ---- Visual (Renderer Foundation)
    {"shaderpacks", "Bedrock Forge's shaderpack loader: runs shaderpacks and shaders from your resource packs. Manage them on the Shaders page."},
    {"glass_frost", "Turns players, items and armour into glass: clear like a window or soft and milky. Drawn by Renderer Foundation, only you see it."},
    {"haze", "A soft haze of colour hanging over the world around you. It thickens with distance and clears near you, so nearby fights stay sharp."},
    {"mist", "Low, soft mist that lies in valleys and on the ground, with light shafts through it. Strongest at dawn and dusk."},
    {"ambience", "Little glowing particles drifting through the air around you, like dust or fireflies in the light."},
    {"jump_circles", "A ring of light spreads out on the ground wherever you jump."},
    {"hit_splash", "A burst of glowing light where each of your hits lands on a player or mob."},
    {"ripple", "A ring-shaped wave of light rolls out from every hit you land; critical hits make a bigger one."},
    {"shockwave", "When you land from a jump or fall, a wave of light runs out through the ground."},
    {"trail", "Leaves a ribbon of light behind you as you move, fading after a moment."},
    {"aura", "Soft light that lives around your character: orbiting lights, fireflies, a spiral, a constellation or the golden Ascension shell. Only you see it."},
    {"halo", "A glowing halo floating above your head, in several designs. Only you see it."},
    {"wings", "A pair of softly animated wings on your back: feathered Seraph, Butterfly or crystal Prism. They flap more when you jump and fly. Only you see them."},
    {"cape", "An animated glowing cape that sways and flows with how fast you move. Silk, Aurora or Starfield. Only you see it."},
    {"main_menu", "Puts your own world behind the title screen. Stand somewhere beautiful, turn this on and tap Capture: the camera looks around you once and saves the view. It shows from the next time you start the game."},
    {"cyberspace", "Turns the world digital: glowing block edges, circuit lines on the ground, a data sky with falling code and a scan line that sweeps the world."},
    {"cute_world", "A soft pastel version of the world with drifting hearts and little sparkles in the air."},
    {"block_outline", "Replaces the thin black block outline with a cleaner glowing one in your colour, gliding smoothly from block to block."},
    {"chroma", "The world starts in grey and colour blooms back wherever you walk. It slowly fades back to grey behind you."},
    {"radiance", "Glowing outlines around players, mobs and dropped items so they stand out from the world."},
    {"weather", "Styles the weather: glassy rain with wet, reflective ground and lightning, soft snowfall or ember showers. It can also make it rain for you only."},
    {"bokeh", "Camera-style depth of field: what you look at stays sharp and the background melts into soft light."},
    {"place_animation", "Blocks you place or break flash with a soft light for a moment."},
    {"fullbright", "See in the dark: brightens the light baked into nearby blocks. Chunks relight when you toggle it."},
    {"pvp_preset", "Turns Smooth Lighting off while it's on: chunks build faster, so fewer stutters when new chunks load. Your old setting comes back when you turn it off."},
    {"time_changer", "Sets the time of day for you only (rendering), so you can play in a sunset or at night. Doesn't change the server's time."},
    {"post_processing", "Bloom, colour fringe and sharpening on the whole picture. Overrides the shaderpack's own post effects while on."},
    {"sky", "One place to pick your sky. Living Skies: our LyricsForge skies (galaxies, auroras, sunsets, sakura, storms...) without the lyrics, on Shuffle a new one sweeps in every few minutes. Cloud Sea and Novak Glass: the full World FX atmospheres (Ctrl+F6 still swaps them). Vanilla: Minecraft's own sky."},
    {"knives", "Your sword becomes a 3D knife with its own draw animation, swings, idle tricks and sounds. Only you see it; nothing about the fight changes."},
    {"lyricsflow", "LyricsForge: the lyrics of the song you're playing, synced and floating in the world around you, with fonts and effects. Open its panel with L."},
    // ---- Movement / World / Misc
    {"scroll_disabler", "Stops the mouse wheel from switching hotbar slots, so you don't swap items by accident."},
    {"toggle_sprint", "You always sprint when walking forward, same as holding the sprint key."},
    {"auto_gg", "Says your message (gg by default) in chat when a game ends on Hive, CubeCraft, Lifeboat, Zeqa, Galaxite and Mineville."},
    {"auto_requeue", "Queues you into the next game of the same mode when one ends on The Hive (and optionally when you die)."},
    {"chat_compact", "Collapses repeated chat messages into one line with a counter, like (x3)."},
    {"hud_layout", "Where the info panel (FPS, CPS, speed...) sits and how dark its background is."},
    {"client_ui", "The look and feel of this menu: glass style, accent colour, blur, animations and sounds."},
    {"creamy_keys", "Your keyboard plays a soft sound on every key press, in the style you pick. Our own synthesized sounds."},
    {"nick_hider", "Hides your name in chat and the player list on your screen (for screenshots and streams). Others still see your real name."},
    {"figurabridge", "Shows players' Figura avatars (custom models) from the Java mod, converted for Bedrock."},
    {"loadouts", "Save and load hotbar and inventory layouts."},
};

// help lines: "<module>/<setting>"; settings not listed are clear from their name
Map const kHelp = {
    {"crosshair/style", "The shape of your crosshair"},
    {"crosshair/spread", "Opens up while you walk and jump, closes when you stand still"},
    {"crosshair/kick", "A small bounce every time you swing"},
    {"crosshair/target_tint", "Turns red when you aim at a player or mob"},
    {"crosshair/hit_marker", "An X flashes when a hit lands (gold for crits)"},
    {"crosshair/breathing", "A slow pulse while you're idle"},
    {"name_tags/range", "Players further away than this get no tag"},
    {"freelook/mode", "Hold: only while the key is down. Toggle: press to start/stop"},
    {"freelook/distance", "How far the camera orbits behind you (0 = from your eyes)"},
    {"zoom/factor", "How far you zoom in"},
    {"zoom/smooth", "Eases in and out instead of snapping"},
    {"cps_cap/limit", "Clicks above this per second are dropped"},
    {"motion_blur/strength", "How long the blur trails"},
    {"motion_blur/moving", "Blur when you move as well as when you turn"},
    {"aura/style", "The look of the light around you (see About above)"},
    {"aura/palette", "Accent follows your menu colour"},
    {"aura/speed", "How fast it moves (0 = still)"},
    {"aura/glow", "How bright the light is"},
    {"aura/first_person", "Also see it while in first person (it can get in the way)"},
    {"halo/height", "How high above your head it floats"},
    {"wings/design", "Seraph: feathered. Butterfly: painted. Prism: crystal"},
    {"wings/flap", "How much the wings beat (they beat harder when you jump)"},
    {"wings/first_person", "See the wing tips at the edge of your view in first person"},
    {"cape/design", "Silk: running sheen. Aurora: colour bands. Starfield: stars"},
    {"cape/sway", "How much it swings when you move and turn"},
    {"cyberspace/reality", "How much of the world turns digital (low = a hint, high = all of it)"},
    {"cyberspace/circuits", "Glowing circuit lines across the ground"},
    {"cyberspace/interval", "Seconds between scan lines sweeping the world"},
    {"cyberspace/sky", "Replace the sky with falling data"},
    {"cute_world/pastel", "How soft and pastel the colours get"},
    {"cute_world/softness", "Softens the light and shadows"},
    {"cute_world/hearts", "How many hearts drift through the air"},
    {"cute_world/sparkles", "How many little sparkles twinkle"},
    {"creamy_keys/profile", "Silk: soft. Stone: deep. Glass: bright and clicky"},
    {"nick_hider/alias", "What shows instead of your name"},
    {"post_processing/ca", "Colour fringe: red and blue edges like a camera lens"},
    {"post_processing/ca_style", "Edges: only toward the screen edges. Toward centre: everywhere"},
    {"pvp_preset/restore_smooth", "The Smooth Lighting setting to go back to when the preset is off"},
    {"time_changer/time", "0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight"},
    {"fullbright/strength", "How bright dark places get"},
    {"auto_gg/delay", "Seconds to wait after the game ends"},
    {"auto_requeue/ondeath", "Re-queue as soon as you die instead of at the end"},
    {"sky/mode", "Which sky owns the screen"},
    {"sky/style", "One Living Sky, or Shuffle through all of them"},
    {"sky/every", "On Shuffle: minutes before the next sky sweeps in"},
    {"sky/music", "Pulses with the music when something is playing"},
    {"sky/day", "Keeps the sky up in daytime too (off: it comes out at night)"},
    {"knives/model", "Which knife your sword becomes"},
    {"knives/auto_inspect", "Plays tricks with the blade when you stand still"},
    {"client_ui/style", "Prism: frosted glass with a soft sheen. Glass: the clear classic look"},
    {"client_ui/scale", "Makes the whole menu bigger or smaller (applies when you let go)"},
    {"client_ui/dim", "Darkens the world behind the menu so the menu is easier to read"},
    {"client_ui/stars", "Twinkling stars in the menu's background"},
    {"client_ui/cascade", "Rows rise in one after another when a page opens"},
    {"client_ui/reduce_motion", "Everything appears at once, no animations"},
    {"client_ui/blur", "How much the glass frosts what's behind it"},
    {"client_ui/refraction", "How much the glass edges bend the world behind"},
    {"client_ui/frost", "A violet haze on panels and buttons"},
};
}   // namespace

/// The "About" text of a module (empty if none).
std::string const& moduleAbout(std::string const& id) {
    static std::string const none;
    auto it = kAbout.find(id);
    return it == kAbout.end() ? none : it->second;
}
/// The help line under a setting (empty if its name says it all).
std::string const& settingHelp(std::string const& mod, std::string const& setting) {
    static std::string const none;
    auto it = kHelp.find(mod + "/" + setting);
    return it == kHelp.end() ? none : it->second;
}
}   // namespace bedrock_forge::client
