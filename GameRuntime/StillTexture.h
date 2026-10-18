#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace spyhunter::runtime
{
class PckArchive;
struct DecodedStill;

// Owns a decoded still's GPU texture. Destroy before its renderer.
class StillTexture
{
public:
    bool load(SDL_Renderer* renderer, const std::filesystem::path& path, std::string& error);
    bool loadPacked(SDL_Renderer* renderer, const PckArchive& archive,
                    std::string_view suffix, std::string& error);
    bool upload(SDL_Renderer* renderer, const DecodedStill& still, std::string& error);
    void drawFit(SDL_Renderer* renderer, const SDL_FRect& viewport) const;
    bool loaded() const { return texture_ != nullptr; }

private:
    struct DeleteTexture { void operator()(SDL_Texture* value) const { SDL_DestroyTexture(value); } };
    std::unique_ptr<SDL_Texture, DeleteTexture> texture_;
};
}
