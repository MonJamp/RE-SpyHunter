#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace spyhunter::runtime
{
class VkfTextureFont
{
public:
    VkfTextureFont() = default;
    ~VkfTextureFont();
    VkfTextureFont(const VkfTextureFont&) = delete;
    VkfTextureFont& operator=(const VkfTextureFont&) = delete;

    bool loadFromPackedArchive(const std::filesystem::path& archivePath,
                               std::string_view memberName, SDL_Renderer* renderer, std::string& error);
    bool loadFromPackedArchive(const std::filesystem::path& archivePath, std::string_view memberName,
                               float glyphScale, SDL_Renderer* renderer, std::string& error);
    bool loadFromBytes(const std::vector<std::uint8_t>& bytes, SDL_Renderer* renderer, std::string& error);
    void unload();
    void drawText(SDL_Renderer* renderer, std::string_view text, float x, float yFromBottom,
                  float canvasHeight, float scale = 1.0f,
                  SDL_Color color = {255, 255, 255, 255}) const;
    void drawTextScaled(SDL_Renderer* renderer, std::string_view text, float x, float yFromBottom,
                        float canvasHeight, float scaleX, float scaleY,
                        SDL_Color color = {255, 255, 255, 255}) const;
    float textAdvance(std::string_view text, float scale = 1.0f) const;
    bool loaded() const { return texture_ != nullptr; }

private:
    struct Glyph
    {
        SDL_FRect source{};
        float width = 0;
        float height = 0;
        float originX = 0;
        float originY = 0;
        float advanceX = 0;
        float advanceY = 0;
        bool present = false;
    };

    SDL_Texture* texture_ = nullptr;
    std::array<Glyph, 128> glyphs_{};
};
}
