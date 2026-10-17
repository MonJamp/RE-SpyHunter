#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../Configuration/SpyHunterConfig.h"

#include <filesystem>
#include <string>

namespace
{
constexpr auto kIntroMovie = "MOVIES/M_LOGO.PSS";
}

int main(int argc, char* argv[])
{
    const std::filesystem::path configPath = argc > 1
        ? std::filesystem::path(argv[1])
        : spyhunter::configuration::findConfigFile(std::filesystem::current_path());

    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) {
        SDL_Log("Configuration error: %s", configResult.error.c_str());
        SDL_Log("Pass the path to SpyHunter.config as the first argument.");
        return 1;
    }

    const auto introMoviePath = configResult.config->extractedBinariesPath / kIntroMovie;
    if (!std::filesystem::is_regular_file(introMoviePath)) {
        SDL_Log("Intro movie was not found: %s", introMoviePath.string().c_str());
        return 1;
    }

    SDL_Log("Configuration: %s", configPath.string().c_str());
    SDL_Log("Initial playback asset: %s", introMoviePath.string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer(
        "SpyHunter - Startup Player - M_LOGO.PSS",
        960, 720,
        SDL_WINDOW_RESIZABLE,
        &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN &&
                event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 15, 25, 45, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
