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
#include <mutex>
#include <set>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/PlaySoundOptions.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "client/Module.h"
#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"
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

// ---------------------------------------------------------------- host
struct BedrockHost final : IHost {
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
    bool                          wheelHeld = false;
    std::set<int>                 digitsDown;
    std::string                   status;
};
State* gS = nullptr;

std::string vn(int mode, std::string const& n) { return mode == 0 ? "variable." + n : n; }

void buildNames(State& s) {
    s.names.clear();
    if (!s.avatar) return;
    int m = s.nameMode < 0 ? 0 : s.nameMode;
    for (size_t i = 0; i < s.avatar->parts.size(); i++) {
        std::string b = "fb" + std::to_string(i);
        s.names.push_back({HashedString(vn(m, b + "rx")), HashedString(vn(m, b + "ry")), HashedString(vn(m, b + "rz")),
                           HashedString(vn(m, b + "px")), HashedString(vn(m, b + "py")), HashedString(vn(m, b + "pz")),
                           HashedString(vn(m, b + "sx")), HashedString(vn(m, b + "sy")), HashedString(vn(m, b + "sz")),
                           HashedString(vn(m, b + "v")), HashedString(vn(m, b + "t")), HashedString(vn(m, b + "e"))});
    }
    s.van = std::make_unique<HashedString>(vn(m, "fbvan"));
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
    for (size_t i = 0; i < out.size() && i < s.names.size(); i++) {
        auto const& o = out[i];
        auto const& n = s.names[i];
        set(n.rx, o.bone.rot.x);
        set(n.ry, o.bone.rot.y);
        set(n.rz, o.bone.rot.z);
        set(n.px, o.bone.pos.x);
        set(n.py, o.bone.pos.y);
        set(n.pz, o.bone.pos.z);
        set(n.sx, o.bone.scale.x);
        set(n.sy, o.bone.scale.y);
        set(n.sz, o.bone.scale.z);
        set(n.v, o.visible && o.opacity > 0.001f ? 1.0 : 0.0);
        set(n.t, o.primary);
        set(n.e, o.secondary);
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

void samplePlayer(State& s, LocalPlayer& lp, bool tick) {
    Actor&       a = lp;
    PlayerState& p = s.avatar->player;
    auto q = [&](char const* expr) { return a.evalMolang(expr); };
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
        p.crouching = q("query.is_sneaking") > 0.5f;
        p.sprinting = q("query.is_sprinting") > 0.5f;
        p.swimming  = q("query.is_swimming") > 0.5f;
        p.gliding   = q("query.is_gliding") > 0.5f;
        p.sleeping  = q("query.is_sleeping") > 0.5f;
        p.inWater   = q("query.is_in_water") > 0.5f;
        p.onFire    = q("query.is_on_fire") > 0.5f;
        p.invisible = q("query.is_invisible") > 0.5f;
        p.alive     = q("query.is_alive") > 0.5f;
        p.usingItem = q("query.is_using_item") > 0.5f;
        p.health    = q("query.health");
        p.maxHealth = q("query.max_health");
        p.climbing  = q("query.is_wall_climbing") > 0.5f;
        p.jumping   = q("query.is_jumping") > 0.5f;
        p.pose      = p.sleeping ? "SLEEPING" : p.gliding ? "FALL_FLYING" : p.swimming ? "SWIMMING" : p.crouching ? "CROUCHING" : "STANDING";
        float used  = q("query.main_hand_item_use_duration");
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
        float attack = q("variable.attack_time");
        bool  sw     = attack > 0.0f;
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
    p.bodyYaw   = q("query.body_y_rotation");
    p.attackAnim = std::clamp(q("variable.attack_time"), 0.0f, 1.0f);
    p.isFirstPerson = q("variable.is_first_person") > 0.5f;
    // the real camera (third-person front, /camera ...) for Figura's Camera parts
    s.avatar->cameraPitch = q("query.camera_rotation(0)");
    s.avatar->cameraYaw   = q("query.camera_rotation(1)");
    s.avatar->cameraSet   = s.avatar->tune("useCamera", 1) > 0;
}

// ---------------------------------------------------------------- avatar loading
bool loadAvatar(State& s, std::string const& name) {
    fs::path dir = s.root / name;
    auto     av  = std::make_unique<Avatar>(s.host.get());
    std::string err;
    s.host->soundEvents.clear();
    if (auto rig = lyrics::json::parse(readAll(dir / "rig.json")))
        for (auto const& [k, v] : (*rig)["sounds"].members()) s.host->soundEvents[k] = v.str();
    if (!av->load(dir / "avatar", dir / "rig.json", &err)) {
        s.status = "FiguraBridge: failed to load '" + name + "': " + err;
        s.host->write(s.status);
        return false;
    }
    s.avatar     = std::move(av);
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
void wheelFrame(State& s, bedrock_forge::client::Frame& f) {
    bool held = gameFocused() && (GetAsyncKeyState('B') & 0x8000);
    s.wheelHeld = held;
    if (!held || !s.avatar || !s.avatar->L) return;
    lua_State* L = s.avatar->L;
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.wheel");
    if (!lua_isfunction(L, -1) || lua_pcall(L, 0, 1, 0) != 0 || !lua_istable(L, -1)) {
        lua_pop(L, 1);
        f.infoLines.push_back("Action wheel: (no page)");
        return;
    }
    lua_getfield(L, -1, "title");
    f.infoLines.push_back(std::string("Action wheel: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "") + "  (1-8)");
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
    // digit keys execute (edge)
    for (int i = 1; i <= 8; i++) {
        bool d = (GetAsyncKeyState('0' + i) & 0x8000) != 0;
        if (d && !s.digitsDown.count(i)) {
            s.digitsDown.insert(i);
            lua_getglobal(L, "action_wheel");
            lua_getfield(L, -1, "execute");
            lua_pushvalue(L, -2);
            lua_pushinteger(L, i);
            lua_pushboolean(L, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            if (lua_pcall(L, 3, 0, 0) != 0) {
                s.host->error(lua_tostring(L, -1) ? lua_tostring(L, -1) : "action error");
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
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
                    if (auto cfg = lyrics::json::parse(readAll(s.root / "active.json"))) s.avatarName = (*cfg)["avatar"].str();
                    s.host->lastError.clear();
                    std::string n = s.avatarName.empty() ? s.avatarName : s.avatarName;
                    s.avatar.reset();
                    s.names.clear();
                    if (!n.empty()) loadAvatar(s, n);
                    if (!s.avatar) return;
                }
                s.reloadStamp = v;
            }
        }
    }
    if (!f.player) return;
    LocalPlayer& lp = *f.player;
    if (s.nameMode < 0) calibrate(s, lp);
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
    wheelFrame(s, f);
    if (!s.host->lastError.empty()) f.infoLines.push_back("Figura error: " + s.host->lastError.substr(0, 120));
    if (s.avatar->tune("debug", 0) > 0) {
        std::string pl;
        for (auto& an : s.avatar->animations)
            if (an && an->playState != PlayState::Stopped) pl += an->name + "(" + std::to_string(int(an->blend * 100)) + ") ";
        f.infoLines.push_back("Figura anims: " + pl);
        auto const& pp = s.avatar->player;
        char b[160];
        std::snprintf(b, sizeof b, "Figura v=%.2f,%.2f,%.2f ground=%d sneak=%d yaw=%.0f body=%.0f cam=%.0f,%.0f fp=%d", pp.velocity.x, pp.velocity.y, pp.velocity.z,
                      int(pp.onGround), int(pp.crouching), pp.yaw, pp.bodyYaw, s.avatar->cameraPitch, s.avatar->cameraYaw, int(pp.isFirstPerson));
        f.infoLines.push_back(b);
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
