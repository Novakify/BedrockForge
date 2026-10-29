// LyricsFlow port - text helpers + TitleCleaner. Logic ported from LyricsFlow v2.0 (374m, MIT); see Text.h.
#include "lyrics/core/Text.h"

#include <regex>
#include <set>

namespace bf::lyrics::text {

namespace {
// Base letter (case kept) for U+00C0..U+00FF after NFD + mark removal.
constexpr std::u32string_view kLatin1 = U"AAAAAA\u00C6CEEEEIIII\u00D0NOOOOO\u00D7\u00D8UUUUY\u00DE\u00DFaaaaaa\u00E6ceeeeiiii\u00F0nooooo\u00F7\u00F8uuuuy\u00FEy";
// Same for U+0100..U+017F (Latin Extended-A). Letters without a canonical decomposition map to themselves.
constexpr std::u32string_view kLatinA =
    U"AaAaAaCcCcCcCcDd\u0110\u0111EeEeEeEeEeGgGgGgGgHh\u0126\u0127IiIiIiIiI\u0131\u0132\u0133JjKk\u0138LlLlLl\u013F\u0140\u0141\u0142"
    U"NnNnNn\u0149\u014A\u014BOoOoOo\u0152\u0153RrRrRrSsSsSsSsTtTt\u0166\u0167UuUuUuUuUuUuWwYyYZzZzZz\u017F";
static_assert(kLatin1.size() == 64, "Latin-1 table");
static_assert(kLatinA.size() == 128, "Latin Extended-A table");

bool isMark(uint32_t cp) { return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x20D0 && cp <= 0x20FF); }

uint32_t baseOf(uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F) return kLatinA[cp - 0x100];
    switch (cp) { // Greek tonos/dialytika and Cyrillic letters with a canonical decomposition
    case 0x386: return 0x391; case 0x388: return 0x395; case 0x389: return 0x397; case 0x38A: return 0x399;
    case 0x38C: return 0x39F; case 0x38E: return 0x3A5; case 0x38F: return 0x3A9; case 0x3AA: return 0x399; case 0x3AB: return 0x3A5;
    case 0x3AC: return 0x3B1; case 0x3AD: return 0x3B5; case 0x3AE: return 0x3B7; case 0x3AF: return 0x3B9; case 0x390: return 0x3B9;
    case 0x3B0: return 0x3C5; case 0x3CA: return 0x3B9; case 0x3CB: return 0x3C5; case 0x3CC: return 0x3BF; case 0x3CD: return 0x3C5;
    case 0x3CE: return 0x3C9;
    case 0x419: return 0x418; case 0x439: return 0x438; case 0x401: return 0x415; case 0x451: return 0x435;
    case 0x407: return 0x406; case 0x457: return 0x456; case 0x40E: return 0x423; case 0x45E: return 0x443;
    default: return cp;
    }
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::regex const& reNoise() {
    static std::regex const r(
        R"(\b(official\s*(music\s*)?(video|audio|lyric(s)?\s*video|visuali[sz]er)|lyrics?\s*video|lyrics?|audio|video\s*clip|clip\s*officiel|hd|hq|4k|remaster(ed)?(\s*\d{4})?|sped\s*up|slowed(\s*\+?\s*reverb)?|nightcore|8d\s*audio|extended|radio\s*edit|bass\s*boosted)\b)",
        std::regex::ECMAScript | std::regex::icase);
    return r;
}
std::regex const& reFeat() {
    static std::regex const r(R"(\s*[\(\[]?\s*\b(feat\.?|ft\.?|featuring|with)\s+[^\)\]\-]*[\)\]]?)", std::regex::ECMAScript | std::regex::icase);
    return r;
}
std::regex const& reArtistSplit() {
    static std::regex const r(R"(\s*(,|&|;|\bx\b|\bfeat\.?|\bft\.?|\bwith\b|/)\s*)", std::regex::ECMAScript | std::regex::icase);
    return r;
}
std::regex const& reSpeed() {
    static std::regex const r(R"(\b(sped\s*up|speed\s*up|slowed(\s*(down|\+|and|&)?\s*reverb)?|nightcore|daycore)\b)",
                              std::regex::ECMAScript | std::regex::icase);
    return r;
}
std::regex const& reRemix() {
    static std::regex const r(R"(\b(remix|rmx|bootleg|mashup|flip|vip mix)\b)", std::regex::ECMAScript | std::regex::icase);
    return r;
}

bool isOpener(uint32_t c) { return c == '(' || c == '[' || c == '{' || c == 0x3010; }
bool isCloser(uint32_t c) { return c == ')' || c == ']' || c == '}' || c == 0x3011; }

// BRACKETS.matcher(s).replaceAll(" "):  \s*[\(\[\{【][^\)\]\}】]*[\)\]\}】]
std::string removeBrackets(std::string_view s) {
    auto        cps = codepoints(s);
    std::string out;
    size_t      i = 0;
    while (i < cps.size()) {
        // leftmost match: a run of whitespace (possibly empty) then an opener with a closer later
        size_t j = i;
        while (j < cps.size() && cps[j] < 0x80 && isWs(char(cps[j]))) ++j;
        if (j < cps.size() && isOpener(cps[j])) {
            size_t k = j + 1;
            while (k < cps.size() && !isCloser(cps[k])) ++k;
            if (k < cps.size()) {
                out += ' ';
                i = k + 1;
                continue;
            }
        }
        append(out, cps[i]);
        ++i;
    }
    return out;
}

std::string stripMarksKeepCase(std::string_view s) {
    std::string out;
    size_t      i = 0;
    while (i < s.size()) {
        uint32_t c = next(s, i);
        if (c == 0x131) c = 'i';
        else if (c == 0x130) c = 'I';
        if (isMark(c)) continue;
        append(out, baseOf(c));
    }
    return out;
}
} // namespace

// ------------------------------------------------------------------ UTF-8
uint32_t next(std::string_view s, size_t& i) {
    auto     b = uint8_t(s[i]);
    uint32_t cp;
    int      n;
    if (b < 0x80) { ++i; return b; }
    if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; n = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; n = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; n = 3; }
    else { ++i; return 0xFFFD; }
    if (i + size_t(n) >= s.size()) { ++i; return 0xFFFD; } // truncated sequence
    for (int k = 1; k <= n; ++k) {
        auto c = uint8_t(s[i + k]);
        if ((c & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (c & 0x3F);
    }
    i += size_t(n) + 1;
    return cp;
}
void append(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}
std::vector<uint32_t> codepoints(std::string_view s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) out.push_back(next(s, i));
    return out;
}
size_t length(std::string_view s) {
    size_t n = 0, i = 0;
    while (i < s.size()) { next(s, i); ++n; }
    return n;
}

// ------------------------------------------------------------------ classes
uint32_t lower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0x80) return c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    if (c == 0x178) return 0xFF;
    if (c == 0x130) return 'i';
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c % 2 == 0) ? c + 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;
    return c;
}
uint32_t upper(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0x80) return c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
    if (c == 0xFF) return 0x178;
    if (c == 0x131) return 'I';
    if ((c >= 0x101 && c <= 0x137) || (c >= 0x14B && c <= 0x177)) return (c % 2 == 1) ? c - 1 : c;
    if ((c >= 0x13A && c <= 0x148) || (c >= 0x17A && c <= 0x17E)) return (c % 2 == 0) ? c - 1 : c;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 0x20;
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;
    if (c >= 0x450 && c <= 0x45F) return c - 0x50;
    return c;
}
bool isLetter(uint32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
    if (c == 0xD7 || c == 0xF7) return false;
    if (isMark(c)) return false;
    if (c >= 0x2000 && c <= 0x2BFF) return false; // punctuation, symbols, arrows, math, misc symbols (incl. music notes)
    if (c >= 0x3000 && c <= 0x303F) return false; // CJK punctuation
    if (c >= 0xFE30 && c <= 0xFE4F) return false;
    if ((c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65)) return false;
    if (c >= 0xE000 && c <= 0xF8FF) return false;   // private use
    if (c >= 0x1F000 && c <= 0x1FAFF) return false; // emoji / symbols
    if (c >= 0x0660 && c <= 0x0669) return false;   // Arabic-Indic digits (digits, not letters)
    if (c == 0xFFFD || c == 0xFEFF) return false;
    return true;
}
bool isLetterOrDigit(uint32_t c) {
    if (c >= '0' && c <= '9') return true;
    if (c >= 0x0660 && c <= 0x0669) return true;
    if (c >= 0xFF10 && c <= 0xFF19) return true;
    return isLetter(c);
}
bool isLatin(uint32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c == 0xAA || c == 0xBA) return true;
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
    if (c >= 0x1E00 && c <= 0x1EFF) return true;
    if (c >= 0xFF21 && c <= 0xFF3A) return true;
    if (c >= 0xFF41 && c <= 0xFF5A) return true;
    return false;
}
bool isCjkOrHangul(uint32_t c) { return (c >= 0x3000 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF); }
bool isHan(uint32_t c) { return c >= 0x4E00 && c <= 0x9FFF; }
bool isRtl(uint32_t c) { return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }

// ------------------------------------------------------------------ strings
std::string toLower(std::string_view s) {
    std::string out;
    size_t      i = 0;
    while (i < s.size()) append(out, lower(next(s, i)));
    return out;
}
std::string toUpper(std::string_view s) {
    std::string out;
    size_t      i = 0;
    while (i < s.size()) {
        uint32_t c = next(s, i);
        if (c == 0xDF) { out += "SS"; continue; } // Java: "ß".toUpperCase() == "SS"
        append(out, upper(c));
    }
    return out;
}
std::string_view trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && uint8_t(s[a]) <= ' ') ++a;
    while (b > a && uint8_t(s[b - 1]) <= ' ') --b;
    return s.substr(a, b - a);
}
std::string trimmed(std::string_view s) { return std::string(trim(s)); }
std::vector<std::string> splitWs(std::string_view s) {
    s = trim(s);
    std::vector<std::string> out;
    size_t                   i = 0;
    while (i < s.size()) {
        size_t j = i;
        while (j < s.size() && !isWs(s[j])) ++j;
        out.emplace_back(s.substr(i, j - i));
        while (j < s.size() && isWs(s[j])) ++j;
        i = j;
    }
    if (out.empty()) out.emplace_back();
    return out;
}
std::vector<std::string> splitLines(std::string_view s) {
    std::vector<std::string> out;
    size_t                   i = 0;
    for (;;) {
        size_t j = s.find('\n', i);
        if (j == std::string_view::npos) { out.emplace_back(s.substr(i)); break; }
        size_t e = (j > i && s[j - 1] == '\r') ? j - 1 : j;
        out.emplace_back(s.substr(i, e - i));
        i = j + 1;
    }
    // Java's split drops trailing empty strings
    while (out.size() > 1 && out.back().empty()) out.pop_back();
    return out;
}
std::string join(std::vector<std::string> const& parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}
bool isBlank(std::string_view s) { return trim(s).empty(); }
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }
bool contains(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }
std::string squeezeSpaces(std::string_view s) {
    std::string out;
    bool        prevWs = false;
    size_t      run    = 0;
    for (char c : s) {
        if (isWs(c)) {
            ++run;
            if (!prevWs) out += c;
            else if (run == 2) out.back() = ' ';
            prevWs = true;
        } else {
            run    = 0;
            prevWs = false;
            out += c;
        }
    }
    return out;
}

std::string fold(std::string_view s) {
    std::string out;
    size_t      i = 0;
    while (i < s.size()) {
        uint32_t c = next(s, i);
        if (c == 0x131 || c == 0x130) c = 'i';
        if (isMark(c)) continue;
        append(out, lower(baseOf(lower(c))));
    }
    return out;
}
std::string norm(std::string_view s) {
    std::string f = fold(s), out;
    size_t      i = 0;
    while (i < f.size()) {
        uint32_t c = next(f, i);
        if (isLetterOrDigit(c)) append(out, c);
    }
    if (out.empty() && !s.empty()) {
        std::string l = toLower(s);
        for (char c : l)
            if (!isWs(c)) out += c;
    }
    return out;
}

// ------------------------------------------------------------------ TitleCleaner
std::string clean(std::string_view s) {
    std::string r = removeBrackets(s);
    r             = std::regex_replace(r, reNoise(), " ");
    static std::regex const tailDash(R"(\s+-\s*$)");
    r = std::regex_replace(r, tailDash, "");
    r = trimmed(squeezeSpaces(r));
    return r.empty() ? trimmed(s) : r;
}
std::string mainArtist(std::string_view a) {
    std::string s(a);
    std::smatch m;
    std::string first = s;
    if (std::regex_search(s, m, reArtistSplit())) first = s.substr(0, size_t(m.position(0)));
    std::string r = trimmed(first);
    return r.empty() ? trimmed(a) : r;
}
std::string stripFeat(std::string_view title) { return std::regex_replace(std::string(title), reFeat(), " "); }

std::vector<std::string> splitYoutube(std::string_view title, std::string_view channel) {
    std::string t = std::regex_replace(std::string(title), std::regex(R"(^\(\d+\)\s*)"), "", std::regex_constants::format_first_only);
    t             = trimmed(t);
    // (?i)\s*[-–—|]\s*youtube( music)?\s*$
    t = std::regex_replace(t, std::regex("\\s*(-|\xE2\x80\x93|\xE2\x80\x94|\\|)\\s*youtube( music)?\\s*$", std::regex::icase), "");
    t = trimmed(t);
    std::string artist(channel);
    artist = std::regex_replace(artist, std::regex(R"(\s*-\s*topic$)", std::regex::icase), "");
    artist = std::regex_replace(artist, std::regex(R"(vevo$)", std::regex::icase), "");
    artist = std::regex_replace(artist, std::regex(R"(\s*official$)", std::regex::icase), "");
    artist = trimmed(artist);
    for (std::string_view sep : {std::string_view(" - "), std::string_view(" \xE2\x80\x93 "), std::string_view(" \xE2\x80\x94 "), std::string_view(" | ")}) {
        size_t i = t.find(sep);
        if (i != std::string::npos && i > 0) {
            std::string a = trimmed(std::string_view(t).substr(0, i));
            std::string s = trimmed(std::string_view(t).substr(i + sep.size()));
            if (!s.empty()) {
                if (sep == " | ") return {artist, a};
                return {a, s};
            }
        }
    }
    return {artist, t};
}

std::vector<Variant> variants(std::string_view artist, std::string_view title) {
    std::string          a(artist), t(title);
    std::vector<Variant> cands{
        {a, t},
        {a, trimmed(stripFeat(t))},
        {a, clean(t)},
        {mainArtist(a), clean(stripFeat(t))},
        {mainArtist(a), std::regex_replace(clean(t), std::regex(R"(\s+-\s+.*$)"), "")},
    };
    std::string ft = clean(stripFeat(t));
    cands.push_back({stripMarksKeepCase(mainArtist(a)), stripMarksKeepCase(ft)});
    std::set<std::string> seen;
    std::vector<Variant>  out;
    for (auto& c : cands) {
        std::string ca = trimmed(c.artist);
        std::string ct = trimmed(squeezeSpaces(c.title));
        if (ct.empty()) continue;
        std::string key = toLower(ca + "|" + ct);
        if (seen.insert(key).second) out.push_back({ca, ct});
    }
    return out;
}

bool        isSpeedVersion(std::string_view title) { std::string s(title); return std::regex_search(s, reSpeed()); }
std::string removeSpeedTags(std::string_view title) { return std::regex_replace(std::string(title), reSpeed(), " "); }
bool        isRemix(std::string_view title) { std::string s(title); return std::regex_search(s, reRemix()); }

} // namespace bf::lyrics::text
