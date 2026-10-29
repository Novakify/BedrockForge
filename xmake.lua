-- Renderer Foundation - LeviLamina client mod for Minecraft Bedrock 1.26.51.01
-- A modernized fork of BetterRenderDragon (ddf8196 and contributors), AGPL-3.0. See LICENSE and NOTICE.md.
add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

add_requires("levilamina 26.51.x", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")
add_requires("imgui v1.91.8", {configs = {dx11 = true, dx12 = true, win32 = true}})

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("RendererFoundation")
    set_version("2.0.0")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modVersion = "2.0.0-alpha", modPlatform = "client"})
    add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
    set_exceptions("none")
    add_cxflags("/EHa", "/utf-8", "/W4")
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
    add_packages("levilamina", "imgui")
    add_syslinks("user32", "advapi32", "d3d11", "d3d12", "dxgi", "dxguid")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    add_includedirs("deps") -- GameInput v2 header, fetched by CI (see .github/workflows/build.yml)
