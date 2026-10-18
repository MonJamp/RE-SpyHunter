#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace spyhunter::runtime
{
class PackedTexture
{
public:
    ~PackedTexture();
    PackedTexture() = default;
    PackedTexture(const PackedTexture&) = delete;
    PackedTexture& operator=(const PackedTexture&) = delete;

    bool load(const std::filesystem::path& archivePath, std::string_view memberName,
              SDL_Renderer* renderer, std::string& error);
    void unload();
    void draw(SDL_Renderer* renderer, float x, float y, float width = 0, float height = 0) const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
}
