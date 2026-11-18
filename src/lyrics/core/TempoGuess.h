// LyricsForge - tempo from the lyrics: when the PC can't hear the song (Apple Music's audio is hidden from capture),
// the backup groove locks to the song's beat by reading when the words are sung - sung syllables sit on the beat
// grid, so a comb over the word start times finds the period and where the beats fall.
#pragma once
#include <cstdint>

namespace bf::lyrics {
struct Lyrics;

struct TempoGuess {
    double bpm = 0;        // 0 = no idea
    double phaseMs = 0;    // a beat falls at phaseMs + k * 60000 / bpm
    float  confidence = 0; // 0..1 (how tightly the words sit on that grid)
};

/// From real word timing (or line starts if that's all there is); needs ~16 onsets. Searches 70..160 BPM.
TempoGuess guessTempo(Lyrics const& ly);
} // namespace bf::lyrics
