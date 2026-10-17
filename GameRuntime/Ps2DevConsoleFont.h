#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

namespace spyhunter::runtime {

// Boot text uses Sony's developer-console bitmap, separate from INTRO.PCK VKF fonts.
class Ps2DevConsoleFont {
public:
    bool load(SDL_Renderer* renderer, const std::filesystem::path& elfPath, std::string& error);
    void draw(SDL_Renderer* renderer, const char* text, float x, float y,
              float cellWidth, float cellHeight, Uint8 shade) const;
    void unload();
    ~Ps2DevConsoleFont() { unload(); }

private:
    SDL_Texture* atlas_ = nullptr;
};

}
