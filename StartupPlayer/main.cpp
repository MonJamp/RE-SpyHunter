#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../Configuration/SpyHunterConfig.h"
#include "../GameRuntime/CutscenePlayer.h"

#include <filesystem>

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
    SDL_Log("Configuration: %s", configPath.string().c_str());
    SDL_Log("Playing intro movie: %s", introMoviePath.string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
        "SpyHunter - Startup Player - M_LOGO.PSS", 960, 720,
        SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const spyhunter::runtime::CutsceneOptions options{
        introMoviePath,
        configPath.parent_path() / "local-assets",
        true};
    const auto playback = spyhunter::runtime::playCutscene(renderer, options);
    if (!playback.error.empty()) SDL_Log("Cutscene playback failed: %s", playback.error.c_str());

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return playback.result == spyhunter::runtime::CutsceneResult::failed ? 1 : 0;
}
