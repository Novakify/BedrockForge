// FiguraBridge avatar runtime (Figura 0.1.6 Avatar / FiguraLuaRuntime / FiguraModelPartReader / renderer traversal).
#include "figura/core/Avatar.h"
#include "figura/core/LuaTex.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "figura/core/LuaBind.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

namespace json = bf::lyrics::json;
using namespace bf::figura::lua;

namespace {
std::string readFile(std::filesystem::path const& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
Vec3 vec3Of(json::Value const& v, Vec3 def = {}) {
    if (!v.isArray() || v.size() < 3) return def;
    return {v[0].asNumber(), v[1].asNumber(), v[2].asNumber()};
}
char const kAvatarKey = 0; // registry[&kAvatarKey] = Avatar* (lightuserdata)

/// Figura's FiguraLuaRuntime instruction limit hook ("Script overran resource limits!")
void countHook(lua_State* L, lua_Debug*) {
    lua_pushlightuserdata(L, (void*)&kAvatarKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* a = static_cast<Avatar*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!a) return;
    a->instructionsUsed += 1000;
    if (a->instructionsUsed > a->instructionLimit) {
        a->instructionsUsed = 0;
        luaL_error(L, "Script overran resource limits!");
    }
}

int traceback(lua_State* L) {
    char const* msg = lua_tostring(L, 1);
    if (!msg) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}
} // namespace

Avatar* avatarOf(lua_State* L) {
    lua_pushlightuserdata(L, (void*)&kAvatarKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* a = static_cast<Avatar*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return a;
}

Avatar::Avatar(IHost* h) : host(h) {
    clock.millis = [this] { return host->millis(); };
    clock.paused = [this] { return host->paused(); };
}

Avatar::~Avatar() {
    if (L) lua_close(L);
}

void Avatar::reportError(std::string const& msg) {
    scriptError = true;
    lastError   = msg;
    if (host) host->error(msg);
    if (onError) onError(msg);
}

// ---------------------------------------------------------------- loading
bool Avatar::load(std::filesystem::path const& bundleDir, std::filesystem::path const& rigJson, std::string* err) {
    dir = bundleDir;
    std::string nbtText = readFile(bundleDir / "figura_avatar.json");
    if (nbtText.empty()) {
        if (err) *err = "missing figura_avatar.json in " + bundleDir.string();
        return false;
    }
    std::string rigText = readFile(rigJson);
    auto        nbt     = json::parse(nbtText);
    auto        rig     = json::parse(rigText.empty() ? "{}" : rigText);
    if (!nbt) {
        if (err) *err = "figura_avatar.json is not valid JSON";
        return false;
    }
    if (!buildFromNbt(*nbt, rig ? *rig : json::Value::makeObject(), err)) return false;
    nbtJson = std::make_shared<json::Value>(*nbt);
    for (auto& p : parts) compiledPivots.push_back(p->pivot); // fixed Bedrock bone pivots (before any script runs)
    compiledParts = int(parts.size());
    for (auto& p : parts) {
        p->bone = p->index;
        boneParent.push_back(p->parent ? p->parent->index : -1);
    }
    // dup bones (compile.py "dups"): free-floating copies of a part's geometry for script-made copies
    if (rig && (*rig)["dups"].isArray())
        for (auto const& d : (*rig)["dups"].items()) {
            int i = int(d["i"].asNumber()), src = int(d["source"].asNumber());
            if (i != int(boneParent.size()) || src < 0 || src >= compiledParts) break; // must follow the parts in order
            boneParent.push_back(-1);
            compiledPivots.push_back(compiledPivots[size_t(src)]);
            dupSlots[src].push_back(i);
        }
    out.resize(boneParent.size());
    boneModel.resize(boneParent.size());
    boneWorld.resize(boneParent.size());
    boneClaimed.assign(boneParent.size(), 0);
    if (rig && (*rig)["itemPivots"].isObject())
        for (int side = 0; side < 2; side++) {
            auto const& ip = (*rig)["itemPivots"][side == 0 ? "right" : "left"];
            if (!ip.isObject()) continue;
            itemOut[side].configured = true;
            itemOut[side].pivot      = vec3Of(ip["pivot"]);
            itemOut[side].hand       = vec3Of(ip["hand"], {side == 0 ? 6.0 : -6.0, 12, -2});
        }
    // render task bones: free-floating item quads / block cubes (compile.py) placed by the bone solve
    if (rig && (*rig)["taskBones"].isArray())
        for (auto const& tb : (*rig)["taskBones"].items()) {
            int i = int(tb["i"].asNumber());
            if (i != int(boneParent.size())) break;
            boneParent.push_back(-1);
            compiledPivots.push_back({});
            if (tb["kind"].str() == "quad") {
                QuadSlot q;
                q.bone = i;
                q.file = tb["file"].isNumber() ? int(tb["file"].asNumber()) : -1;
                quadSlots.push_back(q);
            } else
                taskBones[tb["kind"].str() + ":" + tb["id"].str()].push_back(i);
        }
    if (rig && (*rig)["gatedFiles"].isArray())
        for (auto const& g : (*rig)["gatedFiles"].items()) gatedFiles.push_back(int(g.asNumber()));
    boneFiles.assign(boneParent.size(), {});
    if (rig && (*rig)["boneFiles"].isObject())
        for (auto const& [k, v] : (*rig)["boneFiles"].members()) {
            size_t b = size_t(std::strtoul(k.c_str(), nullptr, 10));
            if (b < boneFiles.size() && v.isArray())
                for (auto const& f : v.items()) boneFiles[b].push_back(int(f.asNumber()));
        }
    if (rig && (*rig)["vanillaTextures"].isObject())
        for (auto const& [k, v] : (*rig)["vanillaTextures"].members()) vanillaTextures[k] = v.str();
    out.resize(boneParent.size());
    boneModel.resize(boneParent.size());
    boneWorld.resize(boneParent.size());
    boneClaimed.assign(boneParent.size(), 0);
    uvBones.assign(boneParent.size(), 0);
    if (rig && (*rig)["uvParts"].isArray())
        for (auto const& u : (*rig)["uvParts"].items()) {
            int i = int(u.asNumber());
            if (i >= 0 && i < int(uvBones.size())) uvBones[size_t(i)] = 1;
        }
    if (!initLua(err)) return false;
    loaded = true;
    initScripts();
    // autoAnims (Avatar.loadAnimations)
    for (auto& an : animations)
        if (std::find(autoAnims.begin(), autoAnims.end(), an->modelName + "." + an->name) != autoAnims.end()) an->play(clock);
    return true;
}

bool Avatar::buildFromNbt(json::Value const& nbt, json::Value const& rig, std::string* err) {
    auto const& meta = nbt["metadata"];
    name             = meta["name"].str();
    color            = meta["color"].str();
    if (meta["authors"].isArray())
        for (auto const& a : meta["authors"].items()) author += (author.empty() ? "" : ", ") + a.str();
    else if (meta["authors"].isString()) author = meta["authors"].str();
    else author = meta["author"].str();
    if (meta["autoScripts"].isArray())
        for (auto const& s : meta["autoScripts"].items()) autoScripts.push_back(s.str());
    if (meta["autoAnims"].isArray())
        for (auto const& s : meta["autoAnims"].items()) autoAnims.push_back(s.str());

    // scripts
    for (auto const& [k, v] : nbt["scripts"].members()) {
        if (v.isString()) scripts[k] = v.asString();
        else if (v.isObject() && v.has("__file")) scripts[k] = readFile(dir / v["__file"].asString());
    }

    // textures (rig.json is the source of the file list + Bedrock indices)
    for (auto const& t : rig["textureFiles"].items()) {
        TextureFile f;
        f.index      = int(t["i"].asInt(int64_t(textureFiles.size())));
        f.figuraName = t["figuraName"].str();
        f.set        = int(t["set"].asInt(-1));
        std::string k = t["type"].str();
        f.kind = k.empty() ? 'd' : k[0];
        f.file = t["file"].str();
        f.w    = int(t["w"].asInt(0));
        f.h    = int(t["h"].asInt(0));
        f.rpTexture = t["rpTexture"].str();
        f.dyn       = t.has("dyn") ? int(t["dyn"].asInt(-1)) : -1;
        textureFiles.push_back(f);
    }
    for (auto const& s : rig["textureSets"].items()) {
        TextureSet ts;
        ts.name = s["name"].str();
        ts.d    = s["d"].isNumber() ? int(s["d"].asInt()) : -1;
        ts.e    = s["e"].isNumber() ? int(s["e"].asInt()) : -1;
        textureSets.push_back(ts);
    }
    for (auto const& [k, v] : rig["sounds"].members()) soundEvents[k] = v.str();

    // animations first (parts reference them by id) - Avatar.loadAnimations
    auto const& anims = nbt["animations"];
    for (size_t i = 0; i < anims.size(); i++) {
        auto const& a = anims[i];
        if (!a.has("mdl") || !a.has("name")) {
            animations.push_back(nullptr);
            continue;
        }
        LoopMode lm = LoopMode::Once;
        if (a.has("loop")) loopModeFromName(a["loop"].str(), lm);
        auto an = std::make_unique<Animation>(a["mdl"].str(), a["name"].str(), lm, a["ovr"].asBool(false),
                                              float(a["len"].asNumber(0)), float(a["off"].asNumber(0)),
                                              a.has("bld") ? float(a["bld"].asNumber(1)) : 1.0f,
                                              float(a["sdel"].asNumber(0)), float(a["ldel"].asNumber(0)));
        an->index = int(i);
        if (a["code"].isArray())
            for (auto const& c : a["code"].items()) an->newCode(float(c["time"].asNumber()), c["src"].str());
        animations.push_back(std::move(an));
    }

    // models
    if (nbt.has("models")) root = readPart(nbt["models"], nullptr);
    else root = addPart("models", nullptr);
    root->name = "models";
    (void)err;
    return true;
}

Part* Avatar::addPart(std::string const& n, Part* parent) {
    auto p   = std::make_unique<Part>();
    p->index = int(parts.size());
    p->name  = n;
    p->parent = parent;
    if (parent) parent->children.push_back(p.get());
    parts.push_back(std::move(p));
    return parts.back().get();
}

Part* Avatar::readPart(json::Value const& pc, Part* parent) {
    Part* p     = addPart(pc["name"].str(), parent);
    p->rotation = vec3Of(pc["rot"]);
    p->pivot    = vec3Of(pc["piv"]);
    if (pc.has("primary")) p->primaryRenderType = pc["primary"].str();
    if (pc.has("secondary")) p->secondaryRenderType = pc["secondary"].str();
    if (pc.has("vsb")) p->visible = pc["vsb"].asBool(true);
    auto const& cube = pc["cube_data"];
    auto const& mesh = pc["mesh_data"];
    if (cube.isObject() && !cube.members().empty()) {
        p->type = PartType::Cube;
        for (char const* f : {"n", "s", "e", "w", "u", "d"})
            if (cube[f].isObject() && cube[f].has("tex")) {
                p->defaultSet = int(cube[f]["tex"].asInt());
                break;
            }
    } else if (mesh.isObject() && !mesh.members().empty()) {
        p->type = PartType::Mesh;
        auto const& tex = mesh["tex"];
        if (tex.isArray() && tex.size() > 0) p->defaultSet = int(int64_t(tex[0].asNumber()) >> 4); // packed (id << 4 | n)
    }
    if (pc.has("pt")) {
        bool ok = false;
        ParentType t = parentTypeFromName(pc["pt"].str(), &ok);
        if (ok) p->parentType = t;
    }
    for (auto const& c : pc["chld"].items()) readPart(c, p);
    if (pc["anim"].isArray()) readAnimData(p, pc["anim"]);
    return p;
}

void Avatar::readAnimData(Part* part, json::Value const& list) {
    for (auto const& a : list.items()) {
        if (!a.has("id") || !a.has("data")) continue;
        int id = int(a["id"].asInt());
        if (id < 0 || id >= int(animations.size()) || !animations[id]) continue;
        Animation* anim = animations[id].get();
        for (auto const& [ch, frames] : a["data"].members()) {
            TransformType type;
            if (ch == "pos") type = TransformType::Position;
            else if (ch == "rot") type = TransformType::Rotation;
            else if (ch == "grot") type = TransformType::GlobalRot;
            else if (ch == "scl") type = TransformType::Scale;
            else continue;
            AnimationChannel channel{type, {}};
            for (auto const& kn : frames.items()) {
                Keyframe k;
                k.time = float(kn["time"].asNumber());
                std::string it = kn["int"].str();
                for (auto& c : it) c = char(std::toupper(static_cast<unsigned char>(c)));
                if (it == "LINEAR") k.interpolation = Interpolation::Linear;
                else if (it == "CATMULLROM") k.interpolation = Interpolation::CatmullRom;
                else if (it == "BEZIER") k.interpolation = Interpolation::Bezier;
                else if (it == "STEP") k.interpolation = Interpolation::Step;
                else continue; // "Invalid interpolation type" -> skipped like Figura
                auto parseData = [](json::Value const& d, std::optional<Vec3>& lit, std::array<std::string, 3>& code) -> bool {
                    if (!d.isArray() || d.size() < 3) return false;
                    if (d[0].isNumber() && d[1].isNumber() && d[2].isNumber()) lit = Vec3{d[0].asNumber(), d[1].asNumber(), d[2].asNumber()};
                    else for (int i = 0; i < 3; i++) code[i] = d[i].str();
                    return true;
                };
                bool hasPre = parseData(kn["pre"], k.targetA, k.aCode);
                if (!hasPre) k.targetA = Vec3{};
                if (!parseData(kn["end"], k.targetB, k.bCode)) {
                    k.targetB = k.targetA;
                    k.bCode   = k.aCode;
                }
                k.bezierLeft      = vec3Of(kn["bl"]);
                k.bezierRight     = vec3Of(kn["br"]);
                k.bezierLeftTime  = vec3Of(kn["blt"], {-0.1, -0.1, -0.1});
                k.bezierRightTime = vec3Of(kn["brt"], {0.1, 0.1, 0.1});
                k.bezierLeftTime += Vec3{1, 1, 1};
                auto clamp01 = [](Vec3 v) { return Vec3{std::clamp(v.x, 0.0, 1.0), std::clamp(v.y, 0.0, 1.0), std::clamp(v.z, 0.0, 1.0)}; };
                k.bezierLeftTime  = clamp01(k.bezierLeftTime);
                k.bezierRightTime = clamp01(k.bezierRightTime);
                k.part      = part;
                k.channel   = type;
                k.animation = anim;
                channel.keyframes.push_back(std::move(k));
            }
            std::stable_sort(channel.keyframes.begin(), channel.keyframes.end(),
                             [](Keyframe const& x, Keyframe const& y) { return x.time < y.time; });
            anim->addAnimation(part, std::move(channel));
        }
    }
}

Animation* Avatar::findAnimation(std::string const& mdl, std::string const& n) {
    for (auto& a : animations)
        if (a && a->modelName == mdl && a->name == n) return a.get();
    return nullptr;
}

int Avatar::fileIndexByFiguraName(std::string const& n) const {
    for (auto const& f : textureFiles)
        if (f.figuraName == n) return f.index;
    return -1;
}

// ---------------------------------------------------------------- Lua
static int luaRequire(lua_State* L);
static int luaLoad(lua_State* L);
static int luaListFiles(lua_State* L);

bool Avatar::initLua(std::string* err) {
    L = luaL_newstate();
    if (!L) {
        if (err) *err = "lua_newstate failed";
        return false;
    }
    lua_pushlightuserdata(L, (void*)&kAvatarKey);
    lua_pushlightuserdata(L, this);
    lua_rawset(L, LUA_REGISTRYINDEX);
    // Globals: JseBaseLib, Bit32, Table, String, Math, Debug (Figura removes debug in sandbox.lua)
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, "bit32", luaopen_bit32, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
    luaL_requiref(L, "debug", luaopen_debug, 1);
    lua_pop(L, 7);
    // keep debug.traceback for our error messages before the sandbox removes `debug`
    lua_newtable(L);
    lua_setglobal(L, "figuraMetatables");
    lua_sethook(L, countHook, LUA_MASKCOUNT, 1000);

    // sandbox.lua (verbatim from Figura)
    static char const* kSandbox =
        "_VERSION = \"Lua 5.2 - Figura\"\n"
        "debug = nil\ndofile = nil\nloadfile = nil\ncollectgarbage = nil\n_GS = _G\n";
    if (luaL_dostring(L, kSandbox) != 0) {
        if (err) *err = lua_tostring(L, -1);
        return false;
    }
    // string metatable read-only like ReadOnlyLuaTable
    openVectors(L);
    openMatrices(L);
    openFiguraApis(*this);

    lua_pushcfunction(L, luaRequire);
    lua_setglobal(L, "require");
    lua_pushcfunction(L, luaLoad);
    lua_setglobal(L, "load");
    lua_pushcfunction(L, luaLoad);
    lua_setglobal(L, "loadstring");
    lua_pushcfunction(L, luaListFiles);
    lua_setglobal(L, "listFiles");

    // prelude (Lua part of the API) + Figura's math.lua
    if (luaL_loadbuffer(L, figuraPreludeSource(), std::strlen(figuraPreludeSource()), "=figurabridge prelude") != 0 ||
        lua_pcall(L, 0, 0, 0) != 0) {
        if (err) *err = std::string("prelude: ") + lua_tostring(L, -1);
        return false;
    }
    return true;
}

bool Avatar::runChunk(int nargs, int nresults) {
    if (!L) return false;
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    instructionsUsed = 0;
    int rc = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (rc != 0) {
        std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
        lua_pop(L, 1);
        reportError(msg);
        return false;
    }
    return true;
}

// require(name): Figura PathUtils semantics (dots or slashes, relative to the calling script's folder)
static std::string normScript(std::string s) {
    for (auto& c : s)
        if (c == '\\') c = '/';
    if (s.size() > 4 && s.substr(s.size() - 4) == ".lua") s.resize(s.size() - 4);
    // collapse "./" and "a/../"
    std::vector<std::string> parts;
    std::string cur;
    std::stringstream ss(s);
    while (std::getline(ss, cur, '/')) {
        if (cur.empty() || cur == ".") continue;
        if (cur == ".." && !parts.empty()) parts.pop_back();
        else parts.push_back(cur);
    }
    std::string r;
    for (auto& p : parts) r += (r.empty() ? "" : ".") + p;
    return r;
}

static int luaInitScript(lua_State* L, Avatar* a, std::string const& nm) {
    // loaded cache: registry "fb.loaded"[name]
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.loaded");
    lua_getfield(L, -1, nm.c_str());
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);
        return 1;
    }
    lua_pop(L, 2);
    auto it = a->scripts.find(nm);
    if (it == a->scripts.end()) return luaL_error(L, "Tried to require nonexistent script \"%s\"!", nm.c_str());
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.loading");
    lua_getfield(L, -1, nm.c_str());
    if (lua_toboolean(L, -1)) return luaL_error(L, "Detected circular dependency in script %s", nm.c_str());
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, nm.c_str());
    lua_pop(L, 1);
    if (luaL_loadbuffer(L, it->second.data(), it->second.size(), nm.c_str()) != 0) return lua_error(L);
    auto dot = nm.rfind('.');
    lua_pushstring(L, dot == std::string::npos ? "" : nm.substr(0, dot).c_str());
    lua_pushstring(L, dot == std::string::npos ? nm.c_str() : nm.substr(dot + 1).c_str());
    lua_call(L, 2, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.loaded");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, nm.c_str());
    lua_pop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.loading");
    lua_pushnil(L);
    lua_setfield(L, -2, nm.c_str());
    lua_pop(L, 1);
    return 1;
}

static int luaRequire(lua_State* L) {
    Avatar*     a   = avatarOf(L);
    std::string arg = luaL_checkstring(L, 1);
    std::string path = arg;
    for (auto& c : path)
        if (c == '.') c = '/';
    // working directory of the calling chunk: its source name minus the last segment
    std::string dirName;
    lua_Debug ar;
    if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "S", &ar) && ar.source) {
        std::string src = ar.source;
        if (!src.empty() && (src[0] == '@' || src[0] == '=')) src.erase(0, 1);
        auto dot = src.rfind('.');
        if (a->scripts.count(src) && dot != std::string::npos) dirName = src.substr(0, dot);
        for (auto& c : dirName)
            if (c == '.') c = '/';
    }
    bool        absolute = !path.empty() && path[0] == '/';
    std::string full     = absolute ? path : (dirName.empty() ? path : dirName + "/" + path);
    std::string nm       = normScript(full);
    if (!a->scripts.count(nm)) {
        std::string alt = normScript(path); // fall back to avatar-root relative
        if (a->scripts.count(alt)) nm = alt;
    }
    return luaInitScript(L, a, nm);
}

static int luaLoad(lua_State* L) {
    // loadstring(src|func, chunkName, env): returns chunk or nil, err (Figura loadstringConstructor)
    std::string src;
    if (lua_isfunction(L, 1)) {
        for (;;) {
            lua_pushvalue(L, 1);
            lua_call(L, 0, 1);
            if (!lua_isstring(L, -1) || lua_rawlen(L, -1) == 0) {
                lua_pop(L, 1);
                break;
            }
            src += lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, 1)) {
        size_t n;
        char const* s = lua_tolstring(L, 1, &n);
        src.assign(s, n);
    } else {
        lua_pushnil(L);
        lua_pushstring(L, "chunk source is neither a string nor function");
        return 2;
    }
    std::string chunk = lua_isstring(L, 2) ? lua_tostring(L, 2) : "loadstring";
    if (luaL_loadbufferx(L, src.data(), src.size(), chunk.c_str(), "t") != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (lua_istable(L, 3)) {
        lua_pushvalue(L, 3);
        if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
    }
    return 1;
}

static int luaListFiles(lua_State* L) {
    Avatar*     a      = avatarOf(L);
    std::string folder = normScript(optStr(L, 1, ""));
    bool        sub    = optBool(L, 2, false);
    lua_newtable(L);
    int i = 1;
    for (auto const& [k, v] : a->scripts) {
        if (!folder.empty() && k.rfind(folder + ".", 0) != 0) continue;
        std::string rest = folder.empty() ? k : k.substr(folder.size() + 1);
        if (!sub && rest.find('.') != std::string::npos) continue;
        lua_pushstring(L, k.c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

void Avatar::initScripts() {
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "fb.loaded");
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "fb.loading");
    std::vector<std::string> order;
    if (!autoScripts.empty())
        for (auto const& n : autoScripts) order.push_back(normScript(n)); // PathUtils: "scripts/main" -> "scripts.main"
    else for (auto const& [k, v] : scripts) order.push_back(k);
    for (auto const& nm : order) {
        if (scriptError) break;
        lua_pushcfunction(L, [](lua_State* L2) -> int {
            return luaInitScript(L2, avatarOf(L2), lua_tostring(L2, 1));
        });
        lua_pushstring(L, nm.c_str());
        instructionsUsed = 0;
        if (!runChunk(1, 0)) break;
    }
}

void Avatar::callEvent(char const* ev, int nargs) {
    if (!L || scriptError) {
        if (L) lua_pop(L, nargs);
        return;
    }
    // prelude: __fbEvents.call(name, ...)
    lua_getfield(L, LUA_REGISTRYINDEX, "fb.callEvent");
    lua_insert(L, -(nargs + 1));
    lua_pushstring(L, ev);
    lua_insert(L, -(nargs + 1));
    runChunk(nargs + 1, 0);
}

void Avatar::runPing(std::string const& nm, int argsRef) {
    (void)nm;
    (void)argsRef;
}

// ---------------------------------------------------------------- IAnimScripts (keyframe Lua)
int Avatar::compileKeyframe(std::string const& chunk, std::string const& src, bool* isLiteral, double* literal) {
    *isLiteral = false;
    *literal   = 0;
    if (!ready()) return 0;
    std::string expr = "return " + src;
    if (luaL_loadbuffer(L, expr.data(), expr.size(), chunk.c_str()) != 0) {
        lua_pop(L, 1);
        if (luaL_loadbuffer(L, src.data(), src.size(), chunk.c_str()) != 0) {
            std::string e = lua_tostring(L, -1);
            lua_pop(L, 1);
            reportError("Syntax error in keyframe [" + chunk + "]: " + e);
            return 0;
        }
    }
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

double Avatar::evalKeyframe(int handle, float delta, Animation* anim, std::string const& chunk) {
    if (!ready()) return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, handle);
    lua_getglobal(L, "animations");
    lua_pushnumber(L, delta);
    pushRef(L, anim, typeId("Animation"));
    if (!runChunk(3, 1)) return 0;
    double v = 0;
    if (lua_isnumber(L, -1)) v = lua_tonumber(L, -1);
    else reportError("Failed to parse data from [" + chunk + "]: expected number, but got " + luaL_typename(L, -1));
    lua_pop(L, 1);
    return v;
}

int Avatar::compileCode(std::string const& chunk, std::string const& src) {
    if (!ready()) return 0;
    if (luaL_loadbuffer(L, src.data(), src.size(), chunk.c_str()) != 0) {
        std::string e = lua_tostring(L, -1);
        lua_pop(L, 1);
        reportError(e);
        return 0;
    }
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void Avatar::runCode(int handle, Animation* anim) {
    if (!ready()) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, handle);
    lua_getglobal(L, "animations");
    pushRef(L, anim, typeId("Animation"));
    runChunk(2, 0);
}

// ---------------------------------------------------------------- tick / frame
void Avatar::tick() {
    if (!loaded || scriptError || !L) return;
    if (!userInit && player.loaded) {
        userInit = true;
        callEvent("ENTITY_INIT", 0);
    }
    // keybinds: press/release edges -> prelude
    for (auto& k : keybinds) {
        bool d = k.enabled && host->keyDown(k.key);
        if (d != k.down) {
            k.down = d;
            lua_getfield(L, LUA_REGISTRYINDEX, "fb.keyEdge");
            lua_rawgeti(L, LUA_REGISTRYINDEX, k.ref);
            lua_pushboolean(L, d);
            runChunk(2, 0);
        }
    }
    callEvent("WORLD_TICK", 0);
    if (player.loaded) callEvent("TICK", 0);
}

void Avatar::applyAnimations() {
    int limit = 1 << 20;
    for (auto& a : animations)
        if (a) limit = animationPlayerTick(*a, limit, clock, this);
}

void Avatar::clearAnimations() {
    for (auto& a : animations)
        if (a) animationPlayerClear(*a);
}

// FiguraModelPart.applyExtraTransforms (ParentType.Camera): the part cancels every rotation above it and turns to
// face the camera (rightMultiply rotateY(180)), pivoting around pivot + offsetPivot. Model space = Blockbench space,
// whose front is -Z; the camera's look relative to the body is (pitch, yaw - bodyYaw).
void Avatar::applyCameraTransform(Part* p, Mat4 const& parentMat) {
    float yaw   = cameraSet ? cameraYaw : player.yaw;
    float pitch = cameraSet ? cameraPitch : player.pitch;
    float rel   = yaw - (player.prevBodyYaw + (player.bodyYaw - player.prevBodyYaw) * tickDelta);
    // parent rotation (normalised columns, translation dropped)
    Mat4 inv;
    double c[3][3] = {{parentMat.v11, parentMat.v12, parentMat.v13}, {parentMat.v21, parentMat.v22, parentMat.v23}, {parentMat.v31, parentMat.v32, parentMat.v33}};
    for (int col = 0; col < 3; col++) {
        double l = std::sqrt(c[0][col] * c[0][col] + c[1][col] * c[1][col] + c[2][col] * c[2][col]);
        if (l > 0) for (int r = 0; r < 3; r++) c[r][col] /= l;
    }
    // inverse rotation = transpose
    inv.v11 = c[0][0]; inv.v12 = c[1][0]; inv.v13 = c[2][0];
    inv.v21 = c[0][1]; inv.v22 = c[1][1]; inv.v23 = c[2][1];
    inv.v31 = c[0][2]; inv.v32 = c[1][2]; inv.v33 = c[2][2];
    Mat4 cam;                       // billboard: face the camera (signs tunable: config/figura/tuning.json)
    cam.rotateX(tune("camPitchSign", -1) * pitch);
    cam.rotateY(tune("camYawOffset", 180) + tune("camYawSign", -1) * rel);
    Vec3 piv2 = p->pivot + p->offsetPivot;
    Mat4 m;
    m.translate(-piv2.x, -piv2.y, -piv2.z);
    m.scale(p->stackScale);
    m = cam * m;
    m = inv * m;
    m.translate(piv2.x + p->position.x + p->animPos.x, piv2.y + p->position.y + p->animPos.y, piv2.z + p->position.z + p->animPos.z);
    p->positionMatrix = m;
}

int Avatar::textureFileByName(std::string const& n) const {
    for (auto const& f : textureFiles)
        if (f.figuraName == n) return f.index;
    return -1;
}

int Avatar::customTextureFile(void* tex) {
    if (!tex) return -1;
    auto* t = static_cast<Tex*>(tex);
    if (t->fileIndex >= 0) return t->fileIndex; // one of the avatar's own textures
    // a script texture: the dyn slot it already has, else a free one (slots are handed out first come)
    int freeFile = -1;
    for (auto const& f : textureFiles) {
        if (f.dyn < 0) continue;
        if (size_t(f.dyn) >= dynSlotTex.size()) dynSlotTex.resize(size_t(f.dyn) + 1, nullptr);
        if (dynSlotTex[size_t(f.dyn)] == tex) return f.index;
        if (!dynSlotTex[size_t(f.dyn)] && freeFile < 0) freeFile = f.index;
    }
    if (freeFile < 0) return -1;
    dynSlotTex[size_t(textureFiles[size_t(freeFile)].dyn)] = tex;
    t->dirty = true; // first use: upload its pixels
    return freeFile;
}

bool Avatar::separateType(ParentType t) const {
    // FiguraModelPart parent types rendered in their own pass (never on the player model)
    switch (t) {
    case ParentType::World: case ParentType::Hud: case ParentType::Gui: case ParentType::Skull: case ParentType::Portrait:
    case ParentType::Arrow: case ParentType::Trident: case ParentType::Item: return true;
    default: return false;
    }
}

void Avatar::solveBones() {
    for (size_t b = 0; b < out.size(); b++) {
        int  par = boneParent[b];
        Mat4 pw  = par >= 0 && par < int(b) ? boneWorld[size_t(par)] : Mat4{};
        Mat4 local;
        if (boneClaimed[b]) local = par >= 0 ? affineInverse(pw) * boneModel[b] : boneModel[b];
        else out[b].visible = false;     // nothing draws through this bone this frame
        boneWorld[b] = pw * local;
        out[b].bone  = decomposeForBedrock(local, compiledPivots[b]);
    }
}

void Avatar::traverse(Part* p, TextureOverride prim, TextureOverride sec, bool parentVisible, Mat4 const& parentMat, bool separate,
                      double const* parentUV) {
    if (p->removed) return;
    // PartCustomizationStack: a part's UV matrix applies on top of its parent's (row-major 3x3, texture fractions)
    double uvm[9];
    if (parentUV) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                double v = 0;
                for (int k = 0; k < 3; k++) v += parentUV[r * 3 + k] * p->uvMatrix[k * 3 + c];
                uvm[r * 3 + c] = v;
            }
    } else std::memcpy(uvm, p->uvMatrix, sizeof uvm);
    // WORLD parts: Figura draws them at world coordinates (16 units = 1 block); here they are drawn through the
    // player's bones, mapped into the player model's space each frame (worldToModel)
    bool enterWorld = p->parentType == ParentType::World && !inWorld && !separate;
    separate = separate || (separateType(p->parentType) && !(p->parentType == ParentType::World && (inWorld || enterWorld)));
    bool savedInWorld = inWorld;
    if (enterWorld) inWorld = true;
    if (p->primaryTexture.set) prim = p->primaryTexture;
    if (p->secondaryTexture.set) sec = p->secondaryTexture;
    p->applyVanillaTransforms(vanillaPose.get(p->parentType));
    bool vis = parentVisible && p->visible && !(p->vanillaVisible.has_value() && !*p->vanillaVisible);
    if (vis && p->preRender && L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, p->preRender);
        lua_pushnumber(L, tickDelta);
        lua_pushstring(L, renderMode.c_str());
        pushRef(L, p, typeId("ModelPart"));
        runChunk(3, 0);
    }
    p->recalculate();
    if (p->parentType == ParentType::Camera) applyCameraTransform(p, parentMat);
    if (vis && p->midRender && L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, p->midRender);
        lua_pushnumber(L, tickDelta);
        lua_pushstring(L, renderMode.c_str());
        pushRef(L, p, typeId("ModelPart"));
        runChunk(3, 0);
    }
    Mat4 myMat = (enterWorld ? Mat4{} : parentMat) * p->positionMatrix;
    Mat4 drawMat = inWorld ? worldToModel * myMat : myMat; // model space (what the bone solve wants)
    // which bone draws this part: its own (compiled parts), else a free dup bone of its source (script copies)
    int bone = p->bone;
    if (bone < 0 && p->sourceBone >= 0 && vis) {
        auto it = dupSlots.find(p->sourceBone);
        if (it != dupSlots.end()) {
            if (p->dupSlot >= 0 && !boneClaimed[size_t(p->dupSlot)]) bone = p->dupSlot;
            else
                for (int d : it->second)
                    if (!boneClaimed[size_t(d)]) { bone = d; break; }
            p->dupSlot = bone;
        }
    }
    bool onBody = vis && !separate;
    // Figura renders the held item at the first visible Right/LeftItemPivot: the Bedrock item bone gets the pivot
    // part's frame, the rest item position (hand) moved onto the part's pivot point (X = T(P) * L * T(-hand),
    // decomposed about the bone's pivot q)
    if (onBody && !inWorld && (p->parentType == ParentType::RightItemPivot || p->parentType == ParentType::LeftItemPivot)) {
        auto& io = itemOut[p->parentType == ParentType::RightItemPivot ? 0 : 1];
        if (io.configured && !io.found) {
            Vec3 P = myMat.apply(p->pivot);
            Mat4 x = myMat;
            x.v14 = x.v24 = x.v34 = 0;
            Mat4 t;
            t.translate(P.x, P.y, P.z);
            Mat4 tq;
            tq.translate(-io.hand.x, -io.hand.y, -io.hand.z);
            io.bone  = decomposeForBedrock(t * x * tq, io.pivot);
            io.found = true;
        }
    }
    if (bone >= 0 && bone < int(out.size()) && !boneClaimed[size_t(bone)] && (p->bone >= 0 || onBody)) {
        boneClaimed[size_t(bone)] = 1;
        boneModel[size_t(bone)] = drawMat;
        PartOut& o = out[size_t(bone)];
        o.visible = onBody;
        auto texIndex = [&](TextureOverride const& t, bool primary) -> int {
            if (!t.set) return -1;
            switch (t.kind) {
            case TexKind::Custom: return customTextureFile(t.tex);
            case TexKind::Primary: {
                int s = p->defaultSet;
                return s >= 0 && s < int(textureSets.size()) ? textureSets[s].d : -1;
            }
            case TexKind::Secondary: {
                int s = p->defaultSet;
                return s >= 0 && s < int(textureSets.size()) ? textureSets[s].e : -1;
            }
            case TexKind::Skin: return textureFileByName("__skin");
            case TexKind::Resource: {
                std::string r = t.resource;
                if (r.find(':') == std::string::npos) r = "minecraft:" + r;
                int f = textureFileByName("resource:" + r);
                return f >= 0 ? f : textureFileByName("resource:" + t.resource);
            }
            case TexKind::Cape: return textureFileByName("__cape");       // the player's Bedrock cape
            case TexKind::Elytra: return textureFileByName("__elytra");   // vanilla elytra texture
            default: return -1;
            }
        };
        o.primary   = texIndex(prim, true);
        o.secondary = texIndex(sec, false);
        o.opacity   = p->alpha.value_or(1.0f);
        o.color     = p->color;
        o.uv[0] = uvm[2];
        o.uv[1] = uvm[5];
        o.uv[2] = uvm[0];
        o.uv[3] = uvm[4];
    }
    // render tasks: items / blocks / entities through a task bone holding their vanilla geometry + texture; text and
    // sprites through a quad bone whose runtime texture gets the painted pixels
    if (onBody && !p->tasks.empty() && (!taskBones.empty() || !quadSlots.empty())) {
        std::vector<TaskDraw> draws;
        taskDraws(*this, p, draws);
        for (auto const& d : draws) {
            if (d.kind == 3 || d.kind == 4) {
                drawQuadTask(d, drawMat);
                continue;
            }
            auto it = taskBones.find((d.kind == 1 ? "item:" : d.kind == 2 ? "block:" : "entity:") + d.id);
            if (it == taskBones.end()) continue;
            for (int b : it->second) {
                if (boneClaimed[size_t(b)]) continue;
                boneClaimed[size_t(b)] = 1;
                boneModel[size_t(b)]   = drawMat * d.m;
                PartOut& o = out[size_t(b)];
                o.visible = true;
                o.primary = o.secondary = -1;
                o.opacity = 1;
                break;
            }
        }
    }
    for (Part* c : std::vector<Part*>(p->children)) traverse(c, prim, sec, vis, myMat, separate, uvm);
    inWorld = savedInWorld;
    if (vis && p->postRender && L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, p->postRender);
        lua_pushnumber(L, tickDelta);
        lua_pushstring(L, renderMode.c_str());
        pushRef(L, p, typeId("ModelPart"));
        runChunk(3, 0);
    }
    p->resetVanillaTransforms();
}

void Avatar::computeFileUse() {
    fileUsed.assign(textureFiles.size() + 1, 0);
    auto mark = [&](int f) { if (f >= 0 && size_t(f) < fileUsed.size()) fileUsed[size_t(f)] = 1; };
    for (size_t b = 0; b < out.size(); b++) {
        PartOut const& o = out[b];
        if (!o.visible || o.opacity <= 0.001f) continue;
        bool task = b < boneFiles.size() && boneFiles[b].size() == 1 && o.primary < 0 && o.secondary < 0;
        if (o.primary >= 0) mark(o.primary);
        if (o.secondary >= 0) mark(o.secondary);
        if (b < boneFiles.size())
            for (int f : boneFiles[b]) {
                // a forced primary replaces the default primary, a forced secondary the default emissive
                bool emissive = f >= 0 && size_t(f) < textureFiles.size() && textureFiles[size_t(f)].kind == 'e';
                if (task || (emissive ? o.secondary < 0 : o.primary < 0)) mark(f);
            }
    }
}

void Avatar::drawQuadTask(TaskDraw const& d, Mat4 const& partMat) {
    if (d.w == 0 || d.h == 0 || d.pw <= 0 || d.ph <= 0 || quadSlots.empty()) return;
    int const size = 256;
    // same content keeps its slot (no repaint); else the first free one
    QuadSlot* slot = nullptr;
    for (auto& q : quadSlots)
        if (!q.used && q.key == d.id) { slot = &q; break; }
    if (!slot)
        for (auto& q : quadSlots)
            if (!q.used) { slot = &q; break; }
    if (!slot || slot->bone < 0 || size_t(slot->bone) >= out.size() || boneClaimed[size_t(slot->bone)]) return;
    slot->used = true;
    double w = std::min(double(d.pw), double(size)), h = std::min(double(d.ph), double(size));
    if (slot->key != d.id) {
        quadPx.assign(size_t(size) * size, 0);
        taskRaster(*this, d, quadPx, size);
        if (slot->file >= 0 && slot->file < int(textureFiles.size()) && host)
            host->uploadTexture(textureFiles[size_t(slot->file)].rpTexture, size, size,
                                reinterpret_cast<uint8_t const*>(quadPx.data()));
        slot->key = d.id;
    }
    size_t b = size_t(slot->bone);
    boneClaimed[b] = 1;
    // quad geometry: Figura x in [-1, 0], y in [-1, 0] (text space x right / y down, flipped like Figura's tasks)
    Mat4 q;
    q.scale(d.w, d.h, 1);
    q.translate(-d.ox, -d.oy, 0);
    boneModel[b] = partMat * d.m * q;
    PartOut& o = out[b];
    o.visible = true;
    o.primary = o.secondary = -1;
    o.opacity = 1;
    bool flip = tune("quadFlipU", 0) != 0; // verified in game: reads left to right from the front
    o.uv[0] = flip ? w / size : 0;
    o.uv[1] = 0;
    o.uv[2] = (flip ? -w : w) / size;
    o.uv[3] = h / size;
}

void Avatar::frame(float delta) {
    if (!loaded) return;
    tickDelta = delta;
    auto it = vanillaVisible.find("PLAYER");
    (void)it;
    vanillaPose = computeVanillaPose(player, delta, !player.invisible);
    if (renderMode == "FIRST_PERSON") {
        // Figura's first-person arm render uses the arm ModelPart at rest (PlayerRenderer.renderHand resets it):
        // no vanilla offsets on arm parts; the Bedrock first-person animation moves the vanilla arm bone instead
        vanillaPose.rightArm = VanillaPart{{-5, 2, 0}, {}, {1, 1, 1}, true};
        vanillaPose.leftArm  = VanillaPart{{5, 2, 0}, {}, {1, 1, 1}, true};
    }
    applyAnimations();
    if (!scriptError) {
        lua_pushnumber(L, delta);
        callEvent("WORLD_RENDER", 1);
        if (player.loaded) {
            lua_pushnumber(L, delta);
            lua_pushstring(L, renderMode.c_str());
            pushMat4(L, Mat4{});
            callEvent("RENDER", 3);
        }
    }
    // Java PlayerRenderer.getRenderOffset: a crouching player is drawn 2px (scale 1/16) lower; Bedrock has no such
    // render offset, so it goes into the root matrix (otherwise the crouch pose floats above the ground)
    Mat4 rootMat;
    if (player.crouching && renderMode != "FIRST_PERSON") rootMat.translate(0, -2 / 0.9375 * tune("crouchDrop", 1), 0); // 2/16 block in world, model is drawn at 0.9375
    std::fill(boneClaimed.begin(), boneClaimed.end(), 0);
    itemOut[0].found = itemOut[1].found = false;
    for (auto& q : quadSlots) q.used = false;
    { // WORLD -> model: inverse of Java's player render transform T(pos) Ry(180 - bodyYaw) S(0.9375 / 16)
        double t  = delta;
        Vec3   P  = player.prevPos + (player.pos - player.prevPos) * t;
        double by = player.prevBodyYaw + (player.bodyYaw - player.prevBodyYaw) * t;
        Mat4   w;
        w.translate(-16 * P.x, -16 * P.y, -16 * P.z);
        w.rotateY(tune("worldYawSign", 1) * (by - tune("worldYawOff", 180)));
        double s = 1 / 0.9375;
        w.scale(s * tune("worldMirrorX", 1), s, s);
        worldToModel = w;
    }
    traverse(root, {}, {}, true, rootMat);
    solveBones();
    computeFileUse();
    if (!scriptError) {
        if (player.loaded) {
            lua_pushnumber(L, delta);
            lua_pushstring(L, renderMode.c_str());
            pushMat4(L, Mat4{});
            callEvent("POST_RENDER", 3);
        }
        lua_pushnumber(L, delta);
        callEvent("POST_WORLD_RENDER", 1);
    }
    if (L) texSync(*this);
    clearAnimations();
}

} // namespace bf::figura
