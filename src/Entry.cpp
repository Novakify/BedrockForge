// Renderer Foundation - LeviLamina entry point (replaces BetterRenderDragon's dllmain.cpp; see Notice.h).
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include "Graphics.h"
#include "MaterialLoader.h"
#include "Notice.h"
#include "Options.h"
#include "Overlay.h"

namespace rf {

class Mod {
public:
    static Mod& getInstance() {
        static Mod m;
        return m;
    }
    Mod() : mSelf(*ll::mod::NativeMod::current()) {}
    ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load() {
        auto& log = mSelf.getLogger();
        log.info("{} {} (fork of {}) for MC {} - AGPL-3.0, source: see README", RF_NAME, RF_VERSION, RF_BASED_ON, RF_TARGET_VERSION);
        Options::init(mSelf.getConfigDir());
        Options::load();
        materials::install(mSelf.getModDir()); // before the renderer opens its materials
        overlay::install();
        return true;
    }
    bool enable() {
        graphics::install();
        return true;
    }
    bool disable() {
        graphics::uninstall();
        overlay::uninstall();
        materials::uninstall();
        Options::save();
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace rf

LL_REGISTER_MOD(rf::Mod, rf::Mod::getInstance());
