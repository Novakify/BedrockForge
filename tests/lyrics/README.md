# LyricsFlow port - core unit tests

`LyricsTests.cpp` tests everything in `src/lyrics/core/` without the game or the network:

- **Parsers vs the original.** `fixtures/golden_java.json` was produced by compiling LyricsFlow v2.0's *own* Java code
  (`Lyrics`, `TitleCleaner`, `Anim`, and `LyricsFetcher.parseLrc / parseYrc / enhancedWords / spread`, decompiled from
  `LyricsFlow_Update.jar` with Vineflower, Minecraft-only bits stubbed) and running it on the fixtures below. The C++ port
  must reproduce it exactly: line/word times, word splits, titles cleaned, search variants, `hash01`, easings.
- **Search without network.** `fixtures/*.json` are saved-shape responses for LRCLIB (`/api/get`, `/api/search`),
  NetEase (`search/get`, `song/lyric`, `song/lyric/v1` YRC) and Musixmatch desktop (`token.get`, `matcher.track.get`,
  `track.richsync.get`, a 401). A fake HTTP layer routes URLs to them; the full fallback chain is exercised
  (Musixmatch richsync -> NetEase YRC -> LRCLIB line sync, plus the sped-up "stretched" path).
- **Placement / physics** against a fake voxel world (ground at y=64, walls, slabs, steps).

Lyric text in the fixtures is made up (no real song lyrics in the repo).

## Run
```
xmake f -p linux -m debug && xmake build LyricsCoreTests && xmake run LyricsCoreTests      # Linux
xmake build LyricsCoreTests && xmake run LyricsCoreTests                                    # Windows (after the normal configure)
# or plain g++:
g++ -std=c++20 -Isrc src/lyrics/core/*.cpp tests/lyrics/LyricsTests.cpp -o lt -pthread && ./lt tests/lyrics/fixtures
```

## Regenerating golden_java.json
Only needed if fixtures change. On a machine with a JDK:
1. Decompile the v2.0 jar: `java -jar vineflower.jar <jar>/dev <out>` (do **not** commit the decompiled source).
2. Copy `Lyrics.java`, `TitleCleaner.java`, `Anim.java` into a scratch tree under their packages; stub
   `dev.lyricsflow.config.ConfigManager`/`LFConfig` (one field: `allCaps = false`).
3. Make `dev/lyricsflow/lyrics/FetchLite.java` holding `LyricsFetcher`'s `LRC`, `YRC_LINE`, `YRC_WORD`, `WORD_TAG`
   patterns and its `parseLrc`, `enhancedWords`, `parseYrc`, `spread` methods copied verbatim.
4. Add `tools/LyricsGolden/Golden.java`, compile everything, and run
   `java -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 -cp out Golden tests/lyrics/fixtures > tests/lyrics/fixtures/golden_java.json`.
