#include "MenuDrawing.h"

#include <algorithm>
#include <cmath>

namespace spyhunter::ui
{
void drawFilledPanel(SDL_Renderer* renderer, const SDL_FRect& bounds, const SDL_Color color)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &bounds);
}

void drawPillSelector(SDL_Renderer* renderer, const SDL_FRect& bounds, const SDL_Color color)
{
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const float diameter = std::min(bounds.h, bounds.w);
    const float radius = diameter * 0.5f;
    const SDL_FRect center{bounds.x + radius, bounds.y, bounds.w - diameter, bounds.h};
    SDL_RenderFillRect(renderer, &center);
    for (int row = 0; row < static_cast<int>(bounds.h); ++row) {
        const float dy = static_cast<float>(row) + 0.5f - radius;
        const float halfWidth = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        SDL_RenderLine(renderer, bounds.x + radius - halfWidth, bounds.y + static_cast<float>(row),
                       bounds.x + bounds.w - radius + halfWidth, bounds.y + static_cast<float>(row));
    }
}

void drawGradientPillSelector(SDL_Renderer* renderer, const SDL_FRect& bounds,
                              const SDL_Color top, const SDL_Color bottom)
{
    if (bounds.w <= 2.0f || bounds.h <= 2.0f) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const float radius = (std::min)(bounds.h, bounds.w) * 0.5f;
    for (int row = 0; row < static_cast<int>(bounds.h); ++row) {
        const float dy = static_cast<float>(row) + 0.5f - bounds.h * 0.5f;
        const float inset = radius - std::sqrt((std::max)(0.0f, radius * radius - dy * dy));
        const float amount = bounds.h <= 1.0f ? 0.0f : static_cast<float>(row) / (bounds.h - 1.0f);
        const auto blend = [amount](const std::uint8_t a, const std::uint8_t b) {
            return static_cast<std::uint8_t>(std::lround(a + (static_cast<float>(b) - a) * amount));
        };
        SDL_SetRenderDrawColor(renderer, blend(top.r, bottom.r), blend(top.g, bottom.g),
                               blend(top.b, bottom.b), blend(top.a, bottom.a));
        SDL_RenderLine(renderer, bounds.x + inset, bounds.y + static_cast<float>(row),
                       bounds.x + bounds.w - inset, bounds.y + static_cast<float>(row));
    }
}

void drawPillOutline(SDL_Renderer* renderer, const SDL_FRect& bounds, const SDL_Color color)
{
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const float radius = (std::min)(bounds.h, bounds.w) * 0.5f;
    const int rows = static_cast<int>(bounds.h);
    for (int row = 0; row < rows; ++row) {
        const float dy = static_cast<float>(row) + 0.5f - bounds.h * 0.5f;
        const float inset = radius - std::sqrt((std::max)(0.0f, radius * radius - dy * dy));
        const float left = bounds.x + inset;
        const float right = bounds.x + bounds.w - inset;
        const float y = bounds.y + static_cast<float>(row);
        SDL_RenderPoint(renderer, left, y);
        SDL_RenderPoint(renderer, right, y);
        if (row == 0 || row == rows - 1) SDL_RenderLine(renderer, left, y, right, y);
    }
}
}
