#include "client/PostFx.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {
BfPostFxV1          gFx{};
std::atomic<int64_t> gLastHud{0};
bool                gAny = false;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float num(std::string const& json, char const* key, float def) {
    std::regex re(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)");
    std::smatch m;
    if (std::regex_search(json, m, re)) return std::stof(m[1].str());
    return def;
}
} // namespace

extern "C" __declspec(dllexport) BfPostFxV1 const* BF_GetPostFx() {
    gFx.active = (gAny && nowMs() - gLastHud.load() < 250) ? 1u : 0u;
    return &gFx;
}

namespace bedrock_forge::client::postfx {

void load(std::filesystem::path const& packsDir, std::string const& activePack) {
    gFx         = {};
    gFx.size    = sizeof(BfPostFxV1);
    gFx.version = 1;
    gFx.fov     = 70.0f;
    gFx.bloomTiles = 6.0f;
    gAny        = false;
    if (activePack.empty() || activePack == "off") return;
    std::ifstream in(packsDir / activePack / "postfx.json");
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string j = ss.str();
    try {
        gFx.bloom      = num(j, "bloom", 0.0f);
        gFx.bloomTiles = num(j, "bloom_tiles", 6.0f);
        gFx.ca         = num(j, "ca", 0.0f);
        gFx.caStyle    = num(j, "ca_style", 0.0f);
        gFx.cas        = num(j, "cas", 0.0f);
        gFx.motionBlur = num(j, "motion_blur", 0.0f);
    } catch (...) {}
    gAny = gFx.bloom > 0 || gFx.ca > 0 || gFx.cas > 0 || gFx.motionBlur > 0;
}

void hudFrame(float pitch, float yaw) {
    gFx.pitch = pitch;
    gFx.yaw   = yaw;
    ++gFx.frame;
    gLastHud = nowMs();
}

void noteFov(float fov) {
    if (fov > 1.0f && fov < 179.0f) gFx.fov = fov;
}

} // namespace bedrock_forge::client::postfx
