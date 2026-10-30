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
#include <mutex>
#include <sstream>
#include <thread>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"

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
    bool                    moduleOn    = false;
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
    std::vector<std::string> atlases;
    std::error_code          ec;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".json") continue;
        auto png = e.path();
        png.replace_extension(".png");
        if (!std::filesystem::exists(png)) continue;
        if (gS->engine.fonts().loadFromJson(e.path().stem().string(), readAll(e.path()), png.string())) atlases.push_back(png.string());
    }
    FrameStore::get().setAtlases(atlases);
}

// LyricsFlow MediaManager.fetchLyrics: up to 5 attempts, waiting 1.5 s * attempt, abandoned when the song changes.
void fetchFor(MediaSample s, int64_t seq) {
    std::thread([s, seq] {
        Track tk;
        tk.s = s;
        auto const cacheFile = gS->cacheDir / cacheFileName(tk.key());
        std::optional<Lyrics> got;
        if (std::filesystem::exists(cacheFile)) got = fromCacheJson(readAll(cacheFile)); // LyricsFlow readCache
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
        if (got && !std::filesystem::exists(cacheFile)) { // LyricsFlow writeCache (synced results only reach here)
            std::ofstream out(cacheFile, std::ios::binary);
            out << toCacheJson(*got);
        }
        std::lock_guard l(gS->mutex);
        if (gS->clock.songSeq() != seq) return;
        gS->lyrics      = got ? std::make_shared<Lyrics const>(std::move(*got)) : nullptr;
        gS->lyricsState = got ? LyricsState::Found : LyricsState::NotFound;
    }).detach();
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
        auto sample = gS->moduleOn ? gS->smtc->poll(src) : std::nullopt;
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
        std::this_thread::sleep_until(started + std::chrono::milliseconds(250));
    }
    gS->smtc.reset();
}

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
    gS->engine.publish(true, fov);
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

    auto& m = bedrock_forge::client::Registry::get().add("lyricsflow", "LyricsFlow", bedrock_forge::client::Category::Visual,
                                                         "Synced song lyrics in the world (port of LyricsFlow by 374m) - PREVIEW, untested in game");
    m.hud      = [](bedrock_forge::client::Frame& f) { hudFrame(f); };
    m.onToggle = [](bool on) {
        gS->moduleOn = on;
        if (!on) {
            std::lock_guard l(gS->mutex);
            gS->engine.reset();
            gS->engine.publish(false, 70.0f);
        }
    };

    // Keys: LyricsFlow's K / J are taken by BF (impact frames / HUD), so only on/off + per-song offset for now.
    bf::input::bindKey("lyricsflow.toggle", 0x4F /*O*/, [] {
        std::lock_guard l(gS->mutex);
        gS->engine.config().enabled = !gS->engine.config().enabled;
        if (!gS->engine.config().enabled) gS->engine.reset();
        saveConfig();
    });
    bf::input::bindKey("lyricsflow.earlier", 0xDD /*]*/, [] { nudgeSongOffset(+100); });
    bf::input::bindKey("lyricsflow.later", 0xDB /*[*/, [] { nudgeSongOffset(-100); });

    gS->running = true;
    gS->media   = std::thread(mediaLoop);
}

void noteCombat() {
    gCombatAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void uninstall() {
    if (!gS) return;
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
