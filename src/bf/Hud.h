#pragma once
// Bedrock Forge SDK - UIAPI (v0, version-specific adapter for MC 1.26.51)
// NOTE: this header still exposes engine types (ScreenView, MinecraftUIRenderContext).
// Per ARCHITECTURE.md these will move behind an adapter once the API stabilises.
// Drawing approach follows Lamium's AfterUIRenderEvent + drawText pattern (re-implemented, not copied).

#include <string>
#include <string_view>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/FontRepository.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"

namespace bf::hud {

struct Rgba {
    float r, g, b, a;
};

/// True when this ScreenView is the in-game HUD (not a menu, inventory or the title screen).
inline bool isHudScreen(ScreenView const& view) {
    auto const& tree = view.mVisualTree;
    return tree && std::string_view(*tree->mRootControlName).ends_with(".hud_screen");
}

/// Translucent box. Coordinates are in UI units (same space the game lays out the HUD in).
inline void fillBox(MinecraftUIRenderContext& ctx, float x, float y, float w, float h, Rgba c) {
    ctx.fillRectangle(RectangleArea{x, x + w, y, y + h}, mce::Color{c.r, c.g, c.b, 1.0f}, c.a);
}

/// One line of text with a 1-unit drop shadow, left-aligned, in the "default" UI font.
inline void drawLabel(MinecraftUIRenderContext& ctx, float x, float y, std::string const& text, Rgba c, float scale = 1.0f) {
    auto const& handle = ctx.mClient.getMinecraftGame_DEPRECATED().getFontRepository()->getFontFromFontType("default");
    TextMeasureData const  textData{scale, 0.0f, false, false, false, ::ui::TextAlignment::Left};
    CaretMeasureData const caret{-1, false};
    float const            lineH = 10.0f * scale;
    auto draw = [&](float off, mce::Color const& col) {
        ctx.drawText(handle.getFont(), RectangleArea{x + off, x + off + 400.0f, y + off, y + off + lineH},
                     std::string(text), col, c.a, ::ui::TextAlignment::Left, textData, caret);
    };
    draw(1.0f, mce::Color{0.15f, 0.15f, 0.15f, 1.0f});
    draw(0.0f, mce::Color{c.r, c.g, c.b, 1.0f});
    ctx.flushText(0.0f, std::nullopt);
}

} // namespace bf::hud
