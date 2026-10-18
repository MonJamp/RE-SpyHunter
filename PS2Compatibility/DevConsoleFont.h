#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>
#include <string>

namespace spyhunter::ps2 {

// Host reconstruction of the observed Sony developer-console glyph drawing.
// The caller supplies sceFont8 bytes and display mapping; no game-specific ELF offsets.
class DevConsoleFont {
public:
    DevConsoleFont() = default;
    DevConsoleFont(const DevConsoleFont&) = delete;
    DevConsoleFont& operator=(const DevConsoleFont&) = delete;
    bool load(SDL_Renderer* renderer, std::span<const std::uint8_t> font, std::string& error);
    void draw(SDL_Renderer* renderer, const char* text, float x, float y,
              float cellWidth, float cellHeight, Uint8 shade) const;
    void unload();
    ~DevConsoleFont() { unload(); }

private:
    SDL_Texture* atlas_ = nullptr;
};

}
