// FiguraBridge headless simulator: runs a compiled avatar with a fake host and scripted player states, and dumps the
// per-part Bedrock bone values for chosen frames (JSON) so tools/FiguraBridge/render_sim.js can draw them.
//   Sim <compiled_dir> <scenario> <out.json> [frameEvery]
// Scenarios: idle, walk, run, crouch, jump, sit, swing, all
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <cstdlib>

#include "figura/core/Avatar.h"
#include "figura/core/LuaBind.h"

using namespace bf::figura;

struct FakeHost : IHost {
    int64_t                ms = 1'000'000;
    int                    errors = 0;
    std::set<std::string>  down;
    int64_t millis() override { return ms; }
    void    log(std::string const& m) override { std::printf("[lua] %s\n", m.c_str()); }
    void    error(std::string const& m) override { errors++; std::printf("[ERROR] %s\n", m.c_str()); }
    void    playSound(std::string const& id, bool custom, Vec3, float v, float p, bool, int) override {
        std::printf("[sound] %s%s v=%.2f p=%.2f\n", custom ? "(custom) " : "", id.c_str(), v, p);
    }
    bool        keyDown(std::string const& k) override { return down.count(k) > 0; }
    std::string vanillaKeyBinding(std::string const& id) override {
        if (id == "key.forward") return "key.keyboard.w";
        if (id == "key.back") return "key.keyboard.s";
        if (id == "key.left") return "key.keyboard.a";
        if (id == "key.right") return "key.keyboard.d";
        if (id == "key.jump") return "key.keyboard.space";
        if (id == "key.attack") return "key.mouse.left";
        if (id == "key.use") return "key.mouse.right";
        if (id == "key.sneak") return "key.keyboard.left.shift";
        return "key.keyboard.unknown";
    }
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: Sim <compiled_dir> <scenario> <out.json> [frameEvery]\n");
        return 2;
    }
    std::string dir = argv[1], scen = argv[2];
    int every = argc > 4 ? std::atoi(argv[4]) : 6;
    FakeHost host;
    Avatar   a(&host);
    std::string err;
    if (!a.load(std::filesystem::path(dir) / "avatar", std::filesystem::path(dir) / "rig.json", &err)) {
        std::printf("load failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("loaded '%s' by %s: %zu parts, %zu animations, %zu textures, scriptError=%d\n", a.name.c_str(),
                a.author.c_str(), a.parts.size(), a.animations.size(), a.textureFiles.size(), int(a.scriptError));
    if (char const* pre = std::getenv("PRELUA")) { // extra test script run after the avatar's own scripts
        if (luaL_loadfile(a.L, pre) != 0 || lua_pcall(a.L, 0, 0, 0) != 0) {
            std::printf("PRELUA error: %s\n", lua_tostring(a.L, -1));
            lua_pop(a.L, 1);
        }
    }
    PlayerState& p = a.player;
    p.loaded = true;
    p.name   = "Novak";
    p.uuid   = "00000000-0000-0000-0000-000000000000";
    std::ofstream out(argv[3]);
    out << "{\"frames\":[";
    bool first = true;
    int  frameNo = 0;
    const int fps = 60;
    double walkPos = 0;
    auto run = [&](char const* label, double seconds, auto&& setup) {
        int frames = int(seconds * fps);
        for (int f = 0; f < frames; f++) {
            double t = f / double(fps);
            host.ms += 1000 / fps;
            setup(t);
            // a game tick every 3 frames (20 Hz)
            if (frameNo % 3 == 0) {
                p.prevPos   = p.pos;
                p.pos       = p.pos + p.velocity;
                p.prevWalkSpeed = p.walkSpeed;
                double sp   = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z) * 4.0;
                p.walkSpeed = float(p.walkSpeed + (std::min(1.0, sp) - p.walkSpeed) * 0.4);
                walkPos += p.walkSpeed;
                p.walkPos = float(walkPos);
                p.age++;
                if (p.swinging) p.swingTime++;
                a.tick();
            }
            float delta = float((frameNo % 3) / 3.0);
            a.frame(delta);
            if (frameNo % every == 0) {
                out << (first ? "" : ",") << "{\"label\":\"" << label << " t=" << t << "\",\"bones\":{";
                first = false;
                bool fb = true;
                for (size_t i = 0; i < a.out.size(); i++) {
                    auto const& o = a.out[i];
                    out << (fb ? "" : ",") << "\"" << i << "\":[" << o.bone.rot.x << "," << o.bone.rot.y << "," << o.bone.rot.z << ","
                        << o.bone.pos.x << "," << o.bone.pos.y << "," << o.bone.pos.z << "," << o.bone.scale.x << ","
                        << o.bone.scale.y << "," << o.bone.scale.z << "," << (o.visible ? 1 : 0) << "," << o.primary << ","
                        << o.secondary << "]";
                    fb = false;
                }
                out << "}}";
            }
            frameNo++;
        }
    };
    auto stand = [&](double) { p.velocity = {}; p.onGround = true; p.crouching = false; p.pose = "STANDING"; };
    bool all = scen == "all";
    if (all || scen == "idle") run("idle", 4, stand);
    if (all || scen == "walk") {
        host.down.insert("key.keyboard.w");
        run("walk", 3, [&](double) { p.velocity = {0, 0, 0.1}; p.onGround = true; });
        host.down.erase("key.keyboard.w");
    }
    if (all || scen == "run") {
        host.down.insert("key.keyboard.w");
        run("run", 2, [&](double) { p.velocity = {0, 0, 0.28}; p.sprinting = true; p.onGround = true; });
        p.sprinting = false;
        host.down.erase("key.keyboard.w");
    }
    if (all || scen == "crouch") run("crouch", 2, [&](double) { p.velocity = {}; p.crouching = true; p.pose = "CROUCHING"; });
    p.crouching = false; p.pose = "STANDING";
    if (all || scen == "jump") {
        run("jump", 1.5, [&](double t) {
            p.onGround = t > 1.0;
            p.velocity = {0, t < 0.4 ? 0.4 - t : (t < 1.0 ? -0.5 : 0), 0};
            if (t < 0.1) host.down.insert("key.keyboard.space"); else host.down.erase("key.keyboard.space");
        });
    }
    if (all || scen == "swing") {
        run("swing", 1, [&](double t) {
            p.velocity = {};
            p.swinging = t < 0.3;
            p.attackAnim = t < 0.3 ? float(t / 0.3) : 0;
            if (t < 0.05) host.down.insert("key.mouse.left"); else host.down.erase("key.mouse.left");
        });
    }
    if (scen == "coverage") {
        std::ifstream cf(std::getenv("COVERAGE") ? std::getenv("COVERAGE") : "/tmp/coverage.lua");
        std::stringstream ss; ss << cf.rdbuf();
        std::string src = ss.str();
        run("tick", 0.1, stand);
        if (luaL_loadbuffer(a.L, src.data(), src.size(), "=coverage") != 0 || lua_pcall(a.L, 0, 0, 0) != 0) std::printf("coverage error: %s\n", lua_tostring(a.L, -1));
    }
    if (scen == "wheel") {
        auto exec = [&](int i) {
            lua_getglobal(a.L, "action_wheel");
            lua_getfield(a.L, -1, "execute");
            lua_pushvalue(a.L, -2);
            lua_pushinteger(a.L, i);
            lua_pushboolean(a.L, 0);
            if (lua_pcall(a.L, 3, 0, 0) != 0) { std::printf("[ERROR] %s\n", lua_tostring(a.L, -1)); lua_pop(a.L, 1); host.errors++; }
            lua_pop(a.L, 1);
        };
        run("before", 0.5, stand);
        exec(6); run("page", 0.2, stand);
        exec(3); run("blue", 0.5, stand);
        std::map<int, int> sec;
        for (auto& o : a.out) sec[o.secondary]++;
        for (auto& [k, v] : sec) std::printf("  secondary file %d: %d parts (%s)\n", k, v, k >= 0 ? a.textureFiles[k].figuraName.c_str() : "default");
    }
    if (all || scen == "sit") {
        p.vehicle.valid = true; p.vehicle.type = "minecraft:boat";
        run("sit", 2, [&](double) { p.velocity = {}; });
        p.vehicle.valid = false;
    }
    out << "]}";
    std::printf("frames=%d errors=%d scriptError=%d %s\n", frameNo, host.errors, int(a.scriptError), a.lastError.c_str());
    // which animations are playing now
    for (auto& an : a.animations)
        if (an && an->playState != PlayState::Stopped)
            std::printf("  anim %s %s t=%.2f blend=%.2f\n", an->name.c_str(), playStateName(an->playState), an->time, an->blend);
    return host.errors ? 1 : 0;
}
