// FiguraBridge - runtime ("dynamic") textures: make a Bedrock texture path show RGBA pixels we produce at runtime.
// Implementation: src/figura/win/DynTex_win.cpp (mce::TextureGroup::updateTextureInPlace / uploadTexture).
//
// Thread: call dynTexUpload / dynTexUnload / dynTexInfo on the CLIENT thread (the thread that runs ticks, HUD frames
// and LL's ClientThreadExecutor - FiguraBridge's HUD-frame avatar code already runs there). From any other thread use
// dynTexUploadAsync, which copies the pixels and posts the upload to the client thread.
//
// How a path picks up the pixels:
//   * InPlace  - the path is already a loaded, non-missing texture of the SAME size (e.g. the PNG the compiled pack
//                ships at "textures/figura/<slug>/t8"). Its GPU texture is overwritten in place, so every cached
//                TexturePtr (actor render controllers resolve theirs when the entity definition loads) shows the new
//                pixels on the next frame. This is the path to use for texture:update() on avatar textures.
//   * Uploaded - the path was not loaded, was the missing texture, or the size changed: a new texture is registered at
//                the path. New lookups see it; a TexturePtr cached earlier (e.g. an actor definition that resolved the
//                path while it was missing) can keep the old one until resources reload. So for CUSTOM / newTexture
//                slots, have the pack ship a placeholder PNG of the right size at the path, then update in place.
// A resource reload (pack change, unloadAllTextures) drops Uploaded textures; just upload again.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class IClientInstance;

namespace bf::figura::win {

enum class DynTexResult : int {
    Failed   = 0, // bad args, no TextureGroup, or the game threw
    InPlace  = 1, // existing texture overwritten (cached TexturePtrs see it)
    Uploaded = 2, // texture (re)registered at the path
};

// rgba: w*h*4 bytes, row-major, top row first, straight (non-premultiplied) alpha, sRGB - the layout of a decoded PNG.
// resourcePath: pack-relative, no extension, forward slashes, e.g. "textures/figura/catmaid/t8".
DynTexResult dynTexUploadEx(IClientInstance& ci, std::string const& resourcePath, int w, int h, uint8_t const* rgba);
inline bool  dynTexUpload(IClientInstance& ci, std::string const& resourcePath, int w, int h, uint8_t const* rgba) {
    return dynTexUploadEx(ci, resourcePath, w, h, rgba) != DynTexResult::Failed;
}

// Any thread: copies rgba, runs dynTexUploadEx on the client thread (ll::service::getClientInstance()).
void dynTexUploadAsync(std::string resourcePath, int w, int h, std::vector<uint8_t> rgba);

// Client thread. Removes the texture registered at the path (the game falls back to normal loading). true if removed.
bool dynTexUnload(IClientInstance& ci, std::string const& resourcePath);

// Client thread. Size of the texture currently loaded at the path; false if not loaded / missing texture.
bool dynTexInfo(IClientInstance& ci, std::string const& resourcePath, int& w, int& h);

} // namespace bf::figura::win
