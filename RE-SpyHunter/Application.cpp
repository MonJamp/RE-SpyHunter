#include "Application.h"
#include "FrontendMenu.h"
#include "IntroSequence.h"
#include "../Configuration/SpyHunterConfig.h"
#include "../GameRuntime/GamepadSet.h"
#include <memory>

namespace spyhunter::game
{
namespace
{
struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } };
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

int showError(const char* title, const std::string& error, SDL_Window* window = nullptr)
{
    SDL_Log("%s: %s", title, error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, error.c_str(), window);
    return 1;
}
}

int runApplication(int argc, char** argv)
{
    const auto configPath = argc > 1 ? std::filesystem::path(argv[1])
        : configuration::findConfigFile(std::filesystem::current_path());
    const auto result = configuration::load(configPath);
    if (!result.config) return showError("RE::SpyHunter configuration", result.error);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        return showError("RE::SpyHunter", SDL_GetError());
    // Reverse destruction order: intro/audio/controllers, renderer, window, SDL.
    const SdlLifetime sdl;
    Window window(SDL_CreateWindow("RE::SpyHunter - Intro", 1280, 896, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    Renderer renderer(window ? SDL_CreateRenderer(window.get(), nullptr) : nullptr, SDL_DestroyRenderer);
    if (!window || !renderer) return showError("RE::SpyHunter", SDL_GetError(), window.get());
    SDL_SetRenderLogicalPresentation(renderer.get(), IntroSequence::logicalWidth, IntroSequence::logicalHeight,
                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);

    runtime::GamepadSet gamepads;
    gamepads.discover();
    IntroSequence intro(renderer.get(), result.config->extractedBinariesPath, result.config->autosaveEnabled);
    std::string error;
    if (!intro.load(error)) return showError("RE::SpyHunter intro assets", error, window.get());

    std::unique_ptr<FrontendMenu> frontend;
    bool displayedPreMenuLoading = false;
    auto previous = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE)) running = false;
            gamepads.handleEvent(event);
            if (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN || gamepads.contains(event.gbutton.which)) {
                if (frontend) frontend->handleEvent(event);
                else intro.handleEvent(event);
            }
        }
        if (!running) break;
        const auto now = SDL_GetTicksNS();
        const double delta = static_cast<double>(now - previous) / 1'000'000'000.0;
        previous = now;
        if (frontend) {
            frontend->render();
        } else {
            if (!intro.update(delta, error)) return showError("RE::SpyHunter movie", error, window.get());
            intro.render();
            if (intro.readyForMenu()) {
                if (displayedPreMenuLoading) {
                    auto menu = std::make_unique<FrontendMenu>(renderer.get(), result.config->extractedBinariesPath,
                                                               configPath.parent_path() / "local-assets" / "profiles.txt");
                    if (!menu->load(error)) return showError("RE::SpyHunter frontend assets", error, window.get());
                    frontend = std::move(menu);
                    SDL_SetWindowTitle(window.get(), "RE::SpyHunter - Frontend");
                } else {
                    displayedPreMenuLoading = true;
                }
            }
        }
        SDL_RenderPresent(renderer.get());
        SDL_Delay(8);
    }
    return 0;
}
}
