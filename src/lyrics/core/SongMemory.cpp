// LyricsForge - [BF] Song Memory, see SongMemory.h. Original code for Bedrock Forge.
#include "lyrics/core/SongMemory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>

#include "lyrics/core/Lyrics.h"

namespace bf::lyrics {

namespace {
constexpr char     kMagic[4] = {'L', 'F', 'M', '1'};
constexpr uint32_t kVersion  = 1;

uint8_t q8(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

std::string mmss(int64_t ms) {
    char b[16];
    int64_t s = std::max<int64_t>(0, ms) / 1000;
    std::snprintf(b, sizeof b, "%lld:%02lld", (long long)(s / 60), (long long)(s % 60));
    return b;
}

std::string normLine(std::string const& s) {
    std::string out;
    bool        space = false;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c >= 0x80) {
            if (space && !out.empty()) out += ' ';
            out += char(std::tolower(c));
            space = false;
        } else space = true;
    }
    return out;
}

template <class T> void put(std::ofstream& o, T v) { o.write(reinterpret_cast<char const*>(&v), sizeof v); }
template <class T> bool get(std::ifstream& in, T& v) { return bool(in.read(reinterpret_cast<char*>(&v), sizeof v)); }
} // namespace

std::string SongMemory::keyFor(std::string const& trackKey, int64_t) {
    // artist + title only: Apple Music can report the PREVIOUS song's length for a while after a change (Nov 4: a 4:08
    // song came in as 4:44). Remixes / sped-up uploads have their own titles.
    return trackKey + "|2";
}

std::string SongMemory::fileNameFor(std::string const& key) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx.lfm", (unsigned long long)h);
    return b;
}

void SongMemory::reset(std::string key, int64_t durationMs) {
    mKey        = std::move(key);
    mDurationMs = std::max<int64_t>(0, durationMs);
    mPlays      = 0;
    mFrames.clear(); mCounts.clear(); mPlay.clear(); mPlayHeard.clear();
    mHeardFrames = 0;
    mLastIdx     = -10;
    mEnergy.clear(); sections.clear(); events.clear(); hooks.clear();
    size_t const n = size_t(mDurationMs / kFrameMs) + 20;
    mFrames.reserve(n); mCounts.reserve(n); mPlay.reserve(n); mPlayHeard.reserve(n);
}

void SongMemory::grow(size_t n) {
    if (mFrames.size() < n) { mFrames.resize(n); mCounts.resize(n, 0); }
    if (mPlay.size() < n) { mPlay.resize(n); mPlayHeard.resize(n, 0); }
}

void SongMemory::record(int64_t posMs, BandFrame const& f) {
    if (posMs < 0) return;
    if (mDurationMs > 0 && posMs > mDurationMs + 5000) return;
    long const idx = long(posMs / kFrameMs);
    if (idx > 200000) return;
    grow(size_t(idx) + 1);
    Frame fr{q8(f.bass), q8(f.mid), q8(f.treble), q8(f.loud)};
    auto mark = [&](long i) {
        mPlay[size_t(i)] = fr;
        if (!mPlayHeard[size_t(i)]) { mPlayHeard[size_t(i)] = 1; ++mHeardFrames; }
    };
    if (mLastIdx < idx && idx - mLastIdx <= 3) // a frame or two fell between polls: fill the small gap
        for (long i = std::max(0L, mLastIdx + 1); i < idx; ++i) mark(i);
    mark(idx);
    mLastIdx = idx;
}

bool SongMemory::endPlay() {
    bool counted = mHeardFrames >= 15000 / kFrameMs;
    if (counted) {
        grow(mPlay.size());
        for (size_t i = 0; i < mPlay.size(); ++i) {
            if (!mPlayHeard[i]) continue;
            Frame&       a = mFrames[i];
            Frame const& b = mPlay[i];
            int const    n = std::min<int>(mCounts[i], 7); // newer plays keep some weight
            auto avg = [n](uint8_t o, uint8_t v) { return uint8_t((int(o) * n + int(v) + n / 2) / (n + 1)); };
            a = n == 0 ? b : Frame{avg(a.bass, b.bass), avg(a.mid, b.mid), avg(a.treble, b.treble), avg(a.loud, b.loud)};
            mCounts[i] = uint8_t(std::min<int>(mCounts[i] + 1, 250));
        }
        int64_t const heard = int64_t(mHeardFrames) * kFrameMs;
        if (mDurationMs > 0 ? heard * 2 >= mDurationMs : heard >= 30000) ++mPlays;
    }
    std::fill(mPlayHeard.begin(), mPlayHeard.end(), uint8_t(0));
    mHeardFrames = 0;
    mLastIdx     = -10;
    return counted;
}

float SongMemory::coverage() const {
    size_t known = 0;
    for (uint8_t c : mCounts) known += c ? 1 : 0;
    size_t const total = mDurationMs > 0 ? size_t(mDurationMs / kFrameMs) : std::max<size_t>(1, mCounts.size());
    return total ? std::min(1.0f, float(known) / float(total)) : 0.0f;
}

void SongMemory::analyze(Lyrics const* lyrics) {
    sections.clear(); events.clear(); hooks.clear();
    size_t const n   = mFrames.size();
    float const  NaN = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> e(n, NaN);
    for (size_t i = 0; i < n; ++i)
        if (mCounts[i]) {
            Frame const& f = mFrames[i];
            e[i] = (0.40f * f.loud + 0.30f * f.bass + 0.20f * f.mid + 0.10f * f.treble) / 255.0f;
        }
    auto smooth = [&](int half) {
        std::vector<float> s(n, NaN);
        for (size_t i = 0; i < n; ++i) {
            if (std::isnan(e[i])) continue;
            double sum = 0;
            int    c   = 0;
            for (long k = long(i) - half; k <= long(i) + half; ++k)
                if (k >= 0 && size_t(k) < n && !std::isnan(e[size_t(k)])) { sum += e[size_t(k)]; ++c; }
            s[i] = c ? float(sum / c) : NaN;
        }
        return s;
    };
    std::vector<float> slow = smooth(20), fast = smooth(3);
    std::vector<float> known;
    for (float v : slow) if (!std::isnan(v)) known.push_back(v);
    if (known.size() >= 200) {
        std::sort(known.begin(), known.end());
        float const p10 = known[known.size() / 10], p95 = known[known.size() * 95 / 100];
        float const span = std::max(1e-3f, p95 - p10);
        auto norm = [&](float v) { return std::isnan(v) ? NaN : std::clamp((v - p10) / span, 0.0f, 1.0f); };
        mEnergy.assign(n, NaN);
        for (size_t i = 0; i < n; ++i) { mEnergy[i] = norm(slow[i]); fast[i] = norm(fast[i]); }

        // sections: calm / mid / high runs; runs under 4 s join the one before
        auto kindOf = [](float v) { return v < 0.33f ? Section::Kind::Calm : v < 0.66f ? Section::Kind::Mid : Section::Kind::High; };
        for (size_t i = 0; i < n;) {
            if (std::isnan(mEnergy[i])) { ++i; continue; }
            Section::Kind k = kindOf(mEnergy[i]);
            size_t        j = i;
            double        sum = 0;
            while (j < n && !std::isnan(mEnergy[j]) && kindOf(mEnergy[j]) == k) sum += mEnergy[j++];
            Section s{int64_t(i) * kFrameMs, int64_t(j) * kFrameMs, k, float(sum / double(j - i))};
            if (!sections.empty() && sections.back().end == s.start && (s.end - s.start < 4000 || sections.back().kind == s.kind)) {
                auto& b  = sections.back();
                b.level  = (b.level * float(b.end - b.start) + s.level * float(s.end - s.start)) / float(s.end - b.start);
                b.end    = s.end;
            } else sections.push_back(s);
            i = j;
        }

        // drops (tuned on real plays, Nov 4): the 4 s after a moment vs the 4 s before, on the 2 s-smoothed curve.
        // A drop = that jump is >= 0.25 and the new part sits at >= 0.5. At least 20 s apart, at most 6 per song
        // (the strongest), so bar-to-bar swells, a TNT blast or a chat ping never count.
        auto meanN = [&](long a, long b) { // [a, b) of mEnergy, NaN when under 70 % known
            double sum = 0;
            int    c = 0, all = 0;
            for (long k = std::max(0L, a); k < std::min(long(n), b); ++k, ++all)
                if (!std::isnan(mEnergy[size_t(k)])) { sum += mEnergy[size_t(k)]; ++c; }
            return (all && c * 10 >= all * 7) ? float(sum / c) : NaN;
        };
        std::vector<float> nov(n, -1.0f);
        for (long i = 80; i + 80 <= long(n); ++i) {
            float before = meanN(i - 80, i), after = meanN(i, i + 80);
            if (!std::isnan(before) && !std::isnan(after)) nov[size_t(i)] = after - before;
        }
        std::vector<std::pair<long, float>> cand;
        for (long i = 0; i < long(n);) {
            if (nov[size_t(i)] < 0.25f) { ++i; continue; }
            long j = i, pk = i;
            while (j < long(n) && nov[size_t(j)] >= 0.25f) { if (nov[size_t(j)] > nov[size_t(pk)]) pk = j; ++j; }
            float after = meanN(pk, pk + 80);
            if (!std::isnan(after) && after >= 0.5f) cand.push_back({pk, nov[size_t(pk)]});
            i = j;
        }
        std::sort(cand.begin(), cand.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
        std::vector<std::pair<long, float>> keep;
        for (auto const& c : cand) {
            bool far = true;
            for (auto const& k : keep) far &= std::labs(k.first - c.first) >= 400;
            if (far && keep.size() < 6) keep.push_back(c);
        }
        for (auto const& [pk, v] : keep) {
            Event ev;
            ev.kind     = Event::Kind::Drop;
            ev.at       = int64_t(pk) * kFrameMs;
            ev.strength = std::clamp(v * 1.6f, 0.0f, 1.0f);
            long k = pk - 1; // the build: energy really climbing for 3..16 s before it (a flat stretch is no build)
            while (k > 0 && pk - k < 320 && !std::isnan(mEnergy[size_t(k - 1)]) && mEnergy[size_t(k - 1)] < mEnergy[size_t(k)] - 0.0005f) --k;
            if (pk - k >= 60 && !std::isnan(mEnergy[size_t(pk - 1)]) && mEnergy[size_t(pk - 1)] - mEnergy[size_t(k)] >= 0.15f)
                ev.buildFrom = int64_t(k) * kFrameMs;
            events.push_back(ev);
        }
        std::sort(events.begin(), events.end(), [](Event const& a, Event const& b) { return a.at < b.at; });
    } else mEnergy.clear();

    // hooks: lyric lines that come back (choruses), merged into time ranges
    if (lyrics) {
        std::map<std::string, int> count;
        for (auto const& l : lyrics->lines)
            if (l.index >= 0) ++count[normLine(l.text)];
        // repeated lines within 6 s of each other form one block; a block needs 2+ lines and 6+ s (tuned on real
        // songs: a chorus is usually broken up by ad-libs / unique lines, which must not split it)
        std::vector<int> lines;
        for (auto const& l : lyrics->lines) {
            if (l.index < 0) continue;
            std::string const k = normLine(l.text);
            if (count[k] < 2 || std::count(k.begin(), k.end(), ' ') < 1) continue;
            if (!hooks.empty() && l.start - hooks.back().second < 6000) { hooks.back().second = std::max(hooks.back().second, l.end); ++lines.back(); }
            else { hooks.push_back({l.start, l.end}); lines.push_back(1); }
        }
        for (size_t i = hooks.size(); i-- > 0;)
            if (lines[i] < 2 || hooks[i].second - hooks[i].first < 6000) hooks.erase(hooks.begin() + long(i));
        for (auto const& h : hooks) { // a chorus event where a hook block starts (unless a drop is right there)
            bool nearDrop = false;
            for (auto const& ev : events) nearDrop |= ev.kind == Event::Kind::Drop && std::llabs(ev.at - h.first) < 3000;
            if (nearDrop) continue;
            float lv = energyAt(h.first + 1000);
            events.push_back({h.first, -1, Event::Kind::Chorus, lv < 0 ? 0.6f : lv});
        }
        std::sort(events.begin(), events.end(), [](Event const& a, Event const& b) { return a.at < b.at; });
    }
}

float SongMemory::energyAt(int64_t posMs) const {
    if (posMs < 0) return -1;
    size_t const i = size_t(posMs / kFrameMs);
    if (i >= mEnergy.size() || std::isnan(mEnergy[i])) return -1;
    return mEnergy[i];
}

SongMemory::Event const* SongMemory::nextEvent(int64_t posMs, int64_t horizonMs) const {
    for (auto const& e : events)
        if (e.at > posMs && e.at <= posMs + horizonMs) return &e;
    return nullptr;
}

void SongMemory::adoptMap(SongMemory&& saved) {
    if (saved.mKey != mKey || saved.mFrames.empty()) return;
    mFrames = std::move(saved.mFrames);
    mCounts = std::move(saved.mCounts);
    mPlays  = saved.mPlays;
    if (mDurationMs <= 0) mDurationMs = saved.mDurationMs;
    if (mPlay.size() < mFrames.size()) { mPlay.resize(mFrames.size()); mPlayHeard.resize(mFrames.size(), 0); }
}

bool SongMemory::save(std::filesystem::path const& file) const {
    if (mKey.empty() || mFrames.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(kMagic, 4);
        put(o, kVersion);
        put(o, uint32_t(kFrameMs));
        put(o, int64_t(mDurationMs));
        put(o, uint32_t(mPlays));
        put(o, uint32_t(mFrames.size()));
        put(o, uint16_t(std::min<size_t>(mKey.size(), 4000)));
        o.write(mKey.data(), std::streamsize(std::min<size_t>(mKey.size(), 4000)));
        o.write(reinterpret_cast<char const*>(mFrames.data()), std::streamsize(mFrames.size() * sizeof(Frame)));
        o.write(reinterpret_cast<char const*>(mCounts.data()), std::streamsize(mCounts.size()));
        if (!o) return false;
    }
    std::filesystem::rename(tmp, file, ec); // whole file or the old one, never half
    return !ec;
}

bool SongMemory::load(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    char     magic[4];
    uint32_t ver = 0, fms = 0, plays = 0, n = 0;
    int64_t  dur = 0;
    uint16_t kl  = 0;
    if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) return false;
    if (!get(in, ver) || ver != kVersion || !get(in, fms) || fms != uint32_t(kFrameMs) || !get(in, dur) || !get(in, plays) || !get(in, n) ||
        n > 200000 || !get(in, kl))
        return false;
    std::string key(kl, '\0');
    if (!in.read(key.data(), kl)) return false;
    if (!mKey.empty() && key != mKey) return false; // a different song hashed to the same name
    std::vector<Frame>   frames(n);
    std::vector<uint8_t> counts(n);
    if (!in.read(reinterpret_cast<char*>(frames.data()), std::streamsize(n * sizeof(Frame)))) return false;
    if (!in.read(reinterpret_cast<char*>(counts.data()), std::streamsize(n))) return false;
    mKey    = key;
    mPlays  = int(plays);
    if (mDurationMs <= 0) mDurationMs = dur;
    mFrames = std::move(frames);
    mCounts = std::move(counts);
    if (mPlay.size() < mFrames.size()) { mPlay.resize(mFrames.size()); mPlayHeard.resize(mFrames.size(), 0); }
    return true;
}

std::string SongMemory::describe() const {
    std::string s;
    char        b[160];
    std::snprintf(b, sizeof b, "plays %d, map covers %.0f%% of %s\n", mPlays, coverage() * 100.0f, mmss(mDurationMs).c_str());
    s += b;
    static char const* const kinds[] = {"calm", "mid", "high"};
    for (auto const& x : sections) {
        std::snprintf(b, sizeof b, "  %s-%s  %-4s %.2f\n", mmss(x.start).c_str(), mmss(x.end).c_str(), kinds[int(x.kind)], x.level);
        s += b;
    }
    for (auto const& e : events) {
        if (e.kind == Event::Kind::Drop)
            std::snprintf(b, sizeof b, "  DROP   at %s%s%s  strength %.2f\n", mmss(e.at).c_str(), e.buildFrom >= 0 ? "  (build from " : "",
                          e.buildFrom >= 0 ? (mmss(e.buildFrom) + ")").c_str() : "", e.strength);
        else std::snprintf(b, sizeof b, "  CHORUS at %s  energy %.2f\n", mmss(e.at).c_str(), e.strength);
        s += b;
    }
    std::snprintf(b, sizeof b, "  %zu repeated-lyric block(s)\n", hooks.size());
    s += b;
    return s;
}

} // namespace bf::lyrics
