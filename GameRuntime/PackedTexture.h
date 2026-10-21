#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <istream>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "VkiImage.h"

namespace spyhunter::runtime
{
// Bounded packed-member decode for consumers that need pixels (for example masks).
std::optional<DecodedStill> loadPackedTexturePixels(const std::filesystem::path& archivePath,
    std::string_view memberName, std::string& error);

class PackedTexture
{
public:
    ~PackedTexture();
    PackedTexture() = default;
    PackedTexture(const PackedTexture&) = delete;
    PackedTexture& operator=(const PackedTexture&) = delete;

    bool load(const std::filesystem::path& archivePath, std::string_view memberName,
              SDL_Renderer* renderer, std::string& error);
    bool load(const std::filesystem::path& archivePath, std::string_view memberName,
              SDL_Renderer* renderer, std::string& error, SDL_ScaleMode scaleMode);
    bool load(std::istream& archiveStream, std::uint64_t memberOffset, std::uint64_t memberSize,
              SDL_Renderer* renderer, std::string& error, SDL_ScaleMode scaleMode = SDL_SCALEMODE_LINEAR);
    void unload();
    void draw(SDL_Renderer* renderer, float x, float y, float width = 0, float height = 0) const;
    void drawRounded(SDL_Renderer* renderer, const SDL_FRect& bounds) const;
    void drawTinted(SDL_Renderer* renderer, const SDL_FRect& bounds, SDL_Color color,
                    const SDL_FRect* source = nullptr) const;
    void drawTintedMirrored(SDL_Renderer* renderer, const SDL_FRect& bounds, SDL_Color color,
                            const SDL_FRect& source, bool horizontal, bool vertical) const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
}
