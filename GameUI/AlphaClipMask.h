#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace spyhunter::ui
{
// Binary destination-alpha visibility, represented as nonoverlapping clip bands.
// Works on software and GPU renderers without custom blend/target support.
class AlphaClipMask
{
public:
    bool assign(int width, int height, std::span<const std::uint8_t> visible)
    {
        bands_.clear();
        if (width <= 0 || height <= 0 || visible.size() != std::size_t(width) * height) return false;
        std::vector<std::size_t> previous;
        for (int y = 0; y < height; ++y) {
            std::vector<std::size_t> current;
            for (int x = 0; x < width;) {
                if (!visible[std::size_t(y) * width + x]) { ++x; continue; }
                const int left = x;
                while (x < width && visible[std::size_t(y) * width + x]) ++x;
                std::size_t index = bands_.size();
                for (auto candidate : previous) {
                    auto& band = bands_[candidate];
                    if (band.x == left && band.w == x - left && band.y + band.h == y) {
                        index = candidate; ++band.h; break;
                    }
                }
                if (index == bands_.size()) bands_.push_back({left, y, x - left, 1});
                current.push_back(index);
            }
            previous = std::move(current);
        }
        return true;
    }

    template<class Draw> void draw(SDL_Renderer* renderer, const SDL_Rect& bounds, Draw&& drawContent) const
    {
        if (!renderer || bands_.empty() || bounds.w <= 0 || bounds.h <= 0) return;
        const bool wasClipped = SDL_RenderClipEnabled(renderer);
        SDL_Rect saved{};
        if (wasClipped && !SDL_GetRenderClipRect(renderer, &saved)) return;
        SDL_Rect available = bounds;
        if (wasClipped && !SDL_GetRectIntersection(&bounds, &saved, &available)) return;
        struct RestoreClip
        {
            SDL_Renderer* renderer;
            const SDL_Rect* previous;
            ~RestoreClip() { SDL_SetRenderClipRect(renderer, previous); }
        } restore{renderer, wasClipped ? &saved : nullptr};
        for (const auto& band : bands_) {
            SDL_Rect intersection;
            if (!SDL_GetRectIntersection(&band, &available, &intersection)) continue;
            SDL_SetRenderClipRect(renderer, &intersection);
            std::invoke(drawContent);
        }
    }

private:
    std::vector<SDL_Rect> bands_;
};
}
