// LyricsFlow port - Bedrock Forge module: wires the core (lyrics/core) to the game and Windows.
// Original code for Bedrock Forge; behaviour follows LyricsFlow v2.0 (374m, MIT) for Fernando's personal build.
//
// STAGED, NOT RUN IN GAME YET. Compile-verified by CI only. Off by default: the "lyricsflow" module (Visual) must be
// switched on in the ClickGUI, and nothing is drawn unless Renderer Foundation's lyrics pass (rf-scaffold branch)
// pulls BF_CopyLyricFrame.
//
// Threads:  media thread (250 ms SMTC poll -> SongClock)  |  lyric fetch thread per song (LRCLIB/NetEase/Musixmatch)
//           HUD frame (render thread): camera + world -> Engine::frame -> publish for RF
#ifdef _WIN32
#include "lyrics/win/Win.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/input/KeyRegistry.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/world/actor/Actor.h"

#include "bf/Events.h"
#include "bf/Hud.h"
#include "client/Menu.h"
#include "lyrics/core/Panel.h"

#include "bf/Input.h"
#include "client/Module.h"
#include "client/PostFx.h"
#include "lyrics/core/Engine.h"
#include "lyrics/win/World_win.h"

namespace bf::lyrics::win {

namespace {
using Clock = std::chrono::steady_clock;
int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

struct State {
    std::mutex              mutex; // guards everything below except the atomics
    Engine                  engine;
    SongClock               clock;
    std::shared_ptr<Lyrics const> lyrics;
    LyricsState             lyricsState = LyricsState::None;
    std::filesystem::path   cfgFile, presetDir, cacheDir;
    std::atomic<bool>       running{false};
    std::thread             media;
    std::unique_ptr<Smtc>   smtc;
    int64_t                 lastFrameNs = 0;
    std::atomic<bool>       moduleOn{false}; // written on the game thread, read by the media thread + key callbacks

    // ---- settings panel (L): see the "panel" section below
    Panel                         panel;
    PanelAssets                   panelAssets;
    std::vector<PresetInfo>       presets;
    std::map<std::string, std::string> presetJson; // id -> file text
    std::vector<std::string>      fontAtlases;      // atlas files of the loaded fonts (indices 0..n-1)
    std::string                   uiAtlas;          // UI image atlas PNG
    std::vector<BfGlyphV1>        panelGlyphs;
    int64_t                       panelStepNs = 0;  // last panel step (the HUD and the dialog both may drive it)
    float                         lastFov = 70.0f;
    bool                          cfgDirty = false;
    int64_t                       savedNs = 0;
    // media thread work requested by the panel (run outside the mutex on the media thread)
    std::vector<PanelAction>      mediaCmds;
    // cover art: written by the media thread, picked up by the HUD frame (atlas list + panel asset)
    std::string                   coverPath, coverShown;
    int64_t                       coverSeq = -1;
    int                           coverTries = 0;
};
State* gS = nullptr;
std::atomic<int64_t> gCombatAtMs{0};

std::string readAll(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void saveConfig() {
    std::ofstream out(gS->cfgFile, std::ios::binary);
    out << toJson(gS->engine.config());
}

void loadConfig() {
    if (std::filesystem::exists(gS->cfgFile))
        if (auto j = json::parse(readAll(gS->cfgFile))) applyJson(gS->engine.config(), *j);
    saveConfig(); // writes new fields with defaults
}

void loadFonts(std::filesystem::path const& dir) {
    // RF has kBfLyricsMaxAtlases texture slots, so at most that many fonts: the configured lyric + top bar fonts
    // first, then the rest in name order (a 9th font used to get index 8 and sample past the atlas array).
    std::vector<std::filesystem::path> jsons;
    std::error_code                    ec;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") jsons.push_back(e.path());
    std::sort(jsons.begin(), jsons.end());
    std::string const want[2] = {gS->engine.config().font, gS->engine.config().topBarFont};
    std::stable_partition(jsons.begin(), jsons.end(), [&](auto const& p) { auto st = p.stem().string(); return st == want[0] || st == want[1]; });
    std::vector<std::string> atlases;
    for (auto const& j : jsons) {
        if (atlases.size() >= kBfLyricsMaxAtlases - 2) break; // + UI image atlas + cover art
        auto png = j;
        png.replace_extension(".png");
        if (!std::filesystem::exists(png, ec)) continue;
        if (gS->engine.fonts().loadFromJson(j.stem().string(), readAll(j), png.string())) atlases.push_back(png.string());
    }
    gS->fontAtlases = atlases;
}

/// Atlas list = fonts, then the UI image atlas, then the current cover art (panel assets follow the indices).
void publishAtlases() {
    std::vector<std::string> files = gS->fontAtlases;
    gS->panelAssets.uiAtlas = -1;
    gS->panelAssets.cover   = -1;
    if (!gS->uiAtlas.empty()) { gS->panelAssets.uiAtlas = int(files.size()); files.push_back(gS->uiAtlas); }
    if (!gS->coverShown.empty()) { gS->panelAssets.cover = int(files.size()); files.push_back(gS->coverShown); }
    FrameStore::get().setAtlases(files);
}

void loadUiAtlas(std::filesystem::path const& dir) {
    std::error_code ec;
    auto png = dir / "ui_atlas.png";
    if (!std::filesystem::exists(png, ec)) return;
    gS->uiAtlas = png.string();
    if (auto j = json::parse(readAll(dir / "ui_atlas.json")))
        for (auto const& [k, v] : (*j)["rects"].members())
            gS->panelAssets.ui[k] = UiRect{float(v[0].asNumber()), float(v[1].asNumber()), float(v[2].asNumber()), float(v[3].asNumber())};
}

void loadPresets() {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (auto const& e : std::filesystem::directory_iterator(gS->presetDir, ec))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto const& f : files) {
        std::string text = readAll(f), id = f.stem().string();
        auto        j    = json::parse(text);
        if (!j) continue;
        gS->presets.push_back({id, (*j)["name"].asString().empty() ? id : (*j)["name"].asString()});
        gS->presetJson[id] = text;
    }
}

/// Apply a look preset on top of fresh defaults, keeping the "plumbing" (source, sync, combat, top bar) the user set.
void applyPreset(std::string const& id) { // caller holds gS->mutex
    auto it = gS->presetJson.find(id);
    if (it == gS->presetJson.end()) return;
    auto j = json::parse(it->second);
    if (!j) return;
    Config const old = gS->engine.config();
    Config       c;
    c.enabled = old.enabled; c.source = old.source; c.syncOffsetMs = old.syncOffsetMs; c.songOffsets = old.songOffsets;
    c.combatSafe = old.combatSafe; c.keepOutDeg = old.keepOutDeg; c.combatAlpha = old.combatAlpha; c.combatHoldMs = old.combatHoldMs;
    c.showTopBar = old.showTopBar; c.topBarStyle = old.topBarStyle; c.topBarScale = old.topBarScale; c.mediaKeys = old.mediaKeys;
    c.skyPreset = old.skyPreset; c.stars = old.stars;
    applyJson(c, (*j)["settings"]);
    gS->engine.config() = c;
    gS->engine.reset(); // re-place the current line with the new look
    gS->cfgDirty = true;
}

// LyricsFlow MediaManager.fetchLyrics: up to 5 attempts, waiting 1.5 s * attempt, abandoned when the song changes.
void fetchFor(MediaSample s, int64_t seq) {
    std::thread([s, seq] { try {
        Track tk;
        tk.s = s;
        auto const cacheFile = gS->cacheDir / cacheFileName(tk.key());
        std::optional<Lyrics> got;
        if (std::error_code e1; std::filesystem::exists(cacheFile, e1)) got = fromCacheJson(readAll(cacheFile)); // LyricsFlow readCache
        for (int attempt = 1; attempt <= 5 && !got; ++attempt) {
            {
                std::lock_guard l(gS->mutex);
                if (gS->clock.songSeq() != seq || !gS->running) return;
            }
            try {
                got = fetch::fetch(&httpGet, s.artist, s.title, s.album, s.durationMs);
            } catch (...) { got.reset(); }
            if (got) break;
            auto until = Clock::now() + std::chrono::milliseconds(1500 * attempt);
            while (Clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard l(gS->mutex);
                if (gS->clock.songSeq() != seq || !gS->running) return;
            }
        }
        if (std::error_code e2; got && !std::filesystem::exists(cacheFile, e2)) { // LyricsFlow writeCache (synced results only reach here)
            std::ofstream out(cacheFile, std::ios::binary);
            out << toCacheJson(*got);
        }
        std::lock_guard l(gS->mutex);
        if (gS->clock.songSeq() != seq) return;
        gS->lyrics      = got ? std::make_shared<Lyrics const>(std::move(*got)) : nullptr;
        gS->lyricsState = got ? LyricsState::Found : LyricsState::NotFound;
    } catch (...) {} }).detach(); // filesystem / stream errors in a detached thread would std::terminate the game
}

void mediaLoop() {
    gS->smtc = std::make_unique<Smtc>();
    while (gS->running) {
        auto   started = Clock::now();
        Source src;
        {
            std::lock_guard l(gS->mutex);
            src = gS->engine.config().source;
        }
        std::vector<PanelAction> cmds;
        {
            std::lock_guard l(gS->mutex);
            cmds.swap(gS->mediaCmds);
        }
        for (auto const& c : cmds) { // player buttons on the settings panel (WinRT calls block: never on the game thread)
            if (c.kind == PanelAction::Kind::PlayPause) gS->smtc->playPause();
            else if (c.kind == PanelAction::Kind::Next) gS->smtc->next();
            else if (c.kind == PanelAction::Kind::Prev) gS->smtc->previous();
            else if (c.kind == PanelAction::Kind::Seek) gS->smtc->seek(c.ms);
        }
        auto sample = gS->moduleOn ? gS->smtc->poll(src) : std::nullopt;
        if (!cmds.empty()) {
            std::lock_guard l(gS->mutex);
            gS->clock.forceSync(); // jump the lyrics right away after a seek / skip
        }
        {
            std::lock_guard l(gS->mutex);
            for (auto e : gS->clock.poll(sample ? &*sample : nullptr, nowNs())) {
                if (e == MediaEvent::Song && sample) {
                    gS->lyrics      = nullptr;
                    gS->lyricsState = LyricsState::Searching;
                    fetchFor(*sample, gS->clock.songSeq());
                }
                if (e == MediaEvent::Stop) {
                    gS->lyrics      = nullptr;
                    gS->lyricsState = LyricsState::None;
                }
            }
        }
        // cover art for the panel: once per song, retried for a few seconds (apps often set it after the title)
        int64_t seq;
        bool    want;
        {
            std::lock_guard l(gS->mutex);
            seq  = gS->clock.songSeq();
            want = sample && gS->clock.hasTrack() && (gS->coverSeq != seq || (gS->coverPath.empty() && gS->coverTries < 20));
            if (gS->coverSeq != seq) { gS->coverSeq = seq; gS->coverTries = 0; gS->coverPath.clear(); }
            if (want) ++gS->coverTries;
        }
        if (want) {
            auto bytes = gS->smtc->thumbnail();
            if (!bytes.empty()) {
                auto file = gS->cacheDir / ("cover_" + std::to_string(seq % 2) + ".img"); // WIC sniffs PNG/JPEG itself
                { std::ofstream o(file, std::ios::binary | std::ios::trunc); o.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size())); }
                std::lock_guard l(gS->mutex);
                if (gS->clock.songSeq() == seq) gS->coverPath = file.string();
            }
        }
        std::this_thread::sleep_until(started + std::chrono::milliseconds(250));
    }
    gS->smtc.reset();
}

void panelStepAndPublish(int64_t now, bool lyricsActive); // settings panel section below

void hudFrame(bedrock_forge::client::Frame& f) {
    if (!gS || !f.player) return;
    gS->moduleOn = true; // hud only runs while the module is on (also after modules.json restored it)
    std::lock_guard l(gS->mutex);
    int64_t now = nowNs();
    float   dt  = gS->lastFrameNs ? float(double(now - gS->lastFrameNs) / 1e9) : 1.0f / 60.0f;
    gS->lastFrameNs = now;
    dt              = std::clamp(dt, 0.0f, 0.1f);

    Actor&      a = *f.player;
    auto        feet = a.getFeetPos();
    CameraState cam;
    cam.pos          = {feet.x, double(feet.y) + 1.62, feet.z}; // TODO(S1): third person / view bobbing -> RF reads the real camera
    cam.pitch        = a.getRotation().x;
    cam.yaw          = a.getRotation().y;
    cam.screenAspect = f.height > 0 ? f.width / f.height : 16.0f / 9.0f;
    BfPostFxV1 const* fx  = BF_GetPostFx();
    float             fov = fx ? fx->fov : 70.0f;

    SceneInput in;
    in.t           = gS->clock.position(now, gS->engine.config());
    in.playing     = gS->clock.playing();
    in.lyrics      = gS->lyrics.get();
    in.lyricsState = gS->lyricsState;
    in.songSeq     = gS->clock.songSeq();
    in.dt          = dt;
    in.nowSeconds  = double(now) / 1e9;
    in.title       = gS->clock.hasTrack() ? gS->clock.track().s.title : std::string();
    in.artist      = gS->clock.hasTrack() ? gS->clock.track().s.artist : std::string();
    in.guiW        = f.width;
    in.guiH        = f.height;
    { // Combat Safe: full right after a hit, easing out over combatHoldMs
        int64_t since = now / 1000000 - gCombatAtMs.load();
        int     hold  = std::max(1, gS->engine.config().combatHoldMs);
        in.combat     = since < hold ? 1.0f - float(since) / float(hold) * 0.5f : std::max(0.0f, 1.5f - float(since) / float(hold));
    }
    GameWorld world(a);
    gS->engine.frame(in, cam, &world);
    gS->lastFov = fov;
    panelStepAndPublish(now, true);
}


// ---------------------------------------------------------------- settings panel (L)
// Same trick as BF's ClickGUI: an empty native dialog gives us the mouse cursor and input focus; we cancel its UI
// render (nothing vanilla shows) and draw the panel ourselves through RF (BF_GLYPH_VIEW). The pointer comes from
// the dialog's ScreenView, the button state from mouse events (cancelled so the dialog never sees them).
namespace host {
std::shared_ptr<AbstractScene> scene;
IClientInstance*               client = nullptr;
Clock::time_point              openedAt{}, seen{};
glm::vec2                      ptr{0, 0}, size{0, 0};
bool                           ptrValid = false;
std::atomic<bool>              down{false};
bf::events::Subscriptions      subs;
std::atomic<IClientInstance*>  lastClient{nullptr};

bool ownsTop() { return scene && Clock::now() - seen < std::chrono::milliseconds(250); }
} // namespace host

void flushConfig(bool force) { // caller holds gS->mutex
    int64_t now = nowNs();
    if (gS->cfgDirty && (force || now - gS->savedNs > 500'000'000)) {
        saveConfig();
        gS->cfgDirty = false;
        gS->savedNs  = now;
    }
}

void closePanel() { // caller holds gS->mutex
    gS->panel.setOpen(false);
    if (host::scene && host::client) host::client->getSceneFactory().getCurrentSceneStack()->schedulePopScreen(1);
    host::scene.reset();
    host::ptrValid = false;
    host::down     = false;
    flushConfig(true);
}

void openPanel(IClientInstance& client) { // caller holds gS->mutex
    if (host::scene || bedrock_forge::client::menu::isOpen()) return;
    auto sc = client.getSceneFactory().createCommonDialogInfoScreen("LyricsFlow", "");
    if (!sc) return;
    host::scene    = sc;
    host::client   = &client;
    host::openedAt = Clock::now();
    host::seen     = {};
    host::down     = false;
    client.getSceneFactory().getCurrentSceneStack()->pushScreen(sc, false);
    gS->panel.setOpen(true);
}

PanelMedia panelMedia(int64_t now) { // caller holds gS->mutex
    PanelMedia m;
    if (gS->clock.hasTrack()) {
        auto const& t = gS->clock.track().s;
        m.hasTrack    = true;
        m.title = t.title; m.artist = t.artist; m.sourceApp = t.sourceApp;
        m.durationMs  = t.durationMs;
        m.positionMs  = std::max<int64_t>(0, gS->clock.positionRaw(now));
        m.playing     = gS->clock.playing();
    }
    m.lyricsState = gS->lyricsState == LyricsState::Found ? "synced" : gS->lyricsState == LyricsState::Searching ? "searching"
                  : gS->lyricsState == LyricsState::NotFound ? "not found" : "";
    return m;
}

/// One panel step + publish (lyrics glyphs + panel glyphs). Caller holds gS->mutex.
void panelStepAndPublish(int64_t now, bool lyricsActive) {
    if (gS->coverPath != gS->coverShown) { gS->coverShown = gS->coverPath; publishAtlases(); }
    gS->panelGlyphs.clear();
    if (gS->panel.visible()) {
        PanelInput in;
        in.dt         = gS->panelStepNs ? std::clamp(float(double(now - gS->panelStepNs) / 1e9), 0.0f, 0.1f) : 1.0f / 60.0f;
        in.now        = double(now) / 1e9;
        in.down       = host::down.load();
        in.mouseValid = host::ptrValid && host::size.x > 1 && host::size.y > 1;
        if (in.mouseValid) {
            in.aspect    = host::size.x / host::size.y;
            in.mouseNdcX = host::ptr.x / host::size.x * 2.0f - 1.0f;
            in.mouseNdcY = 1.0f - host::ptr.y / host::size.y * 2.0f;
        }
        auto actions = gS->panel.frame(in, gS->engine.config(), gS->engine.fonts(), gS->panelAssets, panelMedia(now), gS->presets, gS->panelGlyphs);
        for (auto const& a : actions) {
            switch (a.kind) {
            case PanelAction::Kind::Close: closePanel(); break;
            case PanelAction::Kind::SaveConfig: gS->cfgDirty = true; break;
            case PanelAction::Kind::ApplyPreset: applyPreset(a.id); break;
            case PanelAction::Kind::Refetch:
                if (gS->clock.hasTrack()) {
                    std::error_code ec;
                    Track tk = gS->clock.track();
                    std::filesystem::remove(gS->cacheDir / cacheFileName(tk.key()), ec);
                    gS->lyrics = nullptr;
                    gS->lyricsState = LyricsState::Searching;
                    fetchFor(tk.s, gS->clock.songSeq());
                }
                break;
            default: gS->mediaCmds.push_back(a); break; // play/pause/next/prev/seek -> media thread
            }
        }
        flushConfig(false);
    }
    gS->panelStepNs = now;
    gS->engine.publish(lyricsActive, gS->lastFov, gS->panelGlyphs.empty() ? nullptr : &gS->panelGlyphs);
}

void installPanelHost() {
    // L: open / close the panel (registered through the game's key registry, so it is rebindable in Controls)
    auto& key = ll::input::KeyRegistry::getInstance().getOrCreateKey("lyricsflow.panel", {0x4C /*L*/}, true);
    key.registerButtonDownHandler(
        [](::FocusImpact, ::IClientInstance& client) {
            if (!gS || !gS->moduleOn) return;
            std::lock_guard l(gS->mutex);
            if (gS->panel.open()) closePanel();
            else if (client.getLocalPlayer()) openPanel(client);
        },
        true);

    host::subs.on<ll::event::BeforeUIRenderEvent>([](ll::event::BeforeUIRenderEvent& ev) {
        if (!gS) return;
        std::lock_guard l(gS->mutex);
        if (!host::scene) return;
        auto const now = Clock::now();
        if (bf::hud::isHudScreen(ev.screenView())) {
            // only the HUD renders: the dialog went away (Esc closed it natively) -> close the panel
            if (now - host::openedAt > std::chrono::milliseconds(800) && now - host::seen > std::chrono::milliseconds(400)) {
                host::scene.reset();
                gS->panel.setOpen(false);
                host::ptrValid = false;
                flushConfig(true);
            }
            return;
        }
        host::seen     = now;
        host::size     = {ev.screenView().mSize->x, ev.screenView().mSize->y};
        host::ptr      = *ev.screenView().mPointerLocationPrevious;
        host::ptrValid = true;
        ev.cancel(); // hide the empty dialog; the panel is drawn by RF
        // If the HUD stops rendering under the dialog, keep the panel (and the last lyrics) alive from here.
        int64_t t = nowNs();
        if (t - gS->panelStepNs > 40'000'000) panelStepAndPublish(t, true);
    });
    host::subs.on<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        if (!gS || !host::ownsTop()) return;
        char const  a = ev.actionButtonId();
        schar const d = ev.buttonData();
        if (a == MouseAction::ActionLeft) {
            host::down = d == MouseAction::DataDown; // anything else on the left button = released
            ev.cancel();
        } else if (a == MouseAction::ActionRight || a == MouseAction::ActionMiddle || a == MouseAction::ActionWheel) ev.cancel();
    });
    host::subs.on<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
        if (!gS || !host::ownsTop()) return;
        // Esc (or L: the registry key is suspended while the dialog has focus) closes the panel and its dialog
        if (ev.keyCode() == 0x1B || ev.keyCode() == 0x4C) {
            ev.cancel();
            if (!ev.isDown()) return;
            std::lock_guard l(gS->mutex);
            closePanel();
        }
    });
}

void nudgeSongOffset(int deltaMs) { // LyricsFlow '[' / ']' : +-100 ms for the current song, capped at +-5 s
    std::lock_guard l(gS->mutex);
    if (!gS->clock.hasTrack()) return;
    auto& offs = gS->engine.config().songOffsets;
    auto  key  = gS->clock.track().key();
    int   v    = std::clamp((offs.count(key) ? offs[key] : 0) + deltaMs, -5000, 5000);
    if (v == 0) offs.erase(key);
    else offs[key] = v;
    saveConfig();
}
} // namespace

void install(std::filesystem::path const& modDir, std::filesystem::path const& configDir) {
    if (gS) return; // once per process
    gS            = new State();
    gS->cfgFile   = configDir / "lyricsflow.json";
    gS->presetDir = modDir / "resources" / "lyricsflow" / "presets";
    gS->cacheDir  = configDir / "lyricsflow_cache";
    std::error_code ec;
    std::filesystem::create_directories(gS->cacheDir, ec);
    loadConfig();
    loadFonts(modDir / "resources" / "lyricsflow" / "fonts");
    loadUiAtlas(modDir / "resources" / "lyricsflow" / "ui");
    loadPresets();
    publishAtlases();
    installPanelHost();

    auto& m = bedrock_forge::client::Registry::get().add("lyricsflow", "LyricsFlow", bedrock_forge::client::Category::Visual,
                                                         "Synced song lyrics in the world (port of LyricsFlow by 374m) - PREVIEW, untested in game");
    m.hud      = [](bedrock_forge::client::Frame& f) { hudFrame(f); };
    m.onToggle = [](bool on) {
        gS->moduleOn = on;
        if (!on) {
            std::lock_guard l(gS->mutex);
            if (gS->panel.open()) closePanel();
            gS->engine.reset();
            gS->engine.publish(false, 70.0f);
        }
    };

    // Keys: LyricsFlow's K / J are taken by BF (impact frames / HUD), so only on/off + per-song offset for now.
    bf::input::bindKey("lyricsflow.toggle", 0x4F /*O*/, [] {
        if (!gS->moduleOn) return; // module off: the keys do nothing (used to flip + save "enabled" silently)
        std::lock_guard l(gS->mutex);
        gS->engine.config().enabled = !gS->engine.config().enabled;
        if (!gS->engine.config().enabled) gS->engine.reset();
        saveConfig();
    });
    bf::input::bindKey("lyricsflow.earlier", 0xDD /*]*/, [] { if (gS->moduleOn) nudgeSongOffset(+100); });
    bf::input::bindKey("lyricsflow.later", 0xDB /*[*/, [] { if (gS->moduleOn) nudgeSongOffset(-100); });

    gS->running = true;
    gS->media   = std::thread(mediaLoop);
}

void noteCombat() {
    gCombatAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void uninstall() {
    if (!gS) return;
    host::subs.clear();
    gS->running = false;
    if (gS->media.joinable()) gS->media.join();
    {
        std::lock_guard l(gS->mutex);
        saveConfig();
        gS->engine.publish(false, 70.0f);
    }
    // State stays allocated on purpose: detached fetch threads may still be finishing and check gS->running.
}

} // namespace bf::lyrics::win
#endif
