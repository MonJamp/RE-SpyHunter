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
}
