#include "Ps2DevConsoleFont.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace spyhunter::runtime {
namespace {
// SLUS_200.56: sceFont8 VA 0x004699C8, mapped to this file offset.
constexpr std::streamoff kFontOffset = 0x0036A9C8;
constexpr int kAtlasWidth = 128;
constexpr int kGlyphSize = 8;
}

bool Ps2DevConsoleFont::load(SDL_Renderer* renderer, const std::filesystem::path& elfPath, std::string& error)
{
    unload();
    std::ifstream file(elfPath, std::ios::binary);
    if (!file || !file.seekg(kFontOffset)) {
        error = "Cannot read sceFont8 from " + elfPath.string();
        return false;
    }
    std::array<std::uint8_t, 256 * kGlyphSize> font{};
    if (!file.read(reinterpret_cast<char*>(font.data()), font.size())) {
        error = "Truncated sceFont8 in " + elfPath.string();
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

void Ps2DevConsoleFont::draw(SDL_Renderer* renderer, const char* text, float x, float y,
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

void Ps2DevConsoleFont::unload()
{
    if (atlas_) SDL_DestroyTexture(atlas_);
    atlas_ = nullptr;
}
}
