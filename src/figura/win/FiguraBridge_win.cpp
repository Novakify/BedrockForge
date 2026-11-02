// FiguraBridge - Bedrock Forge module: runs a Figura avatar (src/figura/core) on the local player.
//   * avatar bundles: <BedrockForgeCore config>/figura/<name>/{avatar/, rig.json} made by tools/FiguraBridge/compile.py
//     (the matching resource pack from the same compile must be active - it draws the avatar)
//   * HUD frame: samples the player (20 Hz ticks + every frame), runs the avatar, writes per-part Molang variables
//     (v.fb<i>rx ... see rig.json "vars") into the local player's MolangVariableMap; the pack's drive animation and
//     render controllers read them on the next frame.
//   * action wheel: hold B (Figura default) -> list in the info panel, keys 1-8 run actions; scripts' keybinds read
//     Win32 key state while the game window is focused.
// Original code for Bedrock Forge; Figura 0.1.6 semantics (PolyForm NC, private use with permission).
#ifdef _WIN32
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/PlaySoundOptions.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/util/MolangScriptArgType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "client/Module.h"
#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"
#include "figura/core/LuaTex.h"
#include "figura/win/DynTex.h"
#include "lyrics/core/Json.h"

namespace bf::figura::win {

namespace fs = std::filesystem;

namespace {
using Clock = std::chrono::steady_clock;

int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

std::string readAll(fs::path const& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// ---------------------------------------------------------------- keys (Java key names -> VK)
int vkOf(std::string const& k) {
    static std::map<std::string, int> m = [] {
        std::map<std::string, int> r;
        for (char c = 'a'; c <= 'z'; c++) r[std::string("key.keyboard.") + c] = 'A' + (c - 'a');
        for (char c = '0'; c <= '9'; c++) r[std::string("key.keyboard.") + c] = c;
        for (int i = 1; i <= 12; i++) r["key.keyboard.f" + std::to_string(i)] = VK_F1 + i - 1;
        r["key.keyboard.space"]         = VK_SPACE;
        r["key.keyboard.left.shift"]    = VK_LSHIFT;
        r["key.keyboard.right.shift"]   = VK_RSHIFT;
        r["key.keyboard.left.control"]  = VK_LCONTROL;
        r["key.keyboard.right.control"] = VK_RCONTROL;
        r["key.keyboard.left.alt"]      = VK_LMENU;
        r["key.keyboard.right.alt"]     = VK_RMENU;
        r["key.keyboard.tab"]           = VK_TAB;
        r["key.keyboard.enter"]         = VK_RETURN;
        r["key.keyboard.escape"]        = VK_ESCAPE;
        r["key.keyboard.backspace"]     = VK_BACK;
        r["key.keyboard.up"]            = VK_UP;
        r["key.keyboard.down"]          = VK_DOWN;
        r["key.keyboard.left"]          = VK_LEFT;
        r["key.keyboard.right"]         = VK_RIGHT;
        r["key.keyboard.grave.accent"]  = VK_OEM_3;
        r["key.keyboard.caps.lock"]     = VK_CAPITAL;
        r["key.mouse.left"]             = VK_LBUTTON;
        r["key.mouse.right"]            = VK_RBUTTON;
        r["key.mouse.middle"]           = VK_MBUTTON;
        r["key.mouse.4"]                = VK_XBUTTON1;
        r["key.mouse.5"]                = VK_XBUTTON2;
        return r;
    }();
    auto it = m.find(k);
    return it == m.end() ? 0 : it->second;
}
bool gameFocused() {
    HWND  fg  = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// ---------------------------------------------------------------- world (what Molang can tell us; blocks/biomes stay default)
struct BedrockWorld final : IWorldQuery {
    float   timeOfDay = 0.25f; // query.time_of_day: midnight 0, sunrise 0.25, noon 0.5
    int     moon      = 0;     // query.moon_phase
    int64_t ticks     = 0;     // ticks since the avatar loaded (game time stand-in)
    int64_t time() override { return ticks; }
    int64_t dayTime() override {
        float j = timeOfDay - 0.25f; // Java: 0 = sunrise, 6000 = noon, 18000 = midnight
        if (j < 0) j += 1.0f;
        return int64_t(j * 24000.0f);
    }
    int moonPhase() override { return moon; }
};

// ---------------------------------------------------------------- host
struct BedrockHost final : IHost {
    BedrockWorld             worldQ;
    std::string              title, subtitle, actionbar;   // host:setTitle/... shown in the info panel
    int64_t                  titleUntil = 0, actionbarUntil = 0;
    int                      titleFadeIn = 10, titleStay = 70, titleFadeOut = 20; // ticks (Gui defaults)
    IWorldQuery& world() override { return worldQ; }
    void setTitle(std::string const& t) override { title = t; titleUntil = nowMs() + int64_t(titleFadeIn + titleStay + titleFadeOut) * 50; }
    void setSubtitle(std::string const& t) override { subtitle = t; }
    void setTitleTimes(int a, int b, int c) override { titleFadeIn = a; titleStay = b; titleFadeOut = c; }
    void clearTitle() override { title.clear(); subtitle.clear(); titleUntil = 0; }
    void setActionbar(std::string const& t) override { actionbar = t; actionbarUntil = nowMs() + 3000; }
    std::string titleText() override { return nowMs() < titleUntil ? title : std::string(); }
    std::string subtitleText() override { return nowMs() < titleUntil ? subtitle : std::string(); }
    std::string actionbarText() override { return nowMs() < actionbarUntil ? actionbar : std::string(); }
    IClientInstance*         client = nullptr;
    std::vector<std::string> lines;
    std::string              lastError;
    fs::path                 logFile;
    std::map<int, uint64_t>  handles;
    std::map<std::string, std::string> soundEvents;
    std::string              avatarSlug;

    int64_t millis() override { return nowMs(); }
    bool    paused() override { return false; }
    void    write(std::string const& line) {
        std::ofstream f(logFile, std::ios::app);
        f << line << "\n";
    }
    void log_(std::string const& m) {
        lines.push_back(m);
        if (lines.size() > 6) lines.erase(lines.begin());
        write(m);
    }
    void log(std::string const& m) override { log_("[lua] " + m); }
    void error(std::string const& m) override {
        lastError = m;
        log_("[error] " + m);
    }
    int lastTexResult = -1;
    std::set<std::string> loggedUploads;
    bool uploadTexture(std::string const& path, int w, int h, uint8_t const* rgba) override {
        if (!client) return false;
        lastTexResult = int(dynTexUploadEx(*client, path, w, h, rgba)); // HUD frame = client thread
        if (lastTexResult != 0 && !loggedUploads.insert(path).second) return true; // log once per texture (failures always)
        write("[figurabridge] texture upload " + path + " " + std::to_string(w) + "x" + std::to_string(h) + " -> " +
              (lastTexResult == 1 ? "in place" : lastTexResult == 2 ? "uploaded" : "FAILED"));
        return lastTexResult != 0;
    }
    void playSound(std::string const& id, bool custom, Vec3 pos, float volume, float pitch, bool, int handle) override {
        if (!client) return;
        std::string name;
        if (custom) {
            auto it = soundEvents.find(id);
            if (it == soundEvents.end()) return;
            name = it->second;
        } else {
            name = id;
            if (name.rfind("minecraft:", 0) == 0) name = name.substr(10);
        }
        try {
            auto engine = client->getSoundEngine();
            uint64_t h  = engine->play(name, ::Vec3{float(pos.x), float(pos.y), float(pos.z)}, volume, pitch, std::nullopt);
            handles[handle] = h;
        } catch (...) {}
    }
    void stopSound(int handle) override {
        if (!client) return;
        try {
            auto engine = client->getSoundEngine();
            if (handle < 0) {
                for (auto& [k, h] : handles) engine->stop(h);
                handles.clear();
            } else if (auto it = handles.find(handle); it != handles.end()) {
                engine->stop(it->second);
                handles.erase(it);
            }
        } catch (...) {}
    }
    bool keyDown(std::string const& key) override {
        int vk = vkOf(key);
        return vk && gameFocused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    std::string vanillaKeyBinding(std::string const& id) override {
        static std::map<std::string, std::string> m = {
            {"key.forward", "key.keyboard.w"}, {"key.back", "key.keyboard.s"}, {"key.left", "key.keyboard.a"},
            {"key.right", "key.keyboard.d"}, {"key.jump", "key.keyboard.space"}, {"key.sneak", "key.keyboard.left.shift"},
            {"key.sprint", "key.keyboard.left.control"}, {"key.attack", "key.mouse.left"}, {"key.use", "key.mouse.right"},
            {"key.pickItem", "key.mouse.middle"}, {"key.inventory", "key.keyboard.e"}, {"key.drop", "key.keyboard.q"},
            {"key.chat", "key.keyboard.t"}, {"key.playerlist", "key.keyboard.tab"}, {"key.swapOffhand", "key.keyboard.f"},
            {"key.togglePerspective", "key.keyboard.f5"}};
        auto it = m.find(id);
        return it == m.end() ? "key.keyboard.unknown" : it->second;
    }
    std::string version() override { return "1.20.1"; }
};

// ---------------------------------------------------------------- Molang writer
struct VarNames {
    HashedString rx, ry, rz, px, py, pz, sx, sy, sz, v, t, e;
    HashedString uu{std::string()}, uv{std::string()}, usu{std::string()}, usv{std::string()}; // only for bones with a uv_anim controller
};
struct State {
    std::mutex                    mutex;
    fs::path                      root;       // <config>/figura
    std::string                   avatarName; // folder under root
    std::unique_ptr<BedrockHost>  host;
    std::unique_ptr<Avatar>       avatar;
    std::vector<VarNames>         names;
    std::unique_ptr<HashedString> van;
    int                           nameMode = -1; // -1 unknown, 0 "variable.x", 1 "x"
    int64_t                       lastTickMs = 0;
    int64_t                       lastScanMs = 0;
    int64_t                       lastReloadCheckMs = 0;
    long long                     reloadStamp = 0;
    bool                          wheelHeld = false, bWasDown = false, wheelExecDone = false;
    std::set<int>                 digitsDown;
    std::string                   status;
    void*                         lastPlayer = nullptr;
    std::string                   pendingAvatar; // switch requested mid-world: applied on the next world join
    std::vector<std::pair<int, HashedString>> useNames; // v.fbuse<file> (render controller gating)
    std::vector<float>            cache;      // last value written per bone var (16 per bone); NaN = unknown
    int64_t                       frameNo = 0, dumpAtFrame = -1, wheelSeqNext = 0;
    int                           wheelSeqPos = 0;
    int64_t                       lastFullWriteMs = 0;
};
State* gS = nullptr;

std::string vn(int mode, std::string const& n) { return mode == 0 ? "variable." + n : n; }

void buildNames(State& s) {
    s.names.clear();
    if (!s.avatar) return;
    int m = s.nameMode < 0 ? 0 : s.nameMode;
    for (size_t i = 0; i < s.avatar->out.size(); i++) { // one entry per Bedrock bone (parts + dup bones)
        std::string b = "fb" + std::to_string(i);
        s.names.push_back({HashedString(vn(m, b + "rx")), HashedString(vn(m, b + "ry")), HashedString(vn(m, b + "rz")),
                           HashedString(vn(m, b + "px")), HashedString(vn(m, b + "py")), HashedString(vn(m, b + "pz")),
                           HashedString(vn(m, b + "sx")), HashedString(vn(m, b + "sy")), HashedString(vn(m, b + "sz")),
                           HashedString(vn(m, b + "v")), HashedString(vn(m, b + "t")), HashedString(vn(m, b + "e"))});
        if (i < s.avatar->uvBones.size() && s.avatar->uvBones[i]) {
            auto& n = s.names.back();
            n.uu = HashedString(vn(m, b + "uu"));
            n.uv = HashedString(vn(m, b + "uv"));
            n.usu = HashedString(vn(m, b + "usu"));
            n.usv = HashedString(vn(m, b + "usv"));
        }
    }
    s.van = std::make_unique<HashedString>(vn(m, "fbvan"));
    s.useNames.clear();
    for (int f : s.avatar->gatedFiles) s.useNames.emplace_back(f, HashedString(vn(m, "fbuse" + std::to_string(f))));
    s.cache.assign(s.avatar->out.size() * 16, std::numeric_limits<float>::quiet_NaN());
}

void calibrate(State& s, Actor& a) {
    // Which key does a Molang "v.x" read? Write a probe both ways, read it back through evalMolang.
    auto& map = *a.mMolangVariables;
    map.setMolangVariable(HashedString("variable.fbprobe"), MolangScriptArg(42.0f));
    float v0 = a.evalMolang("v.fbprobe");
    if (v0 > 41.5f && v0 < 42.5f) s.nameMode = 0;
    else {
        map.setMolangVariable(HashedString("fbprobe2"), MolangScriptArg(43.0f));
        float v1 = a.evalMolang("v.fbprobe2");
        s.nameMode = (v1 > 42.5f && v1 < 43.5f) ? 1 : 0;
    }
    s.host->write("[figurabridge] molang variable naming mode " + std::to_string(s.nameMode));
    buildNames(s);
}

void writeVars(State& s, Actor& a) {
    if (!s.avatar || s.names.size() < s.avatar->out.size()) buildNames(s);
    auto& map = *a.mMolangVariables;
    auto  set = [&](HashedString const& n, double v) { map.setMolangVariable(n, MolangScriptArg(float(v))); };
    auto const& out = s.avatar->out;
    // only changed values are written (a full write every 2 s in case the game reset the variable map); a hidden
    // bone's pose is not written at all (nothing draws it)
    int64_t now = nowMs();
    bool    full = now - s.lastFullWriteMs > 2000 || s.cache.size() < out.size() * 16;
    if (full) {
        s.lastFullWriteMs = now;
        if (s.cache.size() < out.size() * 16) s.cache.assign(out.size() * 16, std::numeric_limits<float>::quiet_NaN());
    }
    auto put = [&](size_t i, int k, HashedString const& n, double v) {
        float  f = float(v);
        float& c = s.cache[i * 16 + size_t(k)];
        if (!full && c == f) return;
        c = f;
        set(n, f);
    };
    for (size_t i = 0; i < out.size() && i < s.names.size(); i++) {
        auto const& o = out[i];
        auto const& n = s.names[i];
        bool vis = o.visible && o.opacity > 0.001f;
        put(i, 9, n.v, vis ? 1.0 : 0.0);
        if (!vis && !full) continue;
        put(i, 0, n.rx, o.bone.rot.x);
        put(i, 1, n.ry, o.bone.rot.y);
        put(i, 2, n.rz, o.bone.rot.z);
        put(i, 3, n.px, o.bone.pos.x);
        put(i, 4, n.py, o.bone.pos.y);
        put(i, 5, n.pz, o.bone.pos.z);
        put(i, 6, n.sx, o.bone.scale.x);
        put(i, 7, n.sy, o.bone.scale.y);
        put(i, 8, n.sz, o.bone.scale.z);
        put(i, 10, n.t, o.primary);
        put(i, 11, n.e, o.secondary);
        if (i < s.avatar->uvBones.size() && s.avatar->uvBones[i]) {
            put(i, 12, n.uu, o.uv[0]);
            put(i, 13, n.uv, o.uv[1]);
            put(i, 14, n.usu, o.uv[2]);
            put(i, 15, n.usv, o.uv[3]);
        }
    }
    bool gate = s.avatar->tune("rcGate", 1) != 0; // 0: every texture pass always on (pre-gating behaviour)
    for (auto const& [f, n] : s.useNames)
        set(n, !gate || (f >= 0 && size_t(f) < s.avatar->fileUsed.size() && s.avatar->fileUsed[size_t(f)]) ? 1.0 : 0.0);
    for (int side = 0; side < 2; side++) { // held items on Figura's item pivots (compile.py animation.figura.items)
        auto const& io = s.avatar->itemOut[side];
        if (!io.configured) continue;
        std::string b = side == 0 ? "fbir" : "fbil";
        int m = s.nameMode < 0 ? 0 : s.nameMode;
        auto w = [&](char const* suf, double v) { map.setMolangVariable(HashedString(vn(m, b + suf)), MolangScriptArg(float(v))); };
        BoneOut const bo = io.found ? io.bone : BoneOut{};
        double sc = io.found ? 1.0 : 0.0; // pivot hidden or gone: Figura draws no item
        w("rx", bo.rot.x); w("ry", bo.rot.y); w("rz", bo.rot.z);
        w("px", bo.pos.x); w("py", bo.pos.y); w("pz", bo.pos.z);
        w("sx", bo.scale.x * sc); w("sy", bo.scale.y * sc); w("sz", bo.scale.z * sc);
    }
    bool vanillaShown = true;
    if (auto it = s.avatar->vanillaVisible.find("PLAYER"); it != s.avatar->vanillaVisible.end()) vanillaShown = it->second;
    if (s.van) set(*s.van, vanillaShown ? 1.0 : 0.0);
}

// ---------------------------------------------------------------- player sampling
ItemState itemOf(ItemStack const& st) {
    ItemState it;
    try {
        if (st.isNull()) return it;
        it.id    = st.getTypeName();
        if (it.id.find(':') == std::string::npos) it.id = "minecraft:" + it.id;
        it.count = 1;
        it.name  = it.id;
        auto const& id = it.id;
        auto has = [&](char const* s) { return id.find(s) != std::string::npos; };
        it.isTool  = has("_sword") || has("_axe") || has("_pickaxe") || has("_shovel") || has("_hoe") || has("trident") || has("mace");
        it.isArmor = has("_helmet") || has("_chestplate") || has("_leggings") || has("_boots") || has("elytra");
        if (has("bow") && !has("crossbow")) it.useAction = "BOW";
        else if (has("crossbow")) it.useAction = "CROSSBOW";
        else if (has("shield")) it.useAction = "BLOCK";
        else if (has("trident")) it.useAction = "SPEAR";
        else if (has("spyglass")) it.useAction = "SPYGLASS";
        else if (has("potion") || has("milk_bucket")) it.useAction = "DRINK";
        else if (has("apple") || has("bread") || has("beef") || has("porkchop") || has("chicken") || has("mutton") ||
                 has("cod") || has("salmon") || has("carrot") || has("potato") || has("stew") || has("cookie") || has("melon_slice") ||
                 has("berries") || has("pie") || has("cake") || has("kelp") || has("beetroot") || has("rabbit"))
            it.useAction = "EAT", it.isFood = true;
        it.maxUseDuration = it.useAction == "BOW" ? 72000 : it.useAction == "EAT" || it.useAction == "DRINK" ? 32 : 0;
    } catch (...) {}
    return it;
}

// read an actor's own Molang variable (set by its animation controllers) without evaluating an expression
float readVar(Actor& a, char const* name) {
    try {
        HashedString h(name);
        auto const& arg = a.mMolangVariables->getMolangVariable(h.getHash(), name);
        if (arg.mType == MolangScriptArgType::Float) return arg.mPOD.mFloat;
    } catch (...) {}
    return 0.0f;
}

void samplePlayer(State& s, LocalPlayer& lp, bool tick) {
    Actor&       a = lp;
    PlayerState& p = s.avatar->player;
    p.loaded = true;
    try { p.name = a.getNameTag(); } catch (...) {}
    auto feet = a.getFeetPos();
    if (tick) {
        p.prevPos = p.loaded && p.pos.length() > 0 ? p.pos : Vec3{feet.x, feet.y, feet.z};
        p.pos     = {feet.x, feet.y, feet.z};
        p.velocity = p.pos - p.prevPos;
        p.prevYaw = p.yaw;
        p.prevPitch = p.pitch;
        p.prevBodyYaw = p.bodyYaw;
        p.prevWalkSpeed = p.walkSpeed;
        double h  = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z);
        float  sp = float(std::min(1.0, h * 4.0));
        p.walkSpeed += (sp - p.walkSpeed) * 0.4f;
        p.walkPos += p.walkSpeed;
        p.age++;
        p.onGround  = a.isOnGround();
        // NOTE: Actor::evalMolang rejects every query./variable. here ("not allowed in this context", one content-log
        // error per call), so player state comes straight from the actor
        // test knob: tuning "forceSneak" puts the real player into Bedrock's sneak state (what Shift does)
        if (double fs = s.avatar->tune("forceSneak", -1); fs >= 0) {
            try { a.setSneaking(fs > 0); } catch (...) {}
        }
        try {
            p.crouching = a.isSneaking();
            p.sprinting = a.isSprinting();
            p.swimming  = a.isSwimming();
            p.gliding   = a.isGliding();
            p.sleeping  = a.isSleeping();
            p.inWater   = a.isInWater();
            p.onFire    = a.isOnFire();
            p.invisible = a.isInvisible();
            p.alive     = a.isAlive();
            p.usingItem = a.getStatusFlag(ActorFlags::Usingitem);
            p.health    = float(a.getHealth());
            p.maxHealth = float(a.getMaxHealth());
            p.climbing  = a.getStatusFlag(ActorFlags::Wallclimbing);
            p.inRain    = !p.inWater && a.isInRain();
            p.blocking  = a.isBlocking();
            p.jumping   = a.isJumping();
        } catch (...) {}
        p.inLava    = false;
        p.hasCape   = false;
        s.host->worldQ.ticks++;

        p.pose      = p.sleeping ? "SLEEPING" : p.gliding ? "FALL_FLYING" : p.swimming ? "SWIMMING" : p.crouching ? "CROUCHING" : "STANDING";
        float used  = 0;
        try { used = float(lp.getItemUseDuration()) / 20.0f; } catch (...) {}
        try {
            p.mainHand = itemOf(lp.getCarriedItem());
            p.offHand  = itemOf(a.getOffhandSlot());
        } catch (...) {}
        p.activeItem       = p.usingItem ? p.mainHand : ItemState{};
        p.useItemRemaining = p.usingItem ? std::max(0, p.mainHand.maxUseDuration - int(used * 20)) : 0;
        Actor* veh = a.getVehicle();
        p.vehicle.valid = veh != nullptr;
        if (veh) {
            try { p.vehicle.type = veh->getTypeName(); } catch (...) { p.vehicle.type = "minecraft:boat"; }
            auto vp = veh->getFeetPos();
            p.vehicle.pos = {vp.x, vp.y, vp.z};
        }
        bool sw = false;
        try { sw = lp.mSwinging; } catch (...) {}
        if (sw && !p.swinging) p.swingTime = 0;
        p.swinging   = sw;
        if (sw) p.swingTime++;
    }
    // test knobs (config/figura/tuning.json): fake movement so animations can be checked without holding keys
    if (tick) {
        double fw = s.avatar->tune("simWalk", 0);
        if (fw != 0) {
            p.velocity.z += 0.1 * fw;
            double h = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z);
            p.walkSpeed = p.prevWalkSpeed + (float(std::min(1.0, h * 4.0)) - p.prevWalkSpeed) * 0.4f;
            p.walkPos += p.walkSpeed;
            p.sprinting = fw > 2.5;
        }
        if (s.avatar->tune("simCrouch", 0) > 0) { p.crouching = true; p.pose = "CROUCHING"; }
        if (s.avatar->tune("simRide", 0) > 0) { p.vehicle.valid = true; p.vehicle.type = "minecraft:boat"; }
        if (s.avatar->tune("simAir", 0) > 0) { p.onGround = false; p.velocity.y = -0.4; }
    }
    auto rot    = a.getRotation();
    p.pitch     = rot.x;
    p.yaw       = rot.y;
    p.headYaw   = rot.y;
    try { p.bodyYaw = lp.getYBodyRotation(); } catch (...) { p.bodyYaw = rot.y; }
    try { p.attackAnim = std::clamp(float(lp.mAttackAnim), 0.0f, 1.0f); } catch (...) {}
    p.isFirstPerson = readVar(a, "variable.is_first_person") > 0.5f;
    // the real camera (third-person front, /camera ...) for Figura's Camera parts
    // query.camera_rotation reads 0 outside a render context, so take the renderer's own camera forward vector
    // (LevelRendererPlayer::mCameraForward, the audio-listener camera: follows F5 views and /camera)
    bool camOk = false;
    if (s.host && s.host->client) {
        if (auto* lr = s.host->client->getLevelRenderer()) {
            auto* lrp = lr->mLevelRendererPlayer.get();
            if (lrp) {
                ::Vec3 const f = lrp->mCameraForward.get();
                double l = std::sqrt(double(f.x) * f.x + double(f.y) * f.y + double(f.z) * f.z);
                if (std::isfinite(l) && l > 0.5 && l < 1.5) {
                    s.avatar->cameraYaw   = float(std::atan2(-f.x / l, f.z / l) * 57.29577951308232);
                    s.avatar->cameraPitch = float(std::asin(std::clamp(-f.y / l, -1.0, 1.0)) * 57.29577951308232);
                    camOk = true;
                }
            }
        }
    }
    if (!camOk) {
        s.avatar->cameraPitch = p.pitch;
        s.avatar->cameraYaw   = p.yaw;
    }
    s.avatar->cameraSet   = s.avatar->tune("useCamera", 1) > 0;
}

// ---------------------------------------------------------------- avatar loading
bool loadAvatar(State& s, std::string const& name) {
    fs::path dir = s.root / name;
    auto     av  = std::make_unique<Avatar>(s.host.get());
    std::string err;
    {   // Figura's FileAPI root: <figura>/data (created here so file:write works like a fresh Figura install)
        std::error_code ec;
        fs::create_directories(s.root / "data", ec);
        if (!ec) av->dataDir = s.root / "data";
    }
    s.host->soundEvents.clear();
    if (auto rig = lyrics::json::parse(readAll(dir / "rig.json")))
        for (auto const& [k, v] : (*rig)["sounds"].members()) s.host->soundEvents[k] = v.str();
    if (!av->load(dir / "avatar", dir / "rig.json", &err)) {
        s.status = "FiguraBridge: failed to load '" + name + "': " + err;
        s.host->write(s.status);
        return false;
    }
    s.avatar     = std::move(av);
    s.wheelExecDone = false;
    s.wheelSeqPos   = 0;
    if (auto t = lyrics::json::parse(readAll(s.root / "tuning.json")))
        for (auto const& [k, v] : t->members())
            if (v.isNumber()) s.avatar->tuning[k] = v.asNumber();
    s.avatarName = name;
    s.status     = "FiguraBridge: " + s.avatar->name + (s.avatar->scriptError ? " (script error)" : "");
    s.host->write("[figurabridge] loaded " + name + ": " + std::to_string(s.avatar->parts.size()) + " parts");
    buildNames(s);
    return true;
}

std::vector<std::string> listAvatars(State const& s) {
    std::vector<std::string> r;
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(s.root, ec))
        if (e.is_directory() && fs::exists(e.path() / "rig.json")) r.push_back(e.path().filename().string());
    std::sort(r.begin(), r.end());
    return r;
}

// ---------------------------------------------------------------- action wheel (info panel + digit keys)
std::string wheelTitle(State& s, int i) { // "page / action" titles of slot i (1-8) on the current page
    lua_State* L = s.avatar->L;
    std::string page = "(none)", act = "(empty)";
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.wheel");
    if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_istable(L, -1)) {
        lua_getfield(L, -1, "title");
        if (lua_isstring(L, -1)) page = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, i);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "title");
            if (lua_isstring(L, -1)) act = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return page + " / " + act;
}

void runWheelAction(State& s, int i, bool right) {
    s.host->write("[figurabridge] wheel " + std::to_string(i) + (right ? " (right)" : "") + ": " + wheelTitle(s, i));
    s.dumpAtFrame = s.frameNo + 20; // log what the game holds once the action took effect
    lua_State* L = s.avatar->L;
    lua_getglobal(L, "action_wheel");
    lua_getfield(L, -1, "execute");
    lua_pushvalue(L, -2);
    lua_pushinteger(L, i);
    lua_pushboolean(L, right);
    if (lua_pcall(L, 3, 0, 0) != 0) {
        s.host->error(lua_tostring(L, -1) ? lua_tostring(L, -1) : "action error");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void wheelFrame(State& s, bedrock_forge::client::Frame& f) {
    if (!s.avatar || !s.avatar->L) return;
    // test hook: tuning "wheelExec" = n runs action n once per reload
    if (int n = int(s.avatar->tune("wheelExec", 0)); n > 0 && !s.wheelExecDone) {
        s.wheelExecDone = true;
        runWheelAction(s, n, false);
    }
    // test hook: tuning "wheelSeq" = digits run one after another (e.g. 63 = slot 6, then slot 3), once per reload
    if (int q = int(s.avatar->tune("wheelSeq", 0)); q > 0 && s.wheelSeqPos >= 0) {
        std::string digits = std::to_string(q);
        if (s.wheelSeqPos < int(digits.size()) && s.frameNo >= s.wheelSeqNext) {
            runWheelAction(s, digits[size_t(s.wheelSeqPos)] - '0', false);
            s.wheelSeqPos++;
            s.wheelSeqNext = s.frameNo + 15;
        }
    }
    // Figura's default is B, but B opens Bedrock's emote wheel, so the toggle key defaults to Z
    // (tuning "wheelKey" = a Windows virtual-key code, e.g. 66 for B)
    int  wheelKey = int(s.avatar->tune("wheelKey", 'Z'));
    bool bDown    = gameFocused() && (GetAsyncKeyState(wheelKey) & 0x8000);
    if (bDown && !s.bWasDown) {
        s.wheelHeld = !s.wheelHeld; // tap: open / close (hold-to-open is awkward on Bedrock)
        s.host->write(std::string("[figurabridge] wheel ") + (s.wheelHeld ? "opened" : "closed"));
        lua_State* WL = s.avatar->L;  // action_wheel:isEnabled() follows the wheel
        lua_getfield(WL, LUA_REGISTRYINDEX, "fb.wheelState");
        if (lua_isfunction(WL, -1)) {
            lua_pushboolean(WL, s.wheelHeld);
            lua_pushinteger(WL, 0);
            if (lua_pcall(WL, 2, 0, 0) != 0) lua_pop(WL, 1);
        } else lua_pop(WL, 1);
    }
    s.bWasDown = bDown;
    if (!s.wheelHeld) return;
    lua_State* L = s.avatar->L;
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.wheel");
    if (!lua_isfunction(L, -1) || lua_pcall(L, 0, 1, 0) != 0 || !lua_istable(L, -1)) {
        lua_pop(L, 1);
        f.infoLines.push_back("Action wheel: (no page)  [Z to close]");
        return;
    }
    lua_getfield(L, -1, "title");
    f.infoLines.push_back(std::string("Action wheel: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "") +
                          "  (1-8 = click, Shift = right click, Z = close)");
    lua_pop(L, 1);
    int n = int(lua_rawlen(L, -1));
    for (int i = 1; i <= n && i <= 8; i++) {
        lua_rawgeti(L, -1, i);
        lua_getfield(L, -1, "title");
        lua_getfield(L, -2, "toggled");
        std::string t = lua_isstring(L, -2) ? lua_tostring(L, -2) : "?";
        bool        tg = lua_toboolean(L, -1);
        f.infoLines.push_back(std::to_string(i) + ". " + t + (tg ? " [on]" : ""));
        lua_pop(L, 3);
    }
    lua_pop(L, 1);
    for (int i = 1; i <= 8; i++) {
        bool d = gameFocused() && (GetAsyncKeyState('0' + i) & 0x8000) != 0;
        if (d && !s.digitsDown.count(i)) {
            s.digitsDown.insert(i);
            runWheelAction(s, i, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        } else if (!d) s.digitsDown.erase(i);
    }
}

void hudFrame(bedrock_forge::client::Frame& f) {
    if (!gS) return;
    State& s = *gS;
    std::lock_guard lk(s.mutex);
    s.host->client = &f.client;
    if (!s.avatar) {
        int64_t t = nowMs();
        if (t - s.lastScanMs < 5000) return; // nothing installed: rescan every 5 s, stay silent
        s.lastScanMs = t;
        auto all = listAvatars(s);
        std::string want = s.avatarName;
        if (!want.empty() && std::find(all.begin(), all.end(), want) == all.end()) want.clear();
        if (want.empty() && !all.empty()) want = all.front();
        if (!want.empty()) loadAvatar(s, want);
        if (!s.avatar) return;
    }
    { // hot reload: touch <config>/figura/reload.txt (re-reads active.json, tuning.json and the avatar bundle)
        int64_t t = nowMs();
        if (t - s.lastReloadCheckMs > 1000) {
            s.lastReloadCheckMs = t;
            std::error_code ec;
            auto mt = fs::last_write_time(s.root / "reload.txt", ec);
            if (!ec) {
                auto v = mt.time_since_epoch().count();
                if (s.reloadStamp != 0 && v != s.reloadStamp) {
                    std::string n = s.avatarName;
                    if (auto cfg = lyrics::json::parse(readAll(s.root / "active.json"))) n = (*cfg)["avatar"].str();
                    s.host->lastError.clear();
                    if (f.player && !n.empty() && n != s.avatarName) {
                        // Bedrock keeps the resource pack (geometry/animations of the installed avatar) it read at
                        // world load; driving the old geometry with another avatar's bone vars draws garbage.
                        // Keep the current avatar until the player re-joins a world.
                        s.pendingAvatar = n;
                        s.status = "FiguraBridge: '" + n + "' installed - leave and re-join the world to switch";
                        s.host->write(s.status);
                    } else {
                        s.pendingAvatar.clear();
                        s.avatar.reset();
                        s.names.clear();
                        if (!n.empty()) loadAvatar(s, n);
                        if (!s.avatar) return;
                    }
                }
                s.reloadStamp = v;
            }
        }
    }
    if (!f.player) return;
    LocalPlayer& lp = *f.player;
    if (s.nameMode < 0) calibrate(s, lp);
    if (s.lastPlayer != f.player) { // joined a world: the game reloaded pack textures, push ours again
        s.lastPlayer = f.player;
        s.lastFullWriteMs = 0; // new actor: write every variable again
        // the pack Bedrock just loaded holds whatever avatar is installed now (active.json), even without a reload
        if (s.pendingAvatar.empty())
            if (auto cfg = lyrics::json::parse(readAll(s.root / "active.json"))) {
                std::string n = (*cfg)["avatar"].str();
                if (!n.empty() && n != s.avatarName && fs::exists(s.root / n / "rig.json")) s.pendingAvatar = n;
            }
        if (!s.pendingAvatar.empty()) { // the pack now holds the newly installed avatar: switch to it
            std::string n = std::move(s.pendingAvatar);
            s.pendingAvatar.clear();
            s.avatar.reset();
            s.names.clear();
            loadAvatar(s, n);
            if (!s.avatar) return;
            if (s.nameMode < 0) calibrate(s, lp);
        }
        texMarkDirty(*s.avatar);
    }
    int64_t now = nowMs();
    if (s.lastTickMs == 0) s.lastTickMs = now - 50;
    int ticks = 0;
    while (now - s.lastTickMs >= 50 && ticks < 3) {
        s.lastTickMs += 50;
        samplePlayer(s, lp, true);
        s.avatar->tick();
        ticks++;
    }
    if (now - s.lastTickMs > 500) s.lastTickMs = now;
    samplePlayer(s, lp, false);
    float delta = float(std::clamp(double(now - s.lastTickMs) / 50.0, 0.0, 1.0));
    s.avatar->renderMode = s.avatar->player.isFirstPerson ? "FIRST_PERSON" : "RENDER";
    s.avatar->frame(delta);
    writeVars(s, lp);
    s.frameNo++;
    if (s.dumpAtFrame >= 0 && s.frameNo >= s.dumpAtFrame) { // after a wheel action: what Bedrock actually holds
        s.dumpAtFrame = -1;
        std::map<std::pair<int, int>, int> texCount;
        for (auto const& o : s.avatar->out)
            if (o.visible && o.opacity > 0.001f) texCount[{o.primary, o.secondary}]++;
        std::string line = "[figurabridge] state: visible bones by (primary,secondary):";
        for (auto const& [k, c] : texCount) line += " (" + std::to_string(k.first) + "," + std::to_string(k.second) + ")x" + std::to_string(c);
        line += " | in use (runtime -> actor):";
        int m = s.nameMode < 0 ? 0 : s.nameMode;
        for (auto const& [fi, n] : s.useNames)
            if (fi >= 0 && size_t(fi) < s.avatar->fileUsed.size() && s.avatar->fileUsed[size_t(fi)])
                line += " " + std::to_string(fi) + "->" + std::to_string(int(readVar(lp, vn(m, "fbuse" + std::to_string(fi)).c_str())));
        for (size_t b = 0; b < s.avatar->out.size(); b++)
            if (s.avatar->out[b].visible && s.avatar->out[b].secondary >= 0) {
                std::string bn = "fb" + std::to_string(b);
                line += " | bone " + std::to_string(b) + " e=" + std::to_string(s.avatar->out[b].secondary) + " actor e=" +
                        std::to_string(int(readVar(lp, vn(m, bn + "e").c_str()))) + " v=" + std::to_string(int(readVar(lp, vn(m, bn + "v").c_str())));
                break;
            }
        s.host->write(line);
    }
    wheelFrame(s, f);
    if (!s.host->lastError.empty()) f.infoLines.push_back("Figura error: " + s.host->lastError.substr(0, 120));
    if (auto t = s.host->titleText(); !t.empty()) f.infoLines.push_back(t + (s.host->subtitle.empty() ? "" : "  - " + s.host->subtitle));
    if (auto t = s.host->actionbarText(); !t.empty()) f.infoLines.push_back(t);
    if (s.avatar->tune("debug", 0) > 0) {
        std::string pl;
        for (auto& an : s.avatar->animations)
            if (an && an->playState != PlayState::Stopped) pl += an->name + "(" + std::to_string(int(an->blend * 100)) + ") ";
        f.infoLines.push_back("Figura anims: " + pl);
        auto const& pp = s.avatar->player;
        char b[320];
        std::snprintf(b, sizeof b, "Figura v=%.2f,%.2f,%.2f ground=%d sneak=%d yaw=%.0f body=%.0f cam=%.0f,%.0f fp=%d", pp.velocity.x, pp.velocity.y, pp.velocity.z,
                      int(pp.onGround), int(pp.crouching), pp.yaw, pp.bodyYaw, s.avatar->cameraPitch, s.avatar->cameraYaw, int(pp.isFirstPerson));
        f.infoLines.push_back(b);
        // tuning "probe" = part index: what the actor's Molang map really holds for that part (written vs read back)
        if (int pi = int(s.avatar->tune("probe", -1)); pi >= 0 && pi < int(s.avatar->out.size()) && pi < int(s.names.size())) {
            auto const& o  = s.avatar->out[pi];
            std::string bn = "fb" + std::to_string(pi);
            auto rd = [&](char const* suf) { return readVar(lp, vn(s.nameMode, bn + suf).c_str()); };
            std::snprintf(b, sizeof b, "probe p%d v=%d t=%d r=%.1f,%.1f,%.1f p=%.2f,%.2f,%.2f s=%.2f,%.2f,%.2f | map v=%.0f t=%.0f r=%.1f,%.1f,%.1f p=%.2f,%.2f,%.2f s=%.2f",
                          pi, int(o.visible && o.opacity > 0.001f), int(o.primary), o.bone.rot.x, o.bone.rot.y, o.bone.rot.z, o.bone.pos.x,
                          o.bone.pos.y, o.bone.pos.z, o.bone.scale.x, o.bone.scale.y, o.bone.scale.z, rd("v"), rd("t"), rd("rx"), rd("ry"),
                          rd("rz"), rd("px"), rd("py"), rd("pz"), rd("sx"));
            f.infoLines.push_back(b);
            std::snprintf(b, sizeof b, "probe uv out %.3f,%.3f s%.2f,%.2f | map %.3f,%.3f", o.uv[0], o.uv[1], o.uv[2], o.uv[3], rd("uu"), rd("uv"));
            f.infoLines.push_back(b);
        }
    }
}
} // namespace

void install(fs::path const& modDir, fs::path const& configDir) {
    if (gS) return;
    gS       = new State();
    gS->root = configDir / "figura";
    std::error_code ec;
    fs::create_directories(gS->root, ec);
    gS->host          = std::make_unique<BedrockHost>();
    gS->host->logFile = configDir / "figurabridge.log";
    if (auto cfg = lyrics::json::parse(readAll(gS->root / "active.json"))) gS->avatarName = (*cfg)["avatar"].str();
    (void)modDir;
    auto& m = bedrock_forge::client::Registry::get().add(
        "figurabridge", "FiguraBridge", bedrock_forge::client::Category::Visual,
        "Figura avatars on Bedrock: runs the avatar's Lua scripts + Blockbench animations (hold B: action wheel)", true);
    m.hud      = [](bedrock_forge::client::Frame& f) { hudFrame(f); };
    m.onToggle = [](bool on) {
        if (!gS) return;
        std::lock_guard lk(gS->mutex);
        if (!on) {
            gS->avatar.reset();
            gS->nameMode = -1;
        }
    };
}

void uninstall() {
    if (!gS) return;
    std::lock_guard lk(gS->mutex);
    gS->avatar.reset();
}

} // namespace bf::figura::win
#endif
