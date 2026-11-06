// LyricsForge - [LyricsForge] Sky mood: the sky a music video director would pick for this song.
// Original code for Bedrock Forge.
//
// Random used to pick any sky. Now, with Random on, the song decides: its shape (Song Memory: how much of it is loud,
// how many drops), its cover colour (warm / cool / dark / pastel) and its words (love, night, rain, fire, sun...) give
// it a mood, and each mood has a short list of skies that fit, ordered calm -> intense. Quiet parts get the calm end,
// drops climb to the intense end, so one song plays like a little arc. The lists lean on the realistic skies; the
// skies that look like effects are left out of the automatic picks (you can still choose them yourself).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bf::lyrics {

struct Lyrics;
class SongMemory;

enum class SkyMood : uint8_t { Bright, Hype, Dreamy, Melancholy, Dark, Night };
char const* moodName(SkyMood m);

struct MoodIn {
    float    highShare = -1;  // share of the song in loud sections, 0..1 (-1 = unknown)
    float    calmShare = -1;  // share in calm sections
    float    dropsPerMin = 0; // drops a minute (Song Memory)
    float    liveEnergy = -1; // the energy heard so far when there's no memory yet, 0..1 (-1 = unknown)
    uint32_t cover = 0;       // 0xRRGGBB of the cover's vivid colour (0 = none)
    std::vector<std::string_view> words; // lyric lines (any case)
};
struct MoodScore {
    SkyMood mood = SkyMood::Bright;
    float   score[6] = {};
};

/// the song's mood (deterministic for the same input)
MoodScore moodOf(MoodIn const& in);
/// helper: MoodIn from a Song Memory (may be empty / null) and lyrics (may be null)
MoodIn moodInputs(SongMemory const* mem, Lyrics const* lyrics, uint32_t cover, float liveEnergy);

/// the skies that fit a mood, calm -> intense
std::vector<std::string_view> const& moodSkies(SkyMood m);
/// the skies Random may pick from when it isn't fitting the song (the curated set: no effect-looking skies)
std::vector<std::string_view> const& curatedSkies();
bool isCurated(std::string_view id);

/// pick the next sky for the mood: intensity 0..1 (calm part .. big drop) chooses where on the calm -> intense list,
/// avoiding `recent` (the last few shown) and the one showing now; rng varies the pick inside the window
std::string pickMoodSky(SkyMood m, float intensity, std::vector<std::string> const& recent, std::string_view current, uint32_t rng);

/// [LyricsForge] the lyric fonts that fit a song, from the whole built-in library (unique but readable; no gimmick
/// fonts): a bright vivid pink cover -> its playful set (Shrikhand, Fredoka, Lobster...), else by mood. Empty = none.
std::vector<std::string_view> const& songFonts(SkyMood m, bool haveMood, uint32_t cover);
/// one of songFonts() at random (rng), not `avoid` (the last song's) when there is a choice; "" = none
std::string songFontFor(SkyMood m, bool haveMood, uint32_t cover, uint32_t rng, std::string_view avoid);

/// [LyricsForge] the curated skies whose palette shares the cover's colour (a pink cover -> the pink skies), closest
/// hue first; empty when the cover is grey / dark / missing (0). Pure, uses the built-in preset colours.
std::vector<std::string> coverSkies(uint32_t cover);
/// a saturated, bright cover (hot pink, sunny orange): it decides the sky's colour on every pick
bool coverIsVividBright(uint32_t cover);
/// one of coverSkies() that wasn't shown lately and isn't on now ("" = none fits)
std::string pickCoverSky(uint32_t cover, std::vector<std::string> const& recent, std::string_view current, uint32_t rng);

} // namespace bf::lyrics
