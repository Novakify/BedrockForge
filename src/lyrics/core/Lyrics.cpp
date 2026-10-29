// LyricsFlow port - lyrics model + parsers. Logic ported from LyricsFlow v2.0 (374m, MIT); see Lyrics.h.
#include "lyrics/core/Lyrics.h"

#include <algorithm>
#include <cmath>
#include <regex>

#include "lyrics/core/Json.h"
#include "lyrics/core/Text.h"

namespace bf::lyrics {

namespace {
int64_t jround(double x) { return int64_t(std::floor(x + 0.5)); } // Java Math.round(double)

int64_t fracMs(std::string const& f, bool clampTo3) {
    if (f.empty()) return 0;
    if (f.size() == 1) return std::stoll(f) * 100;
    if (f.size() == 2) return std::stoll(f) * 10;
    return clampTo3 ? std::stoll(f.substr(0, 3)) : std::stoll(f);
}

// LyricsFetcher.enhancedWords: <mm:ss.xx> word tags inside one LRC line (A2 / "enhanced LRC").
std::vector<Word> enhancedWords(std::string const& body) {
    static std::regex const tag(R"(<(\d+):(\d+)(?:\.(\d+))?>)");
    std::vector<int64_t>                at;
    std::vector<std::pair<size_t, size_t>> pos;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), tag); it != std::sregex_iterator(); ++it) {
        auto const& m  = *it;
        int64_t     ms = std::stoll(m[1].str()) * 60000 + std::stoll(m[2].str()) * 1000;
        if (m[3].matched) ms += fracMs(m[3].str(), true);
        at.push_back(ms);
        pos.emplace_back(size_t(m.position(0)), size_t(m.position(0) + m.length(0)));
    }
    if (at.size() < 2) return {};
    std::vector<Word> out;
    for (size_t i = 0; i < at.size(); ++i) {
        size_t      from = pos[i].second;
        size_t      to   = i + 1 < pos.size() ? pos[i + 1].first : body.size();
        std::string seg  = text::trimmed(std::string_view(body).substr(from, to - from));
        if (seg.empty()) continue;
        auto    parts = text::splitWs(seg);
        int64_t st    = at[i];
        int64_t en    = i + 1 < at.size() ? at[i + 1] : st + int64_t(parts.size()) * 450;
        auto    n     = int64_t(parts.size());
        for (int64_t k = 0; k < n; ++k) {
            int64_t a = st + (en - st) * k / n;
            int64_t b = st + (en - st) * (k + 1) / n;
            out.push_back(Word::of(parts[size_t(k)], a, b));
        }
    }
    return out;
}

bool hanThenColon(std::string_view s) { // ".*[一-鿿].*[:：].*"
    auto cps = text::codepoints(s);
    bool han = false;
    for (uint32_t c : cps) {
        if (han && (c == ':' || c == 0xFF1A)) return true;
        if (text::isHan(c)) han = true;
    }
    return false;
}
} // namespace

// ------------------------------------------------------------------ estimation
int syllables(std::string_view w) {
    static std::u32string const vowels = U"aeiouyıöüâêîôûàèìòùáéíóúäëïãõåæøœ";
    auto                        cps    = text::codepoints(w);
    int                         n      = 0;
    bool                        prev   = false;
    for (uint32_t c : cps) {
        bool v = vowels.find(char32_t(text::lower(c))) != std::u32string::npos;
        if (v && !prev) ++n;
        prev = v;
    }
    if (n == 0) {
        int letters = 0;
        for (uint32_t c : cps)
            if (text::isLetterOrDigit(c)) ++letters;
        n = std::max(1, int(jround(double(letters) / 2.5)));
    }
    return n;
}

double songRate(std::vector<Timing> const& times, std::vector<std::string> const& texts) {
    std::vector<double> r;
    for (size_t i = 0; i < texts.size(); ++i) {
        int64_t s = times[i].start;
        int64_t e = times[i].end >= 0 ? times[i].end : (i + 1 < texts.size() ? times[i + 1].start : -1);
        if (e <= s) continue;
        int64_t gap = e - s;
        if (gap < 700 || gap > 8000) continue;
        auto parts = text::splitWs(texts[i]);
        if (parts.size() < 2) continue;
        int syl = 0;
        for (auto& p : parts) syl += syllables(p);
        r.push_back(double(syl) / (double(gap) / 1000.0));
    }
    if (r.size() < 3) return 4.5;
    std::sort(r.begin(), r.end());
    double busy = r[std::min(r.size() - 1, size_t(double(r.size()) * 0.75))];
    return std::max(2.5, std::min(10.0, busy * 1.05));
}

std::vector<Word> splitWords(std::string_view textIn, int64_t start, int64_t end, double rate) {
    auto              parts = text::splitWs(textIn);
    std::vector<Word> out;
    if (parts.empty() || parts[0].empty()) return out;
    int64_t          gap = std::max<int64_t>(300, end - start);
    std::vector<int> syl(parts.size());
    int              total = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        syl[i] = syllables(parts[i]);
        total += syl[i];
    }
    double  lineRate = double(total) / (double(gap) / 1000.0);
    int64_t sing;
    if (lineRate >= rate * 0.9) sing = int64_t(double(gap) * 0.74);
    else sing = std::max(int64_t(double(total) / rate * 1000.0), int64_t(double(gap) * 0.6));
    sing = std::max(std::min<int64_t>(350, gap), std::min(sing, int64_t(double(gap) * 0.82)));
    double weight = 0.0;
    for (int n : syl) weight += double(n) + 0.6;
    double t = double(start);
    for (size_t i = 0; i < parts.size(); ++i) {
        double  d    = double(sing) * ((double(syl[i]) + 0.6) / weight);
        int64_t prev = out.empty() ? start - 1000 : out.back().start;
        int64_t ws   = i == 0 ? start : std::max(prev + 130, int64_t(t) - 60);
        out.push_back(Word::of(parts[i], ws, std::max(ws + 60, int64_t(t + d) - 60)));
        t += d;
    }
    return out;
}

// ------------------------------------------------------------------ Line / Lyrics
void Line::retime(double k) {
    if (!estimated || words.empty()) return;
    int64_t limit = end - 40;
    for (auto& w : words) {
        w.start = std::min(limit, start + jround(double(w.origStart - start) * k));
        w.end   = std::min(limit, start + jround(double(w.origEnd - start) * k));
    }
}
int64_t Line::guessedEnd() const { return words.empty() ? end : words.back().origEnd; }
int     Line::wordAt(int64_t t) const {
    if (words.empty() || t < start) return -1;
    for (int i = int(words.size()) - 1; i >= 0; --i)
        if (t >= words[size_t(i)].start) return i;
    return -1;
}
Line Line::notes(int index, std::string text, int64_t start, int64_t end) {
    Line l;
    l.index     = index;
    l.start     = start;
    l.end       = end;
    l.words     = splitWords(text, start, end, 4.5);
    l.text      = std::move(text);
    l.estimated = true;
    return l;
}

Lyrics Lyrics::build(std::vector<Timing> const& times, std::vector<std::string> const& texts, bool synced, std::string source,
                     int64_t durationMs, std::vector<std::vector<Word>> const* words) {
    Lyrics out;
    double rate = songRate(times, texts);
    for (size_t i = 0; i < texts.size(); ++i) {
        int64_t s = times[i].start;
        int64_t e = times[i].end >= 0 ? times[i].end
                  : (i + 1 < texts.size() ? times[i + 1].start : (durationMs > s ? std::min(durationMs, s + 6000) : s + 5000));
        if (e - s > 9000) e = s + 9000;
        std::vector<Word> const* w = (words && i < words->size()) ? &(*words)[i] : nullptr;
        if (w && !w->empty()) e = std::max(e, w->back().end);
        Line l;
        l.index     = int(out.lines.size());
        l.text      = texts[i];
        l.start     = s;
        l.end       = e;
        l.estimated = !w || w->empty();
        l.words     = l.estimated ? splitWords(l.text, s, e, rate) : *w;
        out.lines.push_back(std::move(l));
    }
    out.synced = synced;
    out.source = std::move(source);
    bool ws    = words != nullptr;
    if (ws)
        for (auto const& w : *words)
            if (w.empty()) { ws = false; break; }
    out.wordSynced = ws && !out.lines.empty();
    return out;
}

Line const* Lyrics::lineAt(int64_t t) const {
    Line const* cur = nullptr;
    for (auto const& l : lines) {
        if (l.start > t) break;
        cur = &l;
    }
    return (cur && t > cur->end) ? nullptr : cur;
}
Line const* Lyrics::lastStartedBefore(int64_t t) const {
    Line const* cur = nullptr;
    for (auto const& l : lines) {
        if (l.start > t) break;
        cur = &l;
    }
    return cur;
}

// ------------------------------------------------------------------ parsers
std::optional<Lyrics> parseLrc(std::string_view lrc, int64_t durationMs, std::string source) {
    struct Entry {
        int64_t           t;
        std::string       text;
        std::vector<Word> words;
    };
    static std::regex const stampRe(R"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    static std::regex const tagRe(R"(<\d+:\d+(?:\.\d+)?>)");
    std::vector<Entry> entries;
    for (auto const& raw : text::splitLines(lrc)) {
        std::vector<int64_t> stamps;
        size_t               end = 0;
        for (auto it = std::sregex_iterator(raw.begin(), raw.end(), stampRe); it != std::sregex_iterator(); ++it) {
            if (size_t(it->position(0)) != end) break;
            auto const& m  = *it;
            int64_t     ms = std::stoll(m[1].str()) * 60000 + std::stoll(m[2].str()) * 1000;
            if (m[3].matched) ms += fracMs(m[3].str(), false);
            stamps.push_back(ms);
            end = size_t(m.position(0) + m.length(0));
        }
        if (stamps.empty()) continue;
        std::string body = raw.substr(end);
        std::string txt  = text::trimmed(std::regex_replace(body, tagRe, ""));
        auto        w    = enhancedWords(body);
        for (int64_t s : stamps) entries.push_back({s, txt, stamps.size() == 1 ? w : std::vector<Word>{}});
    }
    std::stable_sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return a.t < b.t; });
    std::vector<Timing>            times;
    std::vector<std::string>       texts;
    std::vector<std::vector<Word>> wl;
    bool                           anyWords = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& en = entries[i];
        if (en.text.empty() || en.text == "\xE2\x99\xAA") continue; // "♪"
        int64_t s = en.t;
        int64_t e = i + 1 < entries.size() ? entries[i + 1].t : -1;
        times.push_back({s, e > s ? e : -1});
        texts.push_back(en.text);
        if (!en.words.empty()) {
            anyWords   = true;
            Word& last = en.words.back();
            if (last.end <= last.start) last = Word::of(last.text, last.start, e > last.start ? std::min(e, last.start + 1500) : last.start + 600);
        }
        wl.push_back(en.words);
    }
    if (texts.empty()) return std::nullopt;
    return Lyrics::build(times, texts, true, std::move(source), durationMs, anyWords ? &wl : nullptr);
}

std::optional<Lyrics> parseYrc(std::string_view yrc, int64_t durationMs) {
    static std::regex const lineRe(R"(^\[(\d+),(\d+)\](.*)$)");
    static std::regex const wordRe(R"(\((\d+),(\d+),\d+\)([^(]*))");
    std::vector<Timing>            times;
    std::vector<std::string>       texts;
    std::vector<std::vector<Word>> words;
    for (auto const& rawLine : text::splitLines(yrc)) {
        std::string raw = text::trimmed(rawLine);
        std::smatch m;
        if (!std::regex_match(raw, m, lineRe)) continue;
        int64_t           ls = std::stoll(m[1].str()), ld = std::stoll(m[2].str());
        std::string       rest = m[3].str();
        std::vector<Word> lw;
        std::string       textAcc, cur;
        int64_t           cs = -1, ce = -1;
        for (auto it = std::sregex_iterator(rest.begin(), rest.end(), wordRe); it != std::sregex_iterator(); ++it) {
            int64_t     ws = std::stoll((*it)[1].str()), wd = std::stoll((*it)[2].str());
            std::string piece = (*it)[3].str();
            if (cs < 0) cs = ws;
            cur += piece;
            ce = ws + wd;
            bool cjk = false;
            for (uint32_t c : text::codepoints(piece))
                if (text::isCjkOrHangul(c)) { cjk = true; break; }
            if (text::endsWith(piece, " ") || cjk) {
                std::string t = text::trimmed(cur);
                if (!t.empty()) lw.push_back(Word::of(t, cs, ce));
                cur.clear();
                cs = -1;
            }
            textAcc += piece;
        }
        std::string t = text::trimmed(cur);
        if (!t.empty()) lw.push_back(Word::of(t, cs, ce));
        std::string line = text::trimmed(textAcc);
        if (!line.empty() && !lw.empty() && !hanThenColon(line)) {
            times.push_back({ls, ls + ld});
            texts.push_back(line);
            words.push_back(std::move(lw));
        }
    }
    if (texts.size() < 3) return std::nullopt;
    return Lyrics::build(times, texts, true, "netease words", durationMs, &words);
}

std::optional<Lyrics> parseRichsync(std::string_view richsyncJson, int64_t durationMs) {
    auto doc = json::parse(richsyncJson);
    if (!doc || !doc->isArray()) return std::nullopt;
    std::vector<Timing>            times;
    std::vector<std::string>       texts;
    std::vector<std::vector<Word>> words;
    for (auto const& o : doc->items()) {
        if (!o.isObject()) return std::nullopt;
        double                   ts = o["ts"].asNumber(), te = o["te"].asNumber();
        std::vector<std::string> wtext;
        std::vector<double>      starts;
        std::string              cur;
        double                   curStart = -1.0;
        for (auto const& p : o["l"].items()) {
            std::string const& c  = p["c"].asString();
            double             at = ts + p["o"].asNumber();
            if (text::isBlank(c)) {
                if (!cur.empty()) { wtext.push_back(cur); starts.push_back(curStart); cur.clear(); }
            } else {
                if (cur.empty()) curStart = at;
                cur += text::trim(c);
                if (text::endsWith(c, " ")) { wtext.push_back(cur); starts.push_back(curStart); cur.clear(); }
            }
        }
        if (!cur.empty()) { wtext.push_back(cur); starts.push_back(curStart); }
        if (wtext.empty()) continue;
        std::vector<Word> ws;
        for (size_t i = 0; i < wtext.size(); ++i) {
            int64_t s0 = jround(starts[i] * 1000.0);
            int64_t e0 = i + 1 < wtext.size() ? jround(starts[i + 1] * 1000.0) : jround(te * 1000.0);
            ws.push_back(Word::of(wtext[i], s0, std::max(s0 + 60, e0)));
        }
        std::string joined = text::join(wtext, " ");
        std::string txt    = o.has("x") ? text::trimmed(o["x"].str()) : joined;
        times.push_back({jround(ts * 1000.0), jround(te * 1000.0)});
        texts.push_back(txt.empty() ? joined : txt);
        words.push_back(std::move(ws));
    }
    if (texts.size() < 3) return std::nullopt;
    return Lyrics::build(times, texts, true, "Musixmatch", durationMs, &words);
}

std::optional<Lyrics> spread(std::string_view plain, int64_t durationMs, std::string source) {
    std::vector<std::string> lines;
    for (auto const& s : text::splitLines(plain)) {
        std::string t = text::trimmed(s);
        if (!t.empty() && t[0] != '[') lines.push_back(t);
    }
    if (lines.empty()) return std::nullopt;
    int64_t             start = 9000;
    int64_t             end   = durationMs > 30000 ? durationMs - 6000 : start + int64_t(lines.size()) * 3600;
    double              step  = std::max(1800.0, double(end - start) / double(lines.size()));
    std::vector<Timing> times;
    for (size_t i = 0; i < lines.size(); ++i) times.push_back({start + int64_t(double(i) * step), -1});
    return Lyrics::build(times, lines, false, std::move(source), durationMs);
}

Lyrics scaled(Lyrics const& l, double k, int64_t durationMs) {
    std::vector<Timing>            times;
    std::vector<std::string>       texts;
    std::vector<std::vector<Word>> words;
    for (auto const& line : l.lines) {
        times.push_back({jround(double(line.start) * k), jround(double(line.end) * k)});
        texts.push_back(line.text);
        if (l.wordSynced) {
            std::vector<Word> w;
            for (auto const& x : line.words) w.push_back(Word::of(x.text, jround(double(x.start) * k), jround(double(x.end) * k)));
            words.push_back(std::move(w));
        }
    }
    return Lyrics::build(times, texts, true, l.source + " (stretched)", durationMs, l.wordSynced ? &words : nullptr);
}

bool sameScript(std::string_view artist, std::string_view title, Lyrics const& l) {
    std::string t  = std::string(artist) + " " + std::string(title);
    int         tl = 0, tn = 0;
    for (uint32_t c : text::codepoints(t))
        if (text::isLetter(c)) (text::isLatin(c) ? tl : tn)++;
    if (tl == 0 || tn > tl) return true;
    int ll = 0, ln = 0;
    for (auto const& line : l.lines)
        for (uint32_t c : text::codepoints(line.text))
            if (text::isLetter(c)) (text::isLatin(c) ? ll : ln)++;
    return ll + ln == 0 || double(ln) <= double(ll + ln) * 0.5;
}

std::string show(std::string_view s, bool allCaps) { return allCaps ? text::toUpper(s) : std::string(s); }

} // namespace bf::lyrics
