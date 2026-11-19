// [BF] FPS cost badge: what a module really costs, measured on this PC. Many effects share one GPU pass (World FX), so
// their cost can't be read off a timer - it is measured the honest way: frame time with the module on, then with it
// off for a moment, then it goes back on. Runs from the HUD frame (Renderer Foundation only picks a change up while the
// HUD runs), about 8 s. Original code for Bedrock Forge.
#include "client/CostProbe.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

#include "client/Module.h"

namespace bedrock_forge::client::cost {
namespace {
using Clock = std::chrono::steady_clock;
struct Run {
    std::string id;
    int         phase = 0;   // 0 idle, 1 settle on, 2 sample on, 3 settle off, 4 sample off
    bool        wasOn = false;
    double      t0 = 0, sumOn = 0, sumOff = 0;
    int         nOn = 0, nOff = 0;
} gRun;
std::map<std::string, Result> gRes;
std::mutex                    gM;
double                        gLast = 0;
double nowS() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
constexpr double kSettleOn = 1.0, kSample = 2.5, kSettleOff = 1.6;   // RF reads its config about once a second
} // namespace

bool start(std::string const& id) {
    std::lock_guard l(gM);
    if (gRun.phase != 0) return false;
    auto* m = Registry::get().find(id);
    if (!m) return false;
    gRun = Run{};
    gRun.id = id, gRun.wasOn = m->enabled, gRun.phase = 1, gRun.t0 = nowS();
    if (!m->enabled) Registry::get().setEnabled(*m, true);
    return true;
}
bool running(std::string* id) {
    std::lock_guard l(gM);
    if (id) *id = gRun.id;
    return gRun.phase != 0;
}
bool result(std::string const& id, Result& out) {
    std::lock_guard l(gM);
    auto it = gRes.find(id);
    if (it == gRes.end()) return false;
    out = it->second;
    return true;
}
void tick() {
    double const now = nowS(), dt = gLast > 0 ? now - gLast : 0;
    gLast = now;
    std::lock_guard l(gM);
    if (gRun.phase == 0 || dt <= 0 || dt > 0.5) return;   // a hitch (loading, alt-tab) would spoil the sample
    auto* m = Registry::get().find(gRun.id);
    if (!m) { gRun.phase = 0; return; }
    double const age = now - gRun.t0;
    switch (gRun.phase) {
    case 1: if (age > kSettleOn) gRun.phase = 2, gRun.t0 = now; break;
    case 2:
        gRun.sumOn += dt, ++gRun.nOn;
        if (age > kSample) { gRun.phase = 3, gRun.t0 = now; Registry::get().setEnabled(*m, false); }
        break;
    case 3: if (age > kSettleOff) gRun.phase = 4, gRun.t0 = now; break;
    case 4:
        gRun.sumOff += dt, ++gRun.nOff;
        if (age > kSample) {
            Result r;
            r.msOn  = gRun.nOn ? float(1000.0 * gRun.sumOn / gRun.nOn) : 0;
            r.msOff = gRun.nOff ? float(1000.0 * gRun.sumOff / gRun.nOff) : 0;
            r.fpsOn = r.msOn > 0 ? 1000.0f / r.msOn : 0, r.fpsOff = r.msOff > 0 ? 1000.0f / r.msOff : 0;
            gRes[gRun.id] = r;
            Registry::get().setEnabled(*m, gRun.wasOn);
            gRun.phase = 0;
        }
        break;
    default: gRun.phase = 0;
    }
}
} // namespace bedrock_forge::client::cost
