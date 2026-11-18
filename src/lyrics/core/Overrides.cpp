// LyricsForge - [BF] local lyric overrides. Original code for Bedrock Forge; see Overrides.h.
#include "lyrics/core/Overrides.h"

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <map>
#include <sstream>

namespace bf::lyrics {

namespace {
std::string stamp(int64_t ms) {
    if (ms < 0) ms = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d.%02d", int(ms / 60000), int(ms / 1000 % 60), int(ms / 10 % 100));
    return b;
}
std::string header(std::string_view artist, std::string_view title) {
    std::string h;
    h += "[ar:" + std::string(artist) + "]\n";
    h += "[ti:" + std::string(title) + "]\n";
    h += "[by:LyricsForge override - save this file and the game picks it up]\n";
    h += "[re:[mm:ss.xx] = when a line starts, <mm:ss.xx> before a word = when that word is sung (optional)]\n";
    h += "[re:duets - start a line with v2: for the second singer | translations - a line [same time]tr: text]\n";
    h += "[re:sections - a line [time]@build, @drop, @chorus or @calm makes the Director hit exactly there]\n";
    return h;
}
} // namespace

std::string overrideFileName(std::string_view artist, std::string_view title) {
    std::string raw = std::string(artist) + " - " + std::string(title);
    std::string o;
    for (unsigned char ch : raw) {
        if (ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
            o += '_';
        else
            o += char(ch);
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    if (o.size() > 140) { // don't cut a UTF-8 sequence in half
        size_t n = 140;
        while (n > 0 && (static_cast<unsigned char>(o[n]) & 0xC0) == 0x80) --n;
        o.resize(n);
    }
    if (o.empty() || o == "-") o = "unknown";
    return o + ".lrc";
}

std::string toOverrideLrc(Lyrics const& l, std::string_view artist, std::string_view title) {
    std::string s = header(artist, title);
    for (auto const& ln : l.lines) {
        if (ln.index < 0) continue;
        s += "[" + stamp(ln.start) + "]";
        if (ln.voice == 1) s += "v2: ";
        if (l.synced && !ln.words.empty()) {
            for (size_t i = 0; i < ln.words.size(); ++i) {
                if (i) s += ' ';
                s += "<" + stamp(ln.words[i].start) + ">" + ln.words[i].text;
            }
        } else
            s += ln.text;
        s += '\n';
        if (!ln.translation.empty()) s += "[" + stamp(ln.start) + "]tr: " + ln.translation + "\n";
    }
    for (auto const& [t, k] : l.marks) // [BF] manual sections
        s += "[" + stamp(t) + "]@" + (k == 'b' ? "build" : k == 'd' ? "drop" : k == 'c' ? "chorus" : "calm") + "\n";
    return s;
}

Lyrics retimeLines(Lyrics const& l, std::vector<std::pair<int, int64_t>> const& taps) {
    Lyrics out = l;
    if (taps.empty()) return out;
    std::map<int, int64_t> shift; // line index -> ms shift
    for (auto const& [idx, t] : taps)
        for (auto const& ln : l.lines)
            if (ln.index == idx) { shift[idx] = t - ln.start; break; }
    if (shift.empty()) return out;
    for (auto& ln : out.lines) {
        if (ln.index < 0) continue;
        int64_t d;
        auto hi = shift.lower_bound(ln.index);
        if (hi != shift.end() && hi->first == ln.index) d = hi->second;
        else if (hi == shift.begin()) d = hi->second;
        else if (hi == shift.end()) d = std::prev(hi)->second;
        else {
            auto lo = std::prev(hi);
            double const k = double(ln.index - lo->first) / double(hi->first - lo->first);
            d = int64_t(double(lo->second) + (double(hi->second) - double(lo->second)) * k);
        }
        ln.start += d; ln.end += d;
        for (auto& w : ln.words) { w.start += d; w.end += d; w.origStart += d; w.origEnd += d; }
        if (ln.start < 0) { ln.start = 0; }
    }
    // keep the lines in order even if a tap made two of them cross
    for (size_t i = 1; i < out.lines.size(); ++i)
        if (out.lines[i].start < out.lines[i - 1].start) out.lines[i].start = out.lines[i - 1].start + 1;
    for (auto& ln : out.lines) ln.end = std::max(ln.end, ln.start + 1);
    return out;
}

std::string emptyOverrideLrc(std::string_view artist, std::string_view title) {
    return header(artist, title) + "[00:00.00]First line here\n[00:05.00]Second line here\n";
}

namespace {
// "[01:02.50]" at the start of s -> ms (-1 = none); `rest` = what follows the last leading tag
int64_t leadTime(std::string_view s, std::string_view& rest) {
    int64_t t = -1;
    while (s.size() > 2 && s[0] == '[') {
        size_t const e = s.find(']');
        if (e == std::string_view::npos) break;
        std::string_view tag = s.substr(1, e - 1);
        int mm = 0, ss = 0, cs = 0, n = 0;
        if (std::sscanf(std::string(tag).c_str(), "%d:%d.%d%n", &mm, &ss, &cs, &n) >= 2) t = (mm * 60 + ss) * 1000 + cs * 10;
        else break;
        s.remove_prefix(e + 1);
    }
    rest = s;
    return t;
}
bool voicePrefix(std::string& s) { // "v2:" / "V2:" (also "2:") marks the second singer
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    auto pre = [&](std::string_view p) {
        if (s.size() - i < p.size()) return false;
        for (size_t k = 0; k < p.size(); ++k)
            if (std::tolower(static_cast<unsigned char>(s[i + k])) != p[k]) return false;
        s.erase(0, i + p.size());
        while (!s.empty() && s[0] == ' ') s.erase(s.begin());
        return true;
    };
    return pre("v2:") || pre("2:");
}
} // namespace

std::optional<Lyrics> parseOverride(std::string_view text, int64_t durationMs) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3); // Notepad's UTF-8 BOM
    // "[mm:ss.xx]tr: ..." lines are translations of the line with the same time; they don't become lyric lines
    std::map<int64_t, std::string> tr;
    std::vector<std::pair<int64_t, char>> marks;
    std::string                    kept;
    std::istringstream             in{std::string(text)};
    for (std::string ln; std::getline(in, ln);) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string_view rest;
        int64_t const    t = leadTime(ln, rest);
        size_t           i = 0;
        while (i < rest.size() && rest[i] == ' ') ++i;
        if (t >= 0 && rest.size() > i && rest[i] == '@') { // [BF] manual section marks: @build @drop @chorus @calm
            std::string k(rest.substr(i + 1));
            for (auto& ch : k) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            char const kind = k.rfind("build", 0) == 0 ? 'b' : k.rfind("drop", 0) == 0 ? 'd' : k.rfind("chorus", 0) == 0 ? 'c'
                            : k.rfind("calm", 0) == 0 ? 'q' : 0;
            if (kind) marks.push_back({t, kind});
            continue;
        }
        if (t >= 0 && rest.size() >= i + 3 && (rest.substr(i, 3) == "tr:" || rest.substr(i, 3) == "TR:")) {
            std::string v(rest.substr(i + 3));
            while (!v.empty() && v[0] == ' ') v.erase(v.begin());
            tr[t] = v;
            continue;
        }
        kept += ln;
        kept += '\n';
    }
    auto l = parseLrc(kept, durationMs, "override");
    if (!l) return l;
    std::sort(marks.begin(), marks.end());
    l->marks = std::move(marks);
    for (auto& line : l->lines) {
        if (line.index < 0) continue;
        if (voicePrefix(line.text)) {
            line.voice = 1;
            if (!line.words.empty()) {
                voicePrefix(line.words.front().text);
                if (line.words.front().text.empty()) line.words.erase(line.words.begin());
            }
        }
        if (auto it = tr.lower_bound(line.start - 30); it != tr.end() && it->first <= line.start + 30) line.translation = it->second;
    }
    return l;
}

} // namespace bf::lyrics
