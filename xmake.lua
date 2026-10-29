add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Bedrock Forge targets the CLIENT build of LeviLamina only (for now).
-- Pinned to the 26.51 line = Minecraft Bedrock 1.26.51.01. Don't bump without
-- updating docs/COMPATIBILITY_MATRIX.md.
-- levibuildscript's modpacker reads target_type to fill "platform" in manifest.json;
-- without it the manifest says ${modPlatform} and LeviLamina refuses to load the mod.
option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

-- The game mod only builds for Windows; the LyricsFlow core unit tests (target LyricsCoreTests) also build on Linux.
if is_plat("windows") then
    add_requires("levilamina 26.51.x", {configs = {target_type = get_config("target_type")}})
    add_requires("levibuildscript")
end

if is_plat("windows") and not has_config("vs_runtime") then
    set_runtimes("MD")
end

if is_plat("windows") then
target("BedrockForgeCore")
    set_version("0.1.0")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modVersion = "0.2.0", modPlatform = "client"})
    add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
    set_exceptions("none") -- avoid conflicts with /EHa
    add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_cxflags(
        "/EHs",
        "-Wno-microsoft-cast",
        "-Wno-invalid-offsetof",
        "-Wno-c++2b-extensions",
        "-Wno-microsoft-include",
        "-Wno-overloaded-virtual",
        "-Wno-ignored-qualifiers",
        "-Wno-missing-field-initializers",
        "-Wno-potentially-evaluated-expression",
        "-Wno-pragma-system-header-outside-header",
        {tools = {"clang_cl"}}
    )
    set_toolchains("clang-cl")
    add_packages("levilamina")
    add_syslinks("user32", "winhttp", "windowsapp") -- winhttp + windowsapp: LyricsFlow port (lyric search, media session)
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
end

-- LyricsFlow port: unit tests for the pure core (src/lyrics/core). No game, no network (fixtures).
--   xmake build LyricsCoreTests && xmake run LyricsCoreTests
target("LyricsCoreTests")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    add_includedirs("src")
    add_files("src/lyrics/core/*.cpp", "tests/lyrics/LyricsTests.cpp")
    set_rundir("$(projectdir)")
    add_runenvs("LYRICS_FIXTURES", "tests/lyrics/fixtures")
    if is_plat("windows") then
        set_toolchains("clang-cl")
        add_cxflags("/EHsc", "/utf-8", {tools = {"clang_cl", "cl"}})
    else
        add_syslinks("pthread")
    end
target_end()
