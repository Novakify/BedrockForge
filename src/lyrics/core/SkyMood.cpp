// LyricsForge - [LyricsForge] Sky mood. Original code for Bedrock Forge; see SkyMood.h.
#include "lyrics/core/SkyMood.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "lyrics/core/Lyrics.h"
#include "lyrics/core/SongMemory.h"

namespace bf::lyrics {

namespace {
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

void hsv(uint32_t c, float& h, float& s, float& v) {
    float const r = float((c >> 16) & 255) / 255.0f, g = float((c >> 8) & 255) / 255.0f, b = float(c & 255) / 255.0f;
    float const mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 1e-4f ? d / mx : 0.0f;
    if (d < 1e-4f) { h = 0; return; }
    if (mx == r) h = 60.0f * std::fmod((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else h = 60.0f * ((r - g) / d + 4.0f);
}

// words per mood (whole words, lower case)
constexpr std::string_view kBright[] = {"sun", "sunshine", "sunny", "summer", "happy", "smile", "dance", "dancing", "party", "fun",
                                        "bright", "gold", "golden", "beach", "shine", "sing", "colours", "colors", "pink"};
constexpr std::string_view kHype[]   = {"jump", "bounce", "loud", "wild", "crazy", "hype", "lit", "higher", "faster", "go",
                                        "move", "shake", "turn", "boom", "bang"};
constexpr std::string_view kLove[]   = {"love", "heart", "kiss", "baby", "forever", "darling", "honey", "sweet", "hold", "mine",
                                        "together", "beautiful"};
constexpr std::string_view kSad[]    = {"cry", "crying", "tears", "alone", "lonely", "sad", "goodbye", "cold", "rain", "lost",
                                        "broken", "miss", "gone", "hurt", "pain", "empty", "sorry"};
constexpr std::string_view kDark[]   = {"fire", "burn", "blood", "devil", "kill", "war", "fight", "hell", "demon", "evil",
                                        "scream", "rage", "dead", "die", "monster"};
constexpr std::string_view kNight[]  = {"night", "tonight", "moon", "stars", "star", "midnight", "dream", "dreams", "sleep",
                                        "sky", "neon", "lights", "dark"};

template <size_t N> bool hasWord(std::vector<std::string> const& ws, std::string_view const (&set)[N]) {
    for (auto const& w : ws)
        for (auto s : set) if (w == s) return true;
    return false;
}
std::vector<std::string> wordsOf(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        unsigned char const c = static_cast<unsigned char>(ch);
        if (std::isalpha(c) || c == '\'') cur.push_back(char(std::tolower(c)));
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    for (auto& w : out) if (w.size() > 2 && w.compare(w.size() - 2, 2, "'s") == 0) w.resize(w.size() - 2);
    return out;
}
} // namespace

char const* moodName(SkyMood m) {
    static char const* const n[] = {"bright", "hype", "dreamy", "melancholy", "dark", "night"};
    return n[std::min<size_t>(size_t(m), 5)];
}

MoodScore moodOf(MoodIn const& in) {
    float const energy = in.highShare >= 0 ? clamp01(0.25f + 0.75f * in.highShare - 0.3f * std::max(0.0f, in.calmShare))
                       : in.liveEnergy >= 0 ? clamp01(in.liveEnergy) : 0.5f;
    float const hype = clamp01(0.8f * energy + 0.4f * std::min(1.0f, in.dropsPerMin / 2.0f) - 0.15f);
    float h = 0, s = 0, v = 0;
    bool const cov = in.cover != 0;
    if (cov) hsv(in.cover, h, s, v);
    bool const warm   = cov && s > 0.3f && (h < 55 || h > 330);
    bool const pink   = cov && s > 0.25f && h >= 285 && h <= 345;
    bool const cool   = cov && s > 0.3f && h >= 170 && h <= 265;
    bool const dark   = cov && v < 0.35f;
    bool const pastel = cov && s < 0.5f && v > 0.7f;
    bool const red    = cov && s > 0.5f && (h < 18 || h > 342);
    // words: share of lines that carry a word of the mood (x2, capped)
    float w[6] = {};
    int lines = 0;
    for (auto line : in.words) {
        auto const ws = wordsOf(line);
        if (ws.empty()) continue;
        ++lines;
        w[0] += hasWord(ws, kBright); w[1] += hasWord(ws, kHype); w[2] += hasWord(ws, kLove);
        w[3] += hasWord(ws, kSad);    w[4] += hasWord(ws, kDark); w[5] += hasWord(ws, kNight);
    }
    if (lines > 0) for (float& x : w) x = clamp01(2.0f * x / float(lines));
    MoodScore r;
    float* sc = r.score;
    sc[0] = 0.5f * energy + (warm ? 0.25f : 0) + (pastel ? 0.15f : 0) + (cov && v > 0.75f ? 0.1f : 0) + 0.7f * w[0] - 0.4f * w[3] - (dark ? 0.35f : 0);
    sc[1] = 0.9f * hype + (cov && s > 0.6f ? 0.15f : 0) + 0.6f * w[1] - 0.25f * w[3] - 0.25f;
    sc[2] = 0.35f * (1.0f - std::fabs(energy - 0.45f) * 2.0f) + (pink ? 0.3f : 0) + (pastel ? 0.15f : 0) + 0.9f * w[2];
    sc[3] = 0.5f * (1.0f - energy) + (cov && s < 0.3f ? 0.2f : 0) + (cool ? 0.15f : 0) + 1.0f * w[3] - 0.2f;
    sc[4] = 0.4f * energy + (dark ? 0.35f : 0) + (red && v < 0.7f ? 0.2f : 0) + 1.0f * w[4] - 0.35f;
    sc[5] = 0.3f * (1.0f - std::fabs(energy - 0.4f) * 2.0f) + (cool ? 0.25f : 0) + (dark ? 0.15f : 0) + 0.9f * w[5];
    int best = 0;
    for (int i = 1; i < 6; ++i) if (sc[i] > sc[best] + 1e-4f) best = i;
    r.mood = SkyMood(best);
    return r;
}

MoodIn moodInputs(SongMemory const* mem, Lyrics const* lyrics, uint32_t cover, float liveEnergy) {
    MoodIn in;
    in.cover = cover & 0xFFFFFF;
    in.liveEnergy = liveEnergy;
    if (mem && !mem->sections.empty()) {
        int64_t tot = 0, hi = 0, calm = 0;
        for (auto const& s : mem->sections) {
            int64_t const d = std::max<int64_t>(0, s.end - s.start);
            tot += d;
            if (s.kind == SongMemory::Section::Kind::High) hi += d;
            if (s.kind == SongMemory::Section::Kind::Calm) calm += d;
        }
        if (tot > 0) { in.highShare = float(hi) / float(tot); in.calmShare = float(calm) / float(tot); }
        int drops = 0;
        for (auto const& e : mem->events) drops += e.kind == SongMemory::Event::Kind::Drop;
        if (tot > 0) in.dropsPerMin = float(drops) / (float(tot) / 60000.0f);
    }
    if (lyrics)
        for (auto const& l : lyrics->lines) if (l.index >= 0 && !l.text.empty()) in.words.emplace_back(l.text);
    return in;
}

std::vector<std::string_view> const& moodSkies(SkyMood m) {
    static std::vector<std::string_view> const lists[6] = {
        {"dawn", "cotton_candy", "silver_lining", "ocean", "summer", "prism", "golden_hour"},      // bright
        {"summer", "golden_hour", "sunset", "dusk", "bright_night", "supernova"},                 // hype
        {"dawn", "cotton_candy", "aquarell", "dream_ocean", "sakura", "golden_hour", "dusk"},     // dreamy
        {"silver_lining", "storm", "frost", "midnight", "thunder", "bright_night"},              // melancholy
        {"storm", "crimson", "inferno", "eclipse", "blood_moon", "eclipse_heart"},               // dark
        {"midnight", "cosmos", "aurora", "bright_night", "galaxy", "nebula"},                    // night
    };
    return lists[std::min<size_t>(size_t(m), 5)];
}

std::vector<std::string_view> const& curatedSkies() { // [LyricsForge] after the polish pass: every sky but the gimmick
    static std::vector<std::string_view> const c = {
        "summer", "dawn", "dusk", "bright_night", "silver_lining", "golden_hour", "cotton_candy", "sakura", "aurora",
        "midnight", "nebula", "eclipse", "blood_moon", "storm", "supernova", "synthwave", "black_hole", "cosmos",
        "neon_city", "frost", "deep_sea", "inferno", "prism", "pulsar", "aquarell", "polar", "galaxy", "ocean", "ice",
        "purple", "sunset", "cherry", "crimson", "thunder", "rift", "warp", "silk", "astral", "eclipse_heart",
        "dream_ocean", "meteor"};
    return c;
}
bool isCurated(std::string_view id) {
    auto const& c = curatedSkies();
    return std::find(c.begin(), c.end(), id) != c.end();
}

std::string pickMoodSky(SkyMood m, float intensity, std::vector<std::string> const& recent, std::string_view current, uint32_t rng) {
    auto const& l = moodSkies(m);
    int const   n = int(l.size());
    float const at = clamp01(intensity) * float(n - 1);
    auto seen = [&](std::string_view id) { return std::find(recent.begin(), recent.end(), id) != recent.end(); };
    for (float reach : {1.01f, 1.6f, 99.0f}) { // widen the window until something fresh fits
        std::vector<std::string_view> c;
        for (int i = 0; i < n; ++i)
            if (std::fabs(float(i) - at) <= reach && l[size_t(i)] != current && (reach > 50 || !seen(l[size_t(i)]))) c.push_back(l[size_t(i)]);
        if (!c.empty()) {
            // favour the one closest to the intensity: the nearest gets a double share
            std::vector<std::string_view> w;
            for (auto id : c) {
                int const i = int(std::find(l.begin(), l.end(), id) - l.begin());
                w.push_back(id);
                if (std::fabs(float(i) - at) < 0.51f) w.push_back(id);
            }
            return std::string(w[size_t((rng >> 8) % w.size())]);
        }
    }
    return std::string(l.empty() ? std::string_view("summer") : l[0]);
}

std::vector<std::string> coverSkies(uint32_t cover) {
    // colour families by how each sky LOOKS in game (its shader palette), most fitting first. A preset's stored base
    // colours don't say it (cotton candy stores a blue zenith but reads pink), so this is curated by eye.
    static std::vector<std::string_view> const pink   = {"cotton_candy", "sakura", "cherry", "dream_ocean", "aquarell", "synthwave", "eclipse_heart", "neon_city", "dusk", "purple"};
    static std::vector<std::string_view> const red    = {"crimson", "sunset", "eclipse_heart", "blood_moon", "inferno", "cherry"};
    static std::vector<std::string_view> const orange = {"golden_hour", "sunset", "summer", "dawn", "inferno", "dusk"};
    static std::vector<std::string_view> const green  = {"aurora", "polar", "summer", "silver_lining", "prism", "ocean"};
    static std::vector<std::string_view> const cyan   = {"ocean", "dream_ocean", "frost", "polar", "deep_sea", "aurora", "ice"};
    static std::vector<std::string_view> const blue   = {"ocean", "summer", "frost", "prism", "bright_night", "ice", "silver_lining", "midnight"};
    static std::vector<std::string_view> const violet = {"purple", "nebula", "galaxy", "astral", "cosmos", "rift", "pulsar", "silk"};
    std::vector<std::string> out;
    float h, sat, v;
    hsv(cover & 0xFFFFFF, h, sat, v);
    if ((cover & 0xFFFFFF) == 0 || sat < 0.35f || v < 0.35f) return out; // grey, dark or no cover: nothing to match
    auto const& l = h >= 285 && h < 345 ? pink : (h >= 345 || h < 15) ? red : h < 55 ? orange : h < 150 ? green
                  : h < 200 ? cyan : h < 250 ? blue : violet;
    for (auto id : l) if (isCurated(id)) out.emplace_back(id);
    return out;
}

std::string pickCoverSky(uint32_t cover, std::vector<std::string> const& recent, std::string_view current, uint32_t rng) {
    auto const l = coverSkies(cover);
    std::vector<std::string_view> c;
    for (size_t i = 0; i < l.size() && i < 8; ++i) // the eight closest: enough variety, still clearly the cover's colour
        if (l[i] != current && std::find(recent.begin(), recent.end(), l[i]) == recent.end()) c.push_back(l[i]);
    if (c.empty()) for (size_t i = 0; i < l.size() && i < 8; ++i) if (l[i] != current) c.push_back(l[i]);
    return c.empty() ? std::string() : std::string(c[size_t((rng >> 8) % c.size())]);
}

} // namespace bf::lyrics
