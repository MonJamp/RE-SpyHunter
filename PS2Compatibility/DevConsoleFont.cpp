#include "DevConsoleFont.h"

#include <cstdint>
#include <vector>

namespace spyhunter::ps2 {
namespace {
constexpr int kAtlasWidth = 128;
constexpr int kGlyphSize = 8;
}

bool DevConsoleFont::load(SDL_Renderer* renderer, std::span<const std::uint8_t> font, std::string& error)
{
    unload();
    if (font.size() != 256 * kGlyphSize) {
        error = "sceFont8 requires exactly 2,048 bytes.";
        return false;
    }

    std::vector<std::uint32_t> pixels(kAtlasWidth * kAtlasWidth, 0);
    for (int glyph = 0; glyph < 256; ++glyph) {
        const int originX = (glyph % 16) * kGlyphSize;
        const int originY = (glyph / 16) * kGlyphSize;
        for (int row = 0; row < kGlyphSize; ++row) {
            const auto bits = font[glyph * kGlyphSize + row];
            for (int col = 0; col < kGlyphSize; ++col) {
                if (bits & (1u << col))
                    pixels[(originY + row) * kAtlasWidth + originX + col] = 0xffffffffu;
            }
        }
    }
    atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                               kAtlasWidth, kAtlasWidth);
    if (!atlas_ || !SDL_UpdateTexture(atlas_, nullptr, pixels.data(), kAtlasWidth * 4)) {
        error = SDL_GetError();
        unload();
        return false;
    }
    SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    return true;
}

void DevConsoleFont::draw(SDL_Renderer* renderer, const char* text, float x, float y,
                             float cellWidth, float cellHeight, Uint8 shade) const
{
    if (!atlas_ || !text) return;
    SDL_SetTextureColorMod(atlas_, shade, shade, shade);
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p; ++p, x += cellWidth) {
        const SDL_FRect source{static_cast<float>((*p % 16) * kGlyphSize),
                               static_cast<float>((*p / 16) * kGlyphSize), 8.0f, 8.0f};
        const SDL_FRect destination{x, y, cellWidth, cellHeight};
        SDL_RenderTexture(renderer, atlas_, &source, &destination);
    }
}

void DevConsoleFont::unload()
{
    if (atlas_) SDL_DestroyTexture(atlas_);
    atlas_ = nullptr;
}
}
