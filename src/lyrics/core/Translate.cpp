// LyricsForge - [LyricsForge] automatic translations. Original code for Bedrock Forge; see Translate.h.
#include "lyrics/core/Translate.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

#include "lyrics/core/Json.h"
#include "lyrics/core/Text.h"

namespace bf::lyrics::translate {

namespace {
bool nonLatinCp(uint32_t c) {
    return (c >= 0x0370 && c <= 0x03FF) ||  // Greek
           (c >= 0x0400 && c <= 0x052F) ||  // Cyrillic
           (c >= 0x0590 && c <= 0x06FF) ||  // Hebrew, Arabic
           (c >= 0x0900 && c <= 0x0DFF) ||  // Indic
           (c >= 0x0E00 && c <= 0x0E7F) ||  // Thai
           (c >= 0x1100 && c <= 0x11FF) ||  // Hangul Jamo
           (c >= 0x3040 && c <= 0x30FF) ||  // Hiragana, Katakana
           (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || // CJK
           (c >= 0xAC00 && c <= 0xD7AF);    // Hangul syllables
}

std::vector<std::string> wordsLower(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        unsigned char const u = static_cast<unsigned char>(ch);
        if (std::isalpha(u) || u == '\'' || u >= 0x80) cur.push_back(char(u < 0x80 ? std::tolower(u) : u));
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// share of a text's words that are everyday English (an English lyric scores ~0.4-0.6, Spanish / Portuguese ~0.05)
float englishShare(std::vector<std::string> const& ws) {
    static std::set<std::string> const en = {
        "the", "a", "an", "and", "or", "but", "i", "you", "he", "she", "it", "we", "they", "me", "my", "your", "our", "his",
        "her", "their", "is", "are", "was", "were", "be", "been", "am", "do", "don't", "did", "can", "can't", "will", "won't",
        "i'm", "you're", "it's", "that", "this", "what", "when", "where", "who", "why", "how", "all", "so", "if", "just",
        "now", "know", "love", "like", "get", "got", "go", "gonna", "wanna", "want", "need", "feel", "see", "say", "make",
        "take", "come", "baby", "oh", "yeah", "of", "to", "in", "on", "for", "with", "at", "up", "down", "out", "no", "not",
        "never", "ever", "one", "time", "way", "night", "day", "heart", "life", "too", "have", "has", "had", "there", "here",
        "from", "by", "about", "back", "let", "let's", "into", "over", "only", "be", "us", "them", "him", "much", "more"};
    if (ws.empty()) return 1.0f;
    int n = 0;
    for (auto const& w : ws) n += en.count(w) ? 1 : 0;
    return float(n) / float(ws.size());
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// worth showing under the original line?
bool keep(std::string const& orig, std::string const& tr) {
    if (tr.empty()) return false;
    auto const wo = wordsLower(orig), wt = wordsLower(tr);
    if (wo == wt) return false;                         // the same words (an English line, a name, "oh oh oh")
    if (nonLatin(orig)) return true;
    if (englishShare(wo) >= 0.25f) return false;         // already English: Google only reworded it
    std::set<std::string> so(wo.begin(), wo.end());
    int same = 0;
    for (auto const& w : wt) same += so.count(w) ? 1 : 0;
    return wt.empty() || float(same) / float(wt.size()) < 0.6f; // mostly new words = a real translation
}

std::vector<std::string> splitLines(std::string const& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find('\n', a);
        out.push_back(trim(std::string_view(s).substr(a, b == std::string::npos ? std::string::npos : b - a)));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

thread_local std::string tDiag; // what the last translateToEnglish saw (HTTP codes, line counts) - for the log

std::string request(fetch::HttpGet const& http, std::string const& text, int64_t deadline) {
    auto const r = http("https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=en&dt=t&q=" + fetch::urlEncode(text),
                        {}, deadline);
    tDiag += ' ' + std::to_string(r.code);
    if (r.code == 200 && parseGtx(r.body).empty()) tDiag += "(unreadable)";
    return r.code == 200 ? parseGtx(r.body) : std::string();
}
} // namespace

std::string lastDiag() { return tDiag; }

bool nonLatin(std::string_view line) {
    for (uint32_t c : text::codepoints(line)) if (nonLatinCp(c)) return true;
    return false;
}

bool needsTranslation(Lyrics const& l) {
    // [LyricsForge] per line, not per song: a cache with only some lines translated (the 9 s deadline ran out, or the
    // network dropped mid-song) still asks for the rest next time
    std::vector<std::string> latin;
    bool open = false, foreignOpen = false;
    for (auto const& ln : l.lines) {
        if (ln.index < 0 || trim(ln.text).empty()) continue;
        for (auto& w : wordsLower(ln.text)) latin.push_back(std::move(w));
        if (!ln.translation.empty()) continue; // a source / override / earlier run already translated it
        open = true;
        if (nonLatin(ln.text)) foreignOpen = true;
    }
    if (foreignOpen) return true;
    return open && latin.size() >= 12 && englishShare(latin) < 0.12f;
}

bool countsAsLetter(uint32_t c) {
    if (c <= 0x40 || c == 0x3000) return false;                          // spaces, digits, ASCII punctuation
    if (c >= 0x2000 && c <= 0x206F) return false;                        // general punctuation, ZWJ / ZWNJ
    if (c >= 0x2190 && c <= 0x2BFF) return false;                        // arrows, symbols, dingbats (the heart), shapes
    if (c >= 0xFE00 && c <= 0xFE0F) return false;                        // emoji variation selectors (invisible)
    if (c >= 0x1F000 && c <= 0x1FAFF) return false;                      // emoji
    if (c >= 0xE0000) return false;                                      // tags
    return true;
}

int fitToFonts(Lyrics& l, std::function<bool(uint32_t)> const& canDraw) {
    int changed = 0;
    std::vector<Line> keep;
    keep.reserve(l.lines.size());
    for (auto& ln : l.lines) {
        if (ln.index >= 0 && !ln.text.empty()) {
            int letters = 0, bad = 0;
            for (uint32_t c : text::codepoints(ln.text)) {
                if (!countsAsLetter(c)) continue; // spaces, digits, punctuation, emoji (skipped when drawn)
                ++letters;
                bad += canDraw(c) ? 0 : 1;
            }
            if (letters > 0 && bad * 4 >= letters) {
                ++changed;
                if (ln.translation.empty()) continue; // nothing we can show for it: leave the line out
                int64_t const end = ln.guessedEnd();
                ln.text = ln.translation;
                ln.translation.clear();
                ln.words = {Word::of(ln.text, ln.start, end)};
                ln.estimated = true;
            }
        }
        keep.push_back(std::move(ln));
    }
    l.lines = std::move(keep);
    if (changed) normalizeWords(l); // the English lines -> words
    return changed;
}

std::string parseGtx(std::string_view body) {
    auto doc = json::parse(body);
    if (!doc || !doc->isArray() || doc->items().empty() || !doc->items()[0].isArray()) return {};
    std::string out;
    for (auto const& seg : doc->items()[0].items())
        if (seg.isArray() && !seg.items().empty()) out += seg.items()[0].asString();
    return out;
}

int translateToEnglish(fetch::HttpGet const& http, Lyrics& l, int64_t deadlineMs) {
    tDiag.clear();
    // the song reads as English (a verse in another language): only the foreign lines go, not every English one
    std::vector<std::string> all;
    for (auto const& ln : l.lines) if (ln.index >= 0) for (auto& w : wordsLower(ln.text)) all.push_back(std::move(w));
    bool const englishSong = all.size() >= 12 && englishShare(all) >= 0.12f;
    std::vector<Line*> lines;
    for (auto& ln : l.lines) // only the lines still without English (a partial cache from an earlier run keeps its own)
        if (ln.index >= 0 && !trim(ln.text).empty() && ln.translation.empty() &&
            (!englishSong || nonLatin(ln.text) || englishShare(wordsLower(ln.text)) < 0.25f))
            lines.push_back(&ln);
    tDiag = std::to_string(lines.size()) + " sent, http";
    int got = 0;
    size_t i = 0;
    while (i < lines.size() && fetch::nowMs() < deadlineMs) {
        // one request per batch of lines, joined by newlines (gtx keeps them), kept short enough for a GET
        std::string text;
        size_t j = i, enc = 0;
        while (j < lines.size()) {
            size_t const add = fetch::urlEncode(lines[j]->text).size() + 3;
            if (j > i && enc + add > 1700) break;
            if (j > i) text += '\n';
            text += trim(lines[j]->text);
            enc += add;
            ++j;
        }
        auto parts = splitLines(request(http, text, deadlineMs));
        if (parts.size() != j - i) tDiag += "(" + std::to_string(parts.size()) + "/" + std::to_string(j - i) + " lines back)";
        if (parts.size() != j - i) { // the lines didn't come back one for one: ask for the foreign ones alone
            parts.assign(j - i, std::string());
            for (size_t k = i; k < j && fetch::nowMs() < deadlineMs; ++k)
                if (nonLatin(lines[k]->text) || englishShare(wordsLower(lines[k]->text)) < 0.25f) parts[k - i] = trim(request(http, trim(lines[k]->text), deadlineMs));
        }
        for (size_t k = i; k < j; ++k)
            if (keep(lines[k]->text, parts[k - i])) { lines[k]->translation = parts[k - i]; ++got; }
        i = j;
    }
    return got;
}

} // namespace bf::lyrics::translate
