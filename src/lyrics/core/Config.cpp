// LyricsFlow port - settings, themes and colour helpers. Values from LyricsFlow v2.0 (374m, MIT); see Config.h.
#include "lyrics/core/Config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "lyrics/core/Text.h"

namespace bf::lyrics {

namespace {
template <class T, size_t N>
struct EnumNames {
    std::array<std::string_view, N> names;
    std::string_view                 of(T v) const { auto i = size_t(v); return i < N ? names[i] : names[0]; }
    bool                             parse(std::string_view s, T& out) const {
        std::string l = text::toLower(s);
        for (size_t i = 0; i < N; ++i)
            if (text::toLower(names[i]) == l) { out = T(int(i)); return true; }
        return false;
    }
};
constexpr EnumNames<Source, 4>      kSource{{"SPOTIFY", "YOUTUBE", "ANY", "APPLE_MUSIC"}};
constexpr EnumNames<LyricsStyle, 4> kStyle{{"SKY", "FLOATING", "SCREEN", "WALL"}};
constexpr EnumNames<TextStyle, 2>   kTextStyle{{"NEON", "CLEAN"}};
constexpr EnumNames<EnterAnim, 8>   kEnter{{"WORD", "FADE", "RISE", "NEON", "DROP", "SPIN", "ZOOM", "GLITCH"}};
constexpr EnumNames<ExitAnim, 9>    kExit{{"FLOAT_UP", "DRIFT", "FADE", "FALL", "SHATTER", "DISSOLVE", "EXPLODE", "BURN", "GLITCH"}};
constexpr EnumNames<TopBarStyle, 2> kTopBar{{"KARAOKE", "SIMPLE"}};
constexpr EnumNames<LetterLand, 2>  kLand{{"FLAT", "TUMBLE"}};

auto const& names(Source*) { return kSource; }
auto const& names(LyricsStyle*) { return kStyle; }
auto const& names(TextStyle*) { return kTextStyle; }
auto const& names(EnterAnim*) { return kEnter; }
auto const& names(ExitAnim*) { return kExit; }
auto const& names(TopBarStyle*) { return kTopBar; }
auto const& names(LetterLand*) { return kLand; }

bool parseColor(json::Value const& v, int32_t& out) {
    if (v.isNumber()) { out = int32_t(uint32_t(int64_t(v.asNumber()))); return true; }
    if (!v.isString()) return false;
    std::string s = text::toLower(text::trim(v.asString()));
    if (s == "auto" || s.empty()) { out = 0; return true; }
    if (s[0] == '#') s = s.substr(1);
    if (s.size() != 6 && s.size() != 8) return false;
    uint32_t x = 0;
    for (char c : s) {
        x <<= 4;
        if (c >= '0' && c <= '9') x |= uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') x |= uint32_t(c - 'a' + 10);
        else return false;
    }
    if (s.size() == 6) x |= 0xFF000000u;
    out = int32_t(x);
    return true;
}
std::string colorStr(int32_t c) {
    if (c == 0) return "\"auto\"";
    char b[16];
    std::snprintf(b, sizeof b, "\"#%08X\"", uint32_t(c));
    return b;
}
std::string quote(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (uint8_t(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", unsigned(uint8_t(c))); o += b; }
        else o += c;
    }
    return o + "\"";
}
std::string num(float f) {
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", double(f));
    return b;
}

constexpr Theme kThemes[] = {
    {"neon_pink", 0xFFFFE3F6, 0xFFFF5AC8, 0xFF6FD8FF, 0xFF2A1A44, 0xFFD98BB5},
    {"ice_blue", 0xFFE8FBFF, 0xFF3FB8FF, 0xFFC8F0FF, 0xFF0F2440, 0xFF7FB8E0},
    {"sunset", 0xFFFFF0E0, 0xFFFF5A8A, 0xFFFFB347, 0xFF3A1838, 0xFFFF9A6A},
    {"matrix", 0xFFE6FFE9, 0xFF1D9E75, 0xFF5AFF8A, 0xFF04140A, 0xFF1D6E45},
    {"lavender", 0xFFF7F0FF, 0xFF9B5CFF, 0xFFFFFFFF, 0xFF2A2040, 0xFFB9A2E8},
    {"ocean", 0xFFE6FFFB, 0xFF2EE6D6, 0xFF3B4CCA, 0xFF0C2A35, 0xFF3C8EA8},
    {"fire", 0xFFFFF4D6, 0xFFFF3C3C, 0xFFFFB02E, 0xFF3A1010, 0xFFE0602A},
    {"sakura", 0xFFFFF0F5, 0xFFFF8FB8, 0xFFFFD1E1, 0xFF3D2030, 0xFFF2A9C4},
    {"galaxy", 0xFFF0E8FF, 0xFF7C3AED, 0xFF22D3EE, 0xFF0B0820, 0xFF4B2A8A},
    {"gold", 0xFFFFFBE6, 0xFFFFB800, 0xFFFFF3A0, 0xFF2E2408, 0xFFD9A441},
    {"vaporwave", 0xFFFFFFFF, 0xFFFF71CE, 0xFF01CDFE, 0xFF2A1A40, 0xFFB967FF},
    {"toxic", 0xFFF4FFE0, 0xFFA3FF12, 0xFFB026FF, 0xFF1A2408, 0xFF5E8A1E},
    {"blood_moon", 0xFFFFE6E6, 0xFFB3001B, 0xFFFF6B6B, 0xFF24070B, 0xFF8A1E2A},
    {"aurora", 0xFFEFFFF8, 0xFF00E5A0, 0xFF8B5CF6, 0xFF071A24, 0xFF2A8A7A},
    {"candy", 0xFFFFFFFF, 0xFFFF9AD5, 0xFF9AE8FF, 0xFF3A2A40, 0xFFE7A8D8},
    {"midnight", 0xFFE6ECFF, 0xFF3B4CCA, 0xFFA5B4FC, 0xFF0B1026, 0xFF3A4A8A},
    {"minecraft", 0xFFFFFFFF, 0xFF3F3F3F, 0xFF3F3F3F, 0xFF78A7FF, 0xFFC6DBFF},
    {"minecraft_gold", 0xFFFFFF55, 0xFF3F3F15, 0xFFAA7700, 0xFF241A3A, 0xFF8A6AC8},
    {"enchant", 0xFFE0C8FF, 0xFFAA00AA, 0xFF5555FF, 0xFF120A24, 0xFF5A3A9A},
    {"nether", 0xFFFFD0A0, 0xFFAA0000, 0xFFFF5500, 0xFF2A0808, 0xFF7A2410},
    {"black", 0xFF0B0B0F, 0xFF4A4A55, 0xFF9A9AAA, 0xFF050506, 0xFF3A3A42},
    {"black_white", 0xFFFFFFFF, 0xFF0B0B0F, 0xFF2A2A30, 0xFF050506, 0xFF2A2A30},
};
} // namespace

std::string_view enumName(Source v) { return kSource.of(v); }
std::string_view enumName(LyricsStyle v) { return kStyle.of(v); }
std::string_view enumName(TextStyle v) { return kTextStyle.of(v); }
std::string_view enumName(EnterAnim v) { return kEnter.of(v); }
std::string_view enumName(ExitAnim v) { return kExit.of(v); }
std::string_view enumName(TopBarStyle v) { return kTopBar.of(v); }
std::string_view enumName(LetterLand v) { return kLand.of(v); }

int applyJson(Config& cfg, json::Value const& j) {
    int n = 0;
#define BF_LC_B(nm, d) if (j.has(#nm) && j[#nm].isBool()) { cfg.nm = j[#nm].asBool(); ++n; }
#define BF_LC_I(nm, d) if (j.has(#nm) && j[#nm].isNumber()) { cfg.nm = int(j[#nm].asInt()); ++n; }
#define BF_LC_F(nm, d) if (j.has(#nm) && j[#nm].isNumber()) { cfg.nm = float(j[#nm].asNumber()); ++n; }
#define BF_LC_S(nm, d) if (j.has(#nm) && j[#nm].isString()) { cfg.nm = j[#nm].asString(); ++n; }
#define BF_LC_C(nm, d) if (j.has(#nm) && parseColor(j[#nm], cfg.nm)) ++n;
#define BF_LC_E(T, nm, d) if (j.has(#nm) && j[#nm].isString() && names(static_cast<T*>(nullptr)).parse(j[#nm].asString(), cfg.nm)) ++n;
    BF_LYRICS_CONFIG_FIELDS(BF_LC_B, BF_LC_I, BF_LC_F, BF_LC_S, BF_LC_C, BF_LC_E)
#undef BF_LC_B
#undef BF_LC_I
#undef BF_LC_F
#undef BF_LC_S
#undef BF_LC_C
#undef BF_LC_E
    if (j["songOffsets"].isObject()) {
        for (auto const& [k, v] : j["songOffsets"].members())
            if (v.isNumber()) cfg.songOffsets[k] = int(v.asInt());
        ++n;
    }
    return n;
}

std::string toJson(Config const& cfg) {
    std::string o = "{\n";
#define BF_LC_B(nm, d) o += "  \"" #nm "\": " + std::string(cfg.nm ? "true" : "false") + ",\n";
#define BF_LC_I(nm, d) o += "  \"" #nm "\": " + std::to_string(cfg.nm) + ",\n";
#define BF_LC_F(nm, d) o += "  \"" #nm "\": " + num(cfg.nm) + ",\n";
#define BF_LC_S(nm, d) o += "  \"" #nm "\": " + quote(cfg.nm) + ",\n";
#define BF_LC_C(nm, d) o += "  \"" #nm "\": " + colorStr(cfg.nm) + ",\n";
#define BF_LC_E(T, nm, d) o += "  \"" #nm "\": " + quote(enumName(cfg.nm)) + ",\n";
    BF_LYRICS_CONFIG_FIELDS(BF_LC_B, BF_LC_I, BF_LC_F, BF_LC_S, BF_LC_C, BF_LC_E)
#undef BF_LC_B
#undef BF_LC_I
#undef BF_LC_F
#undef BF_LC_S
#undef BF_LC_C
#undef BF_LC_E
    o += "  \"songOffsets\": {";
    bool first = true;
    for (auto const& [k, v] : cfg.songOffsets) {
        o += (first ? "\n    " : ",\n    ") + quote(k) + ": " + std::to_string(v);
        first = false;
    }
    o += first ? "}\n}\n" : "\n  }\n}\n";
    return o;
}

std::vector<Theme> allThemes() { return std::vector<Theme>(std::begin(kThemes), std::end(kThemes)); }

Theme const* findTheme(std::string_view id) {
    for (auto const& t : kThemes)
        if (t.id == id) return &t;
    return nullptr;
}
Theme currentTheme(Config const& c) {
    Theme const* b    = findTheme(c.theme);
    Theme        base = b ? *b : kThemes[0];
    if (c.customTheme) {
        auto a = uint32_t(c.customA), bb = uint32_t(c.customB);
        return {"custom", uint32_t(c.customMain), a, bb, colDarken(a, 0.82f), colLerp(a, bb, 0.5f)};
    }
    if (c.singleColor != 0) base.main = uint32_t(c.singleColor);
    return base;
}

uint32_t colLerp(uint32_t c1, uint32_t c2, float t) {
    t          = std::clamp(t, 0.0f, 1.0f);
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int a = int((c1 >> sh) & 0xFF), b = int((c2 >> sh) & 0xFF);
        int v = std::clamp(int(float(a) + float(b - a) * t), 0, 255);
        r |= uint32_t(v) << sh;
    }
    return r;
}
uint32_t colAlpha(uint32_t c, float mul) {
    if (mul >= 1.0f) return c;
    if (mul <= 0.0f) return c & 0xFFFFFFu;
    return (c & 0xFFFFFFu) | (uint32_t(int(float(c >> 24) * mul)) << 24);
}
uint32_t colDarken(uint32_t c, float x) { return colLerp(c, c & 0xFF000000u, x); }
uint32_t colBrighten(uint32_t c, float x) { return colLerp(c, (c & 0xFF000000u) | 0xFFFFFFu, x); }

} // namespace bf::lyrics
