#pragma once

#include <SDL3/SDL.h>

namespace spyhunter::ui
{
void drawFilledPanel(SDL_Renderer* renderer, const SDL_FRect& bounds, SDL_Color color);
void drawPillSelector(SDL_Renderer* renderer, const SDL_FRect& bounds, SDL_Color color);
void drawGradientPillSelector(SDL_Renderer* renderer, const SDL_FRect& bounds,
                              SDL_Color top, SDL_Color bottom);
void drawPillOutline(SDL_Renderer* renderer, const SDL_FRect& bounds, SDL_Color color);
}
