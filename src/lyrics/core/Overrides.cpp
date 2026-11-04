// LyricsForge - [BF] local lyric overrides. Original code for Bedrock Forge; see Overrides.h.
#include "lyrics/core/Overrides.h"

#include <cstdio>

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
        if (l.synced && !ln.words.empty()) {
            for (size_t i = 0; i < ln.words.size(); ++i) {
                if (i) s += ' ';
                s += "<" + stamp(ln.words[i].start) + ">" + ln.words[i].text;
            }
        } else
            s += ln.text;
        s += '\n';
    }
    return s;
}

std::string emptyOverrideLrc(std::string_view artist, std::string_view title) {
    return header(artist, title) + "[00:00.00]First line here\n[00:05.00]Second line here\n";
}

std::optional<Lyrics> parseOverride(std::string_view text, int64_t durationMs) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3); // Notepad's UTF-8 BOM
    return parseLrc(text, durationMs, "override");
}

} // namespace bf::lyrics
