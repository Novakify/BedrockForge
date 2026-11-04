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
#include <cmath>
#include <utility>
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
#include "core/Win32Input.h"
#include "client/Menu.h"
#include "lyrics/core/Panel.h"

#include "bf/Input.h"
#include "client/Module.h"
#include "client/PostFx.h"
#include "lyrics/core/Engine.h"
#include "lyrics/core/Palette.h"
#include "lyrics/core/SkySystem.h"
#include "lyrics/core/SongMemory.h"
#include "lyrics/core/Conductor.h"
#include "lyrics/core/Studio.h"
#include "lyrics/win/World_win.h"

namespace bf::lyrics::win {

namespace {
using Clock = std::chrono::steady_clock;
int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

struct PresetInfo { std::string id, name; }; // look presets (resources/lyricsflow/presets)

struct State {
    std::mutex              mutex; // guards everything below except the atomics
    Engine                  engine;
    SongClock               clock;
    std::shared_ptr<Lyrics const> lyrics;
    LyricsState             lyricsState = LyricsState::None;
    std::filesystem::path   cfgFile, presetDir, cacheDir, memoryDir;
    SongMemory              memory;         // [BF] Song Memory of the song playing now (key empty = none)
    Conductor               conductor;      // [BF] music state for the Director (game thread, under mutex)
    struct Demo {                           // [BF] Director demo: replays a known song silently (config/lyricsflow_demo.json)
        bool                          on = false;
        std::string                   artist, title, stamp;
        std::shared_ptr<Lyrics const> lyrics;
        SongMemory                    mem;
        int64_t                       startNs = 0, startMs = 0, seq = 1000000, durMs = 0;
        float                         speed = 1, prevBass = 0;
    } demo;
    std::vector<std::string> directorLines;  // [BF] Director events for logs/director.txt (written by the media thread)
    int64_t                 memoryHooksSeq = -1; // song whose lyrics the memory's hooks were found from
    std::map<std::string, std::string> songScenes; // [BF] track key -> Scene Studio scene (config/lyricsflow_songscenes.json)
    std::string             lastScene;      // cfg.scene seen last frame (a change while a song plays = remember it)
    bool                    songScenesDirty = false;
    int64_t                 memorySeq = -1; // song the memory belongs to (clock.songSeq)
    std::string             memoryTitle;
    std::shared_ptr<Lyrics const> memoryLyrics; // this song's lyrics (gS->lyrics is already cleared when the song ends)
    int64_t                 memorySongNs = 0;   // when the current song began (its reported length settles after ~3 s)
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
    std::vector<std::string>      fontAtlases;      // atlas files of the slotted fonts (indices 0..n-1)
    std::map<std::string, std::string> fontPng;     // every font: id -> atlas PNG
    std::vector<std::string>      slotFont;         // font in each atlas slot
    std::vector<uint64_t>         slotUsed;         // last time each slot's font was wanted
    uint64_t                      slotClock = 0;
    std::string                   uiAtlas;          // UI image atlas PNG
    std::string                   iconAtlas;        // [BF] client menu icons (resources/client/icons.png)
    int                           iconAtlasIndex = -1;
    std::map<std::string, UiRect> iconRects;
    std::vector<BfGlyphV1>        panelGlyphs;
    Vec3                          camPos;           // last HUD-frame camera (panel placement + crosshair)
    float                         camYaw = 0, camPitch = 0;
    float                         fpsSmooth = 0;
    bool                          haveCam = false;
    IClientInstance*              openMenuOn = nullptr; // "Open full menu": open BF's menu after the mutex is released
    int64_t                       panelStepNs = 0;  // last panel step (the HUD and the dialog both may drive it)
    float                         lastFov = 70.0f;
    bool                          cfgDirty = false;
    int64_t                       savedNs = 0;
    // media thread work requested by the panel (run outside the mutex on the media thread)
    std::vector<PanelAction>      mediaCmds;
    // cover art: written by the media thread, picked up by the HUD frame (atlas list + panel asset)
    std::string                   coverPath, coverShown;
    uint32_t                      coverAccent = 0; // [BF] vivid colour of the cover (Neon player)
    float                         bass = 0;        // [BF] bass hit level this frame (Audio_win.cpp)
    bool                          bassLive = false;
    int64_t                       coverSeq = -1;
    int                           coverTries = 0;
    // live sky (LyricsFlow 2.1 WorldSky): published every HUD frame for RF (BF_CopySkyFrame)
    sky::System                   sky;
    std::filesystem::path         skyDir;           // "My picture" folder (<config>/lyricsflow_sky)
    std::string                   spritesFile, pictureFile;
    int64_t                       pictureListedMs = -10000;
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
    // [BF] per-song scenes
    auto const ss = gS->cfgFile.parent_path() / "lyricsflow_songscenes.json";
    if (std::filesystem::exists(ss))
        if (auto j = json::parse(readAll(ss)); j && j->isObject())
            for (auto const& [k, v] : j->members()) if (v.isString()) gS->songScenes[k] = v.asString();
    gS->lastScene = gS->engine.config().scene;
}

void saveSongScenes() { // media thread, without gS->mutex: a copy is written
    std::map<std::string, std::string> copy;
    {
        std::lock_guard l(gS->mutex);
        if (!gS->songScenesDirty) return;
        gS->songScenesDirty = false;
        copy = gS->songScenes;
    }
    auto esc = [](std::string const& x) {
        std::string o;
        for (char c : x) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (uint8_t(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", unsigned(uint8_t(c))); o += b; }
            else o += c;
        }
        return o;
    };
    std::string out = "{\n";
    bool first = true;
    for (auto const& [k, v] : copy) { out += (first ? "  \"" : ",\n  \"") + esc(k) + "\": \"" + esc(v) + "\""; first = false; }
    out += "\n}\n";
    std::ofstream f(gS->cfgFile.parent_path() / "lyricsflow_songscenes.json", std::ios::binary);
    f << out;
}

// [BF] Fonts: every font's metrics are loaded, but RF has kBfLyricsMaxAtlases texture slots (fonts + UI atlas + cover
// art), so only the fonts in use get a slot. syncFontSlots() keeps the lyric / top bar / panel fonts slotted and
// evicts the least recently used one when another font is picked (the atlas list is republished, RF reloads it).
constexpr size_t kFontSlots = kBfLyricsMaxAtlases - 3; // fonts + UI atlas + client icon atlas + cover art
void publishAtlases();

void loadFonts(std::filesystem::path const& dir) {
    std::vector<std::filesystem::path> jsons;
    std::error_code                    ec;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") jsons.push_back(e.path());
    std::sort(jsons.begin(), jsons.end());
    for (auto const& j : jsons) {
        auto png = j;
        png.replace_extension(".png");
        if (!std::filesystem::exists(png, ec)) continue;
        if (gS->engine.fonts().loadFromJson(j.stem().string(), readAll(j), png.string())) {
            gS->engine.fonts().setAtlasIndex(j.stem().string(), FontSet::kNoAtlas);
            gS->fontPng[j.stem().string()] = png.string();
        }
    }
}

/// Make sure the fonts in use have atlas slots. Returns true when the atlas list changed. Caller holds gS->mutex.
bool syncFontSlots() {
    auto const& cfg = gS->engine.config();
    std::string const want[] = {cfg.font, cfg.topBarFont, gS->panelAssets.pixelFont, "minecraft", "outfit", "outfit_bold"}; // [BF] Outfit = client menu + player card
    bool changed = false;
    ++gS->slotClock;
    for (auto const& id : want) {
        if (!gS->fontPng.count(id)) continue;
        auto it = std::find(gS->slotFont.begin(), gS->slotFont.end(), id);
        if (it != gS->slotFont.end()) { gS->slotUsed[size_t(it - gS->slotFont.begin())] = gS->slotClock; continue; }
        size_t slot;
        if (gS->slotFont.size() < kFontSlots) {
            slot = gS->slotFont.size();
            gS->slotFont.push_back(id);
            gS->slotUsed.push_back(gS->slotClock);
        } else { // evict the least recently wanted font that isn't wanted right now
            slot = 0;
            for (size_t i = 1; i < gS->slotFont.size(); ++i)
                if (gS->slotUsed[i] < gS->slotUsed[slot]) slot = i;
            gS->engine.fonts().setAtlasIndex(gS->slotFont[slot], FontSet::kNoAtlas);
            gS->engine.forgetAtlas(uint32_t(slot)); // letters lying around in the old font would sample the new atlas
            gS->slotFont[slot] = id;
            gS->slotUsed[slot] = gS->slotClock;
        }
        gS->engine.fonts().setAtlasIndex(id, uint32_t(slot));
        changed = true;
    }
    if (changed) {
        gS->fontAtlases.clear();
        for (auto const& f : gS->slotFont) gS->fontAtlases.push_back(gS->fontPng[f]);
        publishAtlases();
    }
    return changed;
}

/// Atlas list = fonts, then the UI image atlas, then the current cover art (panel assets follow the indices).
void publishAtlases() {
    std::vector<std::string> files = gS->fontAtlases;
    gS->panelAssets.uiAtlas = -1;
    gS->panelAssets.cover   = -1;
    if (!gS->uiAtlas.empty()) { gS->panelAssets.uiAtlas = int(files.size()); files.push_back(gS->uiAtlas); }
    gS->iconAtlasIndex = -1;
    if (!gS->iconAtlas.empty()) { gS->iconAtlasIndex = int(files.size()); files.push_back(gS->iconAtlas); }
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
[[maybe_unused]] void applyPreset(std::string const& id) { // caller holds gS->mutex (not on the 2.1 panel; kept for a presets UI)
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


// ---------------------------------------------------------------- [BF] Song Memory (media thread, see core/SongMemory.h)
// Every 50 ms band frame from the loopback is stamped with the song position and recorded; the play is merged into the
// song's saved map when the song changes or stops. logs/song_memory.txt gets a readable summary per song.
void appendLog(char const* name, std::string const& text) {
    try {
        auto const      logDir = gS->cfgFile.parent_path().parent_path() / "logs";
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        auto const file = logDir / (std::string(name) + ".txt");
        if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) > 256 * 1024)
            std::filesystem::rename(file, logDir / (std::string(name) + ".prev.txt"), ec);
        std::ofstream o(file, std::ios::binary | std::ios::app);
        o << text;
    } catch (...) {}
}
void songMemoryLog(std::string const& text) { appendLog("song_memory", text); }
// Merge + save + log a finished play. Runs WITHOUT gS->mutex (file I/O), on a SongMemory moved out of gS.
void finishPlay(SongMemory& m, std::shared_ptr<Lyrics const> const& lyrics, std::string const& title) {
    if (m.key().empty()) return;
    int64_t const heard   = m.heardThisPlayMs();
    bool const    counted = m.endPlay();
    if (counted) m.save(gS->memoryDir / SongMemory::fileNameFor(m.key()));
    m.analyze(lyrics.get());
    songMemoryLog("== " + title + "  (heard " + std::to_string(heard / 1000) + " s this play" +
                  (counted ? ", saved)\n" : ", too short to save)\n") + m.describe() + "\n");
}
void finishSongMemory() { // gS->mutex held - only at shutdown, where a short wait doesn't matter
    SongMemory m = std::move(gS->memory);
    gS->memory   = SongMemory();
    finishPlay(m, gS->memoryLyrics ? gS->memoryLyrics : gS->lyrics, gS->memoryTitle);
}
void songMemoryStep() {
    std::vector<BandFrame> frames;
    audioFrames(frames); // its own lock: never inside gS->mutex
    SongMemory                    done;
    std::shared_ptr<Lyrics const> doneLyrics;
    std::string                   doneTitle, loadKey;
    int64_t                       loadDur = 0;
    {
        std::lock_guard l(gS->mutex);
        auto const& cfg = gS->engine.config();
        bool const  on  = gS->moduleOn && cfg.songMemory;
        memoryWanted((on || (gS->moduleOn && cfg.director)) && gS->clock.playing()); // the Director reads the bands too
        int64_t const seq = gS->clock.hasTrack() ? gS->clock.songSeq() : -1;
        if (seq != gS->memorySeq) {
            if (!gS->memory.key().empty()) {
                done       = std::move(gS->memory);
                gS->memory = SongMemory();
                doneLyrics = gS->memoryLyrics;
                doneTitle  = gS->memoryTitle;
            }
            gS->memoryLyrics = nullptr;
            gS->memorySeq    = seq;
            gS->memorySongNs = nowNs();
        }
        if (gS->lyrics) gS->memoryLyrics = gS->lyrics; // the fetch for THIS song (cleared on every song change)
        if (gS->memoryLyrics && gS->memoryHooksSeq != seq && !gS->memory.key().empty()) {
            gS->memory.analyze(gS->memoryLyrics.get()); // hooks from repeated lines: choruses even on a first play
            gS->memoryHooksSeq = seq;
        }
        if (on && seq >= 0) {
            auto const& tr = gS->clock.track();
            if (!gS->memory.key().empty() && tr.s.durationMs > 0 && nowNs() - gS->memorySongNs > 3000000000LL)
                gS->memory.setDuration(tr.s.durationMs); // the app's length settles a few seconds into the song
            if (gS->memory.key().empty()) {
                loadKey = SongMemory::keyFor(tr.key(), tr.s.durationMs);
                loadDur = tr.s.durationMs;
                gS->memory.reset(loadKey, loadDur); // recording starts now; the saved map is adopted below
                gS->memoryTitle = tr.s.artist + " - " + tr.s.title;
            }
            if (!gS->memory.key().empty() && gS->clock.playing())
                for (auto const& f : frames) gS->memory.record(gS->clock.position(f.tNs, cfg), f);
        }
    }
    finishPlay(done, doneLyrics, doneTitle);
    if (!loadKey.empty()) {
        SongMemory saved;
        saved.reset(loadKey, loadDur);
        bool const known = saved.load(gS->memoryDir / SongMemory::fileNameFor(loadKey));
        std::string title;
        if (known) {
            std::lock_guard l(gS->mutex);
            if (gS->memory.key() == loadKey) {
                gS->memory.adoptMap(std::move(saved));
                gS->memory.analyze(gS->lyrics.get());
            }
            title = gS->memoryTitle;
        }
        if (known) songMemoryLog("-- " + title + ": known song, map loaded\n");
    }
}

// [BF] Director demo (media thread): config/lyricsflow_demo.json = {"artist": "...", "title": "...", "startMs": 0,
// "speed": 1} replays that song's cached lyrics against its Song Memory map, with no music playing - so the Director
// can be checked (and tuned) remotely. Delete the file to stop. Also flushes the Director log.
void demoStep() {
    static int64_t lastCheck = 0;
    std::vector<std::string> lines;
    {
        std::lock_guard l(gS->mutex);
        lines.swap(gS->directorLines);
    }
    auto const logDir = gS->cfgFile.parent_path().parent_path() / "logs";
    if (!lines.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        std::ofstream out(logDir / "director.txt", std::ios::app | std::ios::binary);
        for (auto const& x : lines) out << x << "\n";
    }
    int64_t const now = nowNs();
    if (now - lastCheck < 1000000000LL) return;
    lastCheck = now;
    auto const file = gS->cfgFile.parent_path() / "lyricsflow_demo.json";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        std::lock_guard l(gS->mutex);
        gS->demo.on = false;
        return;
    }
    std::string const text = readAll(file);
    {
        std::lock_guard l(gS->mutex);
        if (gS->demo.on && gS->demo.stamp == text) return;
    }
    auto j = json::parse(text);
    if (!j || !j->isObject()) return;
    std::string const artist = (*j)["artist"].asString(), title = (*j)["title"].asString();
    std::string const key    = text::toLower(artist + std::string(1, '\0') + title);
    std::optional<Lyrics> lyr;
    if (auto cf = gS->cacheDir / cacheFileName(key); std::filesystem::exists(cf, ec)) lyr = fromCacheJson(readAll(cf));
    SongMemory mem;
    std::string const mkey = SongMemory::keyFor(key);
    mem.reset(mkey, 0);
    bool const known = mem.load(gS->memoryDir / SongMemory::fileNameFor(mkey));
    if (known) mem.analyze(lyr ? &*lyr : nullptr);
    std::lock_guard l(gS->mutex);
    auto& d   = gS->demo;
    d.on      = lyr.has_value() || known;
    d.stamp   = text;
    d.artist  = artist;
    d.title   = title;
    d.lyrics  = lyr ? std::make_shared<Lyrics const>(std::move(*lyr)) : nullptr;
    d.mem     = std::move(mem);
    d.startMs = int64_t((*j)["startMs"].asNumber(0));
    d.speed   = float(std::clamp((*j)["speed"].asNumber(1), 0.25, 4.0));
    d.startNs = now;
    d.durMs   = d.mem.durationMs() > 0 ? d.mem.durationMs() : 240000;
    d.seq++;
    gS->directorLines.push_back("== demo " + artist + " - " + title + (known ? " (memory: " + std::to_string(d.mem.events.size()) + " events, " + std::to_string(d.mem.hooks.size()) + " hooks)" : " (no memory)") + (d.lyrics ? "" : " (no cached lyrics)"));
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
            else if (c.kind == PanelAction::Kind::Repeat) gS->smtc->cycleRepeat();
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
                    auto& cfg = gS->engine.config(); // [BF] this song's remembered scene comes back
                    if (cfg.songScenes)
                        if (auto it = gS->songScenes.find(gS->clock.track().key()); it != gS->songScenes.end())
                            if (auto sc = findScene(it->second); sc && cfg.scene != sc->id) {
                                applyScene(cfg, *sc);
                                gS->lastScene = cfg.scene;
                                saveConfig();
                            }
                }
                if (e == MediaEvent::Stop) {
                    gS->lyrics      = nullptr;
                    gS->lyricsState = LyricsState::None;
                }
            }
        }
        songMemoryStep();
        saveSongScenes();
        demoStep();
        // [BF] timing log (mods/BedrockForgeCore/logs/lyrics_clock.txt, ~1 line/s, this song + lyrics_clock.prev.txt):
        // what Windows reports vs our song clock, so a lyrics delay can be traced to the app or to the lyrics.
        // The line is built under the lock; the file is written AFTER it (the game thread waits on gS->mutex every
        // frame, and an antivirus scan of the growing file must never stall a frame).
        try {
            static int     tick = 0;
            static int64_t logSeq = -1;
            auto const     logDir = gS->cfgFile.parent_path().parent_path() / "logs";
            std::string    header, line;
            {
                std::lock_guard l(gS->mutex);
                int64_t const seqNow = gS->clock.songSeq();
                if (seqNow != logSeq && sample) {
                    logSeq = seqNow;
                    header = "song: " + sample->artist + " - " + sample->title + "  app: " + sample->sourceApp + "  offset: " +
                             std::to_string(gS->engine.config().syncOffsetMs) +
                             " ms\n# wallMs playing rawPos(app) extrapolated(app) updatedAgeMs ourClock(ourClock-app) lyrics\n";
                }
                if (sample && (++tick % 4) == 0) {
                    int64_t const nowFt = int64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch()).count() * 10 + 116444736000000000LL);
                    int64_t const age   = sample->updatedFt > 0 ? (nowFt - sample->updatedFt) / 10000 : -1;
                    int64_t const ours  = gS->clock.positionRaw(nowNs());
                    line = std::to_string(nowNs() / 1000000) + ' ' + (sample->playing ? "1" : "0") + ' ' + std::to_string(sample->rawPositionMs) + ' ' +
                           std::to_string(sample->positionMs) + ' ' + std::to_string(age) + ' ' + std::to_string(ours) + " (" +
                           std::to_string(ours - sample->positionMs) + ") " +
                           (gS->lyrics ? gS->lyrics->source + (gS->lyrics->wordSynced ? " words" : " lines") : std::string("-")) + '\n';
                }
            }
            if (!header.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(logDir, ec);
                std::filesystem::rename(logDir / "lyrics_clock.txt", logDir / "lyrics_clock.prev.txt", ec);
                std::ofstream o(logDir / "lyrics_clock.txt", std::ios::binary | std::ios::trunc);
                o << header;
            }
            if (!line.empty()) {
                std::ofstream o(logDir / "lyrics_clock.txt", std::ios::binary | std::ios::app);
                o << line;
            }
        } catch (...) {}
        // cover art for the panel: once per song, retried for a few seconds (apps often set it after the title)
        int64_t seq;
        bool    want;
        {
            std::lock_guard l(gS->mutex);
            seq  = gS->clock.songSeq();
            want = sample && gS->clock.hasTrack() && (gS->coverSeq != seq || (gS->coverPath.empty() && gS->coverTries < 20));
            if (gS->coverSeq != seq) { gS->coverSeq = seq; gS->coverTries = 0; gS->coverPath.clear(); gS->coverAccent = 0; }
            if (want) ++gS->coverTries;
        }
        if (want) {
            auto bytes = gS->smtc->thumbnail();
            if (!bytes.empty()) {
                auto file = gS->cacheDir / ("cover_" + std::to_string(seq % 2) + ".img"); // WIC sniffs PNG/JPEG itself
                { std::ofstream o(file, std::ios::binary | std::ios::trunc); o.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size())); }
                auto const     small  = decodeSmallRgba(bytes, 48); // [BF] Neon player: the cover's own colour
                uint32_t const accent = small.empty() ? 0u : coverAccent(small.data(), 48, 48, 0);
                std::lock_guard l(gS->mutex);
                if (gS->clock.songSeq() == seq) { gS->coverPath = file.string(); gS->coverAccent = accent; }
            }
        }
        std::this_thread::sleep_until(started + std::chrono::milliseconds(250));
    }
    gS->smtc.reset();
}

void    panelStepAndPublish(int64_t now, bool lyricsActive); // world panel section below
int64_t nowMsWall();

namespace host {
std::atomic<bool>             click{false}, down{false};
bf::events::Subscriptions     subs;
std::atomic<IClientInstance*> client{nullptr};
std::atomic<bool>             hovered{false}, open{false};
} // namespace host
void togglePanel();
void nudgeSongOffset(int deltaMs);

/// Keys, polled once per HUD frame (edge-triggered, only with the game focused and the HUD screen on top, so typing
/// in chat never triggers them): L panel, O lyrics on/off, ] / [ timing for this song. Same approach as BF's menu key.
void pollKeys(bedrock_forge::client::Frame& f) {
    namespace w32 = bedrock_forge::win32;
    if (!bf::hud::isHudScreen(f.view) || !f.player) return; // other views must not reset the key state
    static bool    was[4]  = {};
    static int64_t last[4] = {};
    int const      vks[4]  = {0x4C /*L*/, 0x4F /*O*/, 0xDD /*]*/, 0xDB /*[*/};
    bool const     focused = w32::gameFocused();
    int64_t const  ms      = nowMsWall();
    for (int i = 0; i < 4; ++i) {
        bool down = focused && w32::keyDown(vks[i]);
        bool edge = down && !was[i] && ms - last[i] > 150;
        was[i]    = down;
        if (edge) last[i] = ms;
        if (!edge) continue;
        if (i == 0) {
            host::client = &f.client;
            std::lock_guard l(gS->mutex);
            togglePanel();
        } else if (i == 1) {
            std::lock_guard l(gS->mutex);
            gS->engine.config().enabled = !gS->engine.config().enabled;
            if (!gS->engine.config().enabled) gS->engine.reset();
            saveConfig();
        } else {
            nudgeSongOffset(i == 2 ? +100 : -100);
        }
    }
}

/// LyricsFlow 2.1 live sky: clock + stars / weather / picture -> BF_CopySkyFrame. Caller holds gS->mutex.
void skyStep(Actor& a, CameraState const& cam, float dt, GameWorld& world) {
    auto const& cfg = gS->engine.config();
    int64_t     ms  = nowMsWall();
    if (cfg.skyImage && ms - gS->pictureListedMs > 3000) { // SkyImage.chosen(): folder listing cached 3 s
        gS->pictureListedMs = ms;
        std::vector<std::string> files;
        std::error_code          ec;
        for (auto it = std::filesystem::directory_iterator(gS->skyDir, ec); !ec && it != std::filesystem::directory_iterator();
             it.increment(ec))
            if (it->is_regular_file(ec)) files.push_back(it->path().filename().string());
        auto pick       = sky::pictureChoice(cfg, files);
        gS->pictureFile = pick.empty() ? std::string() : (gS->skyDir / pick).string();
    } else if (!cfg.skyImage) {
        gS->pictureFile.clear();
    }
    sky::SystemIn in;
    in.nowMs = ms;
    in.dtSec = dt * std::clamp(gS->conductor.state().timeScale, 0.25f, 2.5f); // [BF] the sky rides the speed ramp too
    in.yaw = cam.yaw; in.pitch = cam.pitch;
    in.camX = cam.pos.x; in.camY = cam.pos.y; in.camZ = cam.pos.z;
    in.overworld = static_cast<int>(a.getDimensionId()) == 0;
    auto feet    = a.getFeetPos();
    in.skyOpen   = world.canSeeSky(BlockPos::of(Vec3{feet.x, double(feet.y) + 1.0, feet.z})); // player.blockPos().above()
    in.night     = 1.0f; // TODO: level day time (only matters with Day/night dimming and "keep sky by day" off)
    in.playing   = gS->clock.playing();
    in.beat      = gS->bassLive ? std::max(gS->bass, gS->conductor.state().impact) : 0.0f; // [BF] sky breathes with the music
    in.spritesFile = gS->spritesFile;
    in.pictureFile = gS->pictureFile;
    gS->sky.step(gS->moduleOn && cfg.enabled, cfg, gS->engine.perf(), in);
}

std::atomic<bool> gPanelRequest{false}; // [BF] the client menu's "Open LyricsFlow panel" (opened on the next HUD frame)

void hudFrame(bedrock_forge::client::Frame& f) {
    if (!gS || !f.player) return;
    gS->moduleOn = true; // hud only runs while the module is on (also after modules.json restored it)
    pollKeys(f);
    if (gPanelRequest.exchange(false) && bf::hud::isHudScreen(f.view)) {
        host::client = &f.client;
        std::lock_guard l(gS->mutex);
        if (!gS->panel.isOpen()) togglePanel();
    }
    {
        std::lock_guard l(gS->mutex);
        syncFontSlots();
    }
    std::lock_guard l(gS->mutex);
    int64_t now = nowNs();
    float   dt  = gS->lastFrameNs ? float(double(now - gS->lastFrameNs) / 1e9) : 1.0f / 60.0f;
    gS->lastFrameNs = now;
    dt              = std::clamp(dt, 0.0f, 0.1f);
    { // [BF] bass hits: only listen while something uses them
        auto const& c = gS->engine.config();
        bassWanted(c.playerStyle == 2 || c.beatGlow || c.fxBeatShake || c.director);
        gS->bass = bassLevel(dt, &gS->bassLive);
    }

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
    in.beat        = gS->bassLive ? gS->bass : 0.0f;
    in.beatLive    = gS->bassLive;
    { // Combat Safe: full right after a hit, easing out over combatHoldMs
        int64_t since = now / 1000000 - gCombatAtMs.load();
        int     hold  = std::max(1, gS->engine.config().combatHoldMs);
        in.combat     = since < hold ? 1.0f - float(since) / float(hold) * 0.5f : std::max(0.0f, 1.5f - float(since) / float(hold));
    }
    { // [BF] the Conductor: one music state for the Director (lyrics now, sky / grade through RF next)
        ConductorIn ci;
        ci.bandsLive = gS->bassLive;
        ci.bands     = gS->bassLive ? audioLatest() : BandFrame{};
        ci.memory    = gS->memory.key().empty() ? nullptr : &gS->memory;
        if (auto& d = gS->demo; d.on) { // Director demo: a known song replayed from its memory map, no music needed
            int64_t t = d.startMs + int64_t(double(now - d.startNs) / 1e6 * d.speed);
            if (t > d.durMs) { d.startNs = now; d.startMs = 0; d.seq++; t = 0; }
            in.t = t; in.playing = true; in.lyrics = d.lyrics.get(); in.lyricsState = d.lyrics ? LyricsState::Found : LyricsState::NotFound;
            in.songSeq = d.seq; in.title = d.title; in.artist = d.artist;
            BandFrame const f = d.mem.frameAt(t);
            float const hit = std::clamp((f.bass - d.prevBass) * 6.0f, 0.0f, 1.0f);
            d.prevBass = f.bass;
            in.beat  = std::max(hit, in.beat * 0.9f); in.beatLive = true;
            ci.bands = f; ci.bandsLive = true; ci.memory = &d.mem;
        }
        ci.posMs   = in.t;
        ci.playing = in.playing;
        ci.songSeq = in.songSeq;
        ci.dt      = dt;
        ci.beat    = in.beat;
        ci.enabled = gS->engine.config().director;
        gS->conductor.update(ci);
        in.music = &gS->conductor.state();
        in.skyLuma = gS->sky.luma();
        in.albumAccent = gS->coverAccent;
        auto const& cfgNow = gS->engine.config(); // [BF] a scene picked while a song plays is remembered for it
        if (cfgNow.scene != gS->lastScene) {
            gS->lastScene = cfgNow.scene;
            if (cfgNow.songScenes && gS->clock.hasTrack() && !cfgNow.scene.empty()) {
                gS->songScenes[gS->clock.track().key()] = cfgNow.scene;
                gS->songScenesDirty = true;
            }
        }
        auto const& m = gS->conductor.state();
        auto stamp = [&](char const* what) {
            char b[160];
            std::snprintf(b, sizeof b, "%7.1fs %-7s budget %.2f energy %.2f section %d build %.2f hook %.2f choruses %d %s", double(in.t) / 1000.0,
                          what, m.budget, m.energy, int(m.section), m.build, m.inHook, m.choruses, m.known ? "memory" : "live");
            gS->directorLines.emplace_back(b);
        };
        if (m.songStart) stamp("start");
        if (m.buildNow) stamp("build");
        if (m.dropNow) stamp("DROP");
        if (m.chorusNow) stamp("CHORUS");
        if (m.calmNow) stamp("calm");
        static int64_t lastBeatLog = 0;
        if (m.active && now - lastBeatLog > 5000000000LL) { lastBeatLog = now; stamp("."); }
    }
    GameWorld world(a);
    auto const t0 = Clock::now();
    gS->engine.frame(in, cam, &world);
    gS->lastFov = fov;
    gS->camPos = cam.pos; gS->camYaw = cam.yaw; gS->camPitch = cam.pitch; gS->haveCam = true;
    panelStepAndPublish(now, true);
    skyStep(a, cam, dt, world);
    // Auto FPS (2.1 AutoPerf): frame rate from the HUD frame interval, the mod's own cost = this function's work
    double const modMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    gS->fpsSmooth      = gS->fpsSmooth <= 0 ? 1.0f / std::max(dt, 1e-3f) : gS->fpsSmooth * 0.95f + 0.05f / std::max(dt, 1e-3f);
    gS->engine.perf().tick(nowMsWall(), int(gS->fpsSmooth + 0.5f), modMs, gS->engine.config());
}


// ---------------------------------------------------------------- world panel (L; LyricsFlow 2.1 WorldPanel)
// The panel stands in the world 3.2 blocks in front of you; you aim with the crosshair and left-click. While the
// crosshair is on it, the game doesn't get the left / right click (no punching or placing through the menu).
// (host state: see namespace host above pollKeys)

void flushConfig(bool force) { // caller holds gS->mutex
    int64_t now = nowNs();
    if (gS->cfgDirty && (force || now - gS->savedNs > 500'000'000)) {
        saveConfig();
        gS->cfgDirty = false;
        gS->savedNs  = now;
    }
}

int64_t nowMsWall() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

Vec3 lookOf(float yawDeg, float pitchDeg) { // Minecraft yaw / pitch (degrees) -> unit forward
    double y = yawDeg * 3.14159265358979 / 180.0, p = pitchDeg * 3.14159265358979 / 180.0;
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}
Vec3 upOf(float yawDeg, float pitchDeg) {
    double y = yawDeg * 3.14159265358979 / 180.0, p = pitchDeg * 3.14159265358979 / 180.0;
    return {-std::sin(y) * std::sin(p), std::cos(p), std::cos(y) * std::sin(p)}; // right x forward (RF's camera basis)
}

void togglePanel() { // caller holds gS->mutex
    int64_t const t = nowMsWall();
    if (gS->panel.isOpen()) {
        gS->panel.close(t);
        flushConfig(true);
    } else if (gS->haveCam) {
        gS->panel.open(gS->camPos, lookOf(gS->camYaw, gS->camPitch), t);
    }
    host::open = gS->panel.isOpen();
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
        m.repeat      = t.repeat;
    }
    return m;
}

/// One panel step + publish (lyrics glyphs + panel glyphs). Caller holds gS->mutex.
void panelStepAndPublish(int64_t now, bool lyricsActive) {
    if (gS->coverPath != gS->coverShown) { gS->coverShown = gS->coverPath; publishAtlases(); }
    gS->panelAssets.coverAccent = gS->coverShown.empty() ? 0u : gS->coverAccent;
    gS->panelGlyphs.clear();
    int64_t const ms = nowMsWall();
    if (gS->panel.visible(ms) && gS->haveCam) {
        PanelInput in;
        in.eye   = gS->camPos;
        in.look  = lookOf(gS->camYaw, gS->camPitch);
        in.up    = upOf(gS->camYaw, gS->camPitch);
        in.nowMs = ms;
        in.beat     = gS->bass;
        in.beatLive = gS->bassLive;
        in.dt    = gS->panelStepNs ? std::clamp(float(double(now - gS->panelStepNs) / 1e9), 0.0f, 0.1f) : 1.0f / 60.0f;
        in.click = host::click.exchange(false);
        in.perfLevel = gS->engine.perf().level();
        in.fps       = gS->engine.perf().fps();
        in.modMs     = gS->engine.perf().modMs();
        in.down  = host::down.load();
        auto actions = gS->panel.frame(in, gS->engine.origin(), gS->engine.config(), gS->engine.fonts(), gS->panelAssets, panelMedia(now),
                                       gS->panelGlyphs);
        for (auto const& a : actions) {
            switch (a.kind) {
            case PanelAction::Kind::Close: gS->panel.close(ms); flushConfig(true); break;
            case PanelAction::Kind::SaveConfig: gS->cfgDirty = true; break;
            case PanelAction::Kind::OpenFullMenu:
                if (auto* cl = host::client.load()) {
                    gS->panel.close(ms);
                    gS->openMenuOn = cl; // opened after we drop the mutex (the menu takes its own lock)
                }
                break;
            default: gS->mediaCmds.push_back(a); break; // play/pause/next/prev/seek/repeat -> media thread
            }
        }
        flushConfig(false);
    }
    host::hovered = gS->panel.hovered();
    host::open    = gS->panel.isOpen();
    gS->panelStepNs = now;
    gS->engine.publish(lyricsActive, gS->lastFov, gS->panelGlyphs.empty() ? nullptr : &gS->panelGlyphs);
}

void installPanelHost() {
    // L / O / [ / ] are polled in hudFrame (pollKeys): LeviLamina's KeyRegistry handlers never fired in game.
    // clicks on the panel: left = press, right also swallowed so nothing gets placed behind it
    host::subs.on<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
        if (!gS || !host::open.load()) return;
        char const  a = ev.actionButtonId();
        schar const d = ev.buttonData();
        if (a == MouseAction::ActionLeft) {
            bool const pressed = d == MouseAction::DataDown;
            if (pressed && host::hovered.load()) {
                host::click = true;
                host::down  = true;
                ev.cancel();
            } else if (!pressed && host::down.load()) {
                host::down = false;
                ev.cancel();
            }
        } else if (a == MouseAction::ActionRight && host::hovered.load()) ev.cancel();
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
    gS->memoryDir = configDir / "lyricsflow_memory";
    std::error_code ec;
    std::filesystem::create_directories(gS->cacheDir, ec);
    loadConfig();
    loadFonts(modDir / "resources" / "lyricsflow" / "fonts");
    loadUiAtlas(modDir / "resources" / "lyricsflow" / "ui");
    { // [BF] client menu icon atlas
        std::error_code e2;
        auto png = modDir / "resources" / "client" / "icons.png";
        if (std::filesystem::exists(png, e2)) {
            gS->iconAtlas = png.string();
            if (auto j = json::parse(readAll(modDir / "resources" / "client" / "icons.json")))
                for (auto const& [k, v] : (*j)["rects"].members())
                    gS->iconRects[k] = UiRect{float(v[0].asNumber()), float(v[1].asNumber()), float(v[2].asNumber()), float(v[3].asNumber())};
        }
    }
    loadPresets();
    syncFontSlots();
    publishAtlases();
    gS->skyDir      = configDir / "lyricsflow_sky";
    std::filesystem::create_directories(gS->skyDir, ec);
    gS->spritesFile = (modDir / "resources" / "lyricsflow" / "sky" / "sprites.png").string();
    installPanelHost();

    auto& m = bedrock_forge::client::Registry::get().add("lyricsflow", "LyricsForge", bedrock_forge::client::Category::Visual,
                                                         "Synced song lyrics in the world (port of LyricsFlow by 374m) - PREVIEW, untested in game");
    m.hud      = [](bedrock_forge::client::Frame& f) {
        hudFrame(f);
        IClientInstance* menuOn = nullptr;
        {
            std::lock_guard l(gS->mutex);
            std::swap(menuOn, gS->openMenuOn);
        }
        if (menuOn) bedrock_forge::client::menu::open(*menuOn); // "Open full menu" on the panel
    };
    m.onToggle = [](bool on) {
        gS->moduleOn = on;
        if (!on) bassWanted(false);
        if (!on) {
            std::lock_guard l(gS->mutex);
            if (gS->panel.isOpen()) gS->panel.close(nowMsWall());
            gS->engine.reset();
            gS->engine.publish(false, 70.0f);
            gS->sky.step(false, gS->engine.config(), gS->engine.perf(), {});
        }
    };

    // Keys: LyricsFlow's K / J are taken by BF (impact frames / HUD), so only on/off + per-song offset for now.
    gS->running = true;
    gS->media   = std::thread(mediaLoop);
    bassStart(); // idles (device closed) until the Neon player / beat effects want it
}

ClientUiAssets clientUiAssets() {
    ClientUiAssets a;
    if (!gS) return a;
    std::lock_guard l(gS->mutex);
    syncFontSlots(); // the menu needs Outfit even while the lyricsflow module is off
    auto const* r = gS->engine.fonts().get("outfit");
    auto const* b = gS->engine.fonts().get("outfit_bold");
    a.regular     = r && r->atlasIndex != FontSet::kNoAtlas && r->id == "outfit" ? r : nullptr;
    a.bold        = b && b->atlasIndex != FontSet::kNoAtlas && b->id == "outfit_bold" ? b : a.regular;
    a.icons       = gS->iconAtlasIndex;
    a.iconRects   = gS->iconRects;
    return a;
}
void requestPanel() { gPanelRequest = true; }
bool lyricsShown() {
    if (!gS) return false;
    std::lock_guard l(gS->mutex);
    return gS->engine.config().enabled;
}
void setLyricsShown(bool on) {
    if (!gS) return;
    std::lock_guard l(gS->mutex);
    gS->engine.config().enabled = on;
    if (!on) gS->engine.reset();
    saveConfig();
}

void noteCombat() {
    gCombatAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void uninstall() {
    if (!gS) return;
    host::subs.clear();
    gS->running = false;
    if (gS->media.joinable()) gS->media.join();
    try { std::lock_guard l(gS->mutex); finishSongMemory(); } catch (...) {} // keep this play's map
    bassStop();
    {
        std::lock_guard l(gS->mutex);
        saveConfig();
        gS->engine.publish(false, 70.0f);
    }
    // State stays allocated on purpose: detached fetch threads may still be finishing and check gS->running.
}

} // namespace bf::lyrics::win
#endif
