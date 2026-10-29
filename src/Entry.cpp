// Renderer Foundation - LeviLamina entry point (replaces BetterRenderDragon's dllmain.cpp; see Notice.h).
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include "Graphics.h"
#include "MaterialLoader.h"
#include "Notice.h"
#include "Options.h"
#include "Overlay.h"
#include "ScreenWatch.h"
#include "SpatialPanel.h"
#include "UiCapture.h"

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
        RfOptions::init(mSelf.getConfigDir());
        RfOptions::load();
        materials::install(mSelf.getModDir()); // before the renderer opens its materials
        uicapture::init(mSelf.getConfigDir(), mSelf.getModDir() / "logs" / "uicapture");
        rf::panel::installEarly(); // v0.8: catch the game's own GameInput before it creates its mouse readings
        overlay::install();
        return true;
    }
    bool enable() {
        overlay::install(); // retry if the load-time probe failed
        graphics::install();
        screens::install(); // Spatial GUI: panel only for inventory/chest/crafting screens
        return true;
    }
    bool disable() {
        graphics::uninstall();
        screens::uninstall();
        overlay::uninstall();
        materials::uninstall();
        RfOptions::save();
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace rf

LL_REGISTER_MOD(rf::Mod, rf::Mod::getInstance());
