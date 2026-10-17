#include "Gui.h"
#include "../Configuration/SpyHunterLog.h"

#include <algorithm>

namespace spyhunter::gui
{
bool configureRenderer(SDL_Renderer* renderer, const float scale)
{
    const bool configured = SDL_SetRenderScale(renderer, scale, scale);
    logging::write("GUI", configured ? logging::Level::debug : logging::Level::error,
                   configured ? "renderer_scale=" + std::to_string(scale) :
                                "renderer_scale_failed=" + std::string(SDL_GetError()));
    return configured;
}

void panel(SDL_Renderer* renderer, const SDL_FRect& rectangle, const SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rectangle);
}

void text(SDL_Renderer* renderer, const float x, const float y, const std::string& value)
{
    SDL_RenderDebugText(renderer, x, y, value.c_str());
}

std::string shorten(const std::string& value, const size_t maximumCharacters)
{
    if (value.size() <= maximumCharacters) return value;
    if (maximumCharacters < 4) return value.substr(0, maximumCharacters);
    return value.substr(0, maximumCharacters - 3) + "...";
}

SDL_FRect fittedRect(const SDL_FRect& viewport, const float contentWidth, const float contentHeight)
{
    if (contentWidth <= 0.0f || contentHeight <= 0.0f || viewport.w <= 0.0f || viewport.h <= 0.0f) return viewport;
    const float scale = (std::min)(viewport.w / contentWidth, viewport.h / contentHeight);
    const float width = contentWidth * scale;
    const float height = contentHeight * scale;
    return {viewport.x + (viewport.w - width) * 0.5f, viewport.y + (viewport.h - height) * 0.5f, width, height};
}
}
