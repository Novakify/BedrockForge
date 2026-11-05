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
        {"dawn", "cotton_candy", "silver_lining", "summer", "golden_hour"},   // bright
        {"summer", "golden_hour", "dusk", "bright_night", "supernova"},      // hype
        {"dawn", "cotton_candy", "sakura", "golden_hour", "dusk"},           // dreamy
        {"silver_lining", "storm", "midnight", "bright_night"},             // melancholy
        {"storm", "eclipse", "dusk", "blood_moon"},                          // dark
        {"midnight", "bright_night", "aurora", "nebula"},                    // night
    };
    return lists[std::min<size_t>(size_t(m), 5)];
}

std::vector<std::string_view> const& curatedSkies() {
    static std::vector<std::string_view> const c = {"summer", "dawn", "dusk", "bright_night", "silver_lining", "golden_hour",
                                                     "cotton_candy", "sakura", "aurora", "midnight", "nebula", "eclipse",
                                                     "blood_moon", "storm", "supernova"};
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

} // namespace bf::lyrics
