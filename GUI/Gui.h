#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace spyhunter::gui
{
constexpr int defaultWindowWidth = 2560;
constexpr int defaultWindowHeight = 1440;
constexpr float defaultUiScale = 2.0f;

struct BrowserLayout
{
    float width = 340.0f;
    float top = 88.0f;
    float rowHeight = 22.0f;
};

bool configureRenderer(SDL_Renderer* renderer, float scale = defaultUiScale);
void panel(SDL_Renderer* renderer, const SDL_FRect& rectangle, SDL_Color color);
void text(SDL_Renderer* renderer, float x, float y, const std::string& value);
std::string shorten(const std::string& value, size_t maximumCharacters);
SDL_FRect fittedRect(const SDL_FRect& viewport, float contentWidth, float contentHeight);
}
