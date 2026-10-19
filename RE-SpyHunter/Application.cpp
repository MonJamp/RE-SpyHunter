#include "Application.h"
#include "FrontendMenu.h"
#include "IntroSequence.h"
#include "../Configuration/SpyHunterConfig.h"
#include "../GameRuntime/GamepadSet.h"
#include "../ThirdParty/CLI11/CLI11.hpp"
#include <memory>
#include <optional>
#include <string_view>

namespace spyhunter::game
{
namespace
{
struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } };
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

std::optional<FrontendMenu::Screen> parseDebugStartScreen(const std::string_view name)
{
    using Screen = FrontendMenu::Screen;
    if (name == "settings") return Screen::settings;
    if (name == "controller") return Screen::controller;
    if (name == "extras") return Screen::extras;
    if (name == "music") return Screen::musicPlayer;
    if (name == "movies") return Screen::moviePlayer;
    if (name == "cheats") return Screen::cheatGrid;
    return std::nullopt;
}

int showError(const char* title, const std::string& error, SDL_Window* window = nullptr)
{
    SDL_Log("%s: %s", title, error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, error.c_str(), window);
    return 1;
}
}

int runApplication(int argc, char** argv)
{
    std::string configArgument;
    std::string startScreenArgument;
    bool debugProfile = false;
    CLI::App commandLine{"Run the SpyHunter frontend reconstruction."};
    commandLine.add_option("config", configArgument, "Optional SpyHunter.config path");
    commandLine.add_flag("--debug-profile", debugProfile,
                         "Use isolated local-assets/debug-profiles.txt storage");
    commandLine.add_option("--start-screen", startScreenArgument,
                           "Open a System Options submenu directly (settings, controller, extras, music, movies, cheats)");
    argv = commandLine.ensure_utf8(argv);
    try {
        commandLine.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        const bool requestedHelp = error.get_exit_code() == static_cast<int>(CLI::ExitCodes::Success);
        SDL_ShowSimpleMessageBox(requestedHelp ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
                                 "RE::SpyHunter command line",
                                 requestedHelp ? commandLine.help().c_str() : error.what(), nullptr);
        return requestedHelp ? 0 : 2;
    }
    std::optional<FrontendMenu::Screen> debugStartScreen;
    if (!startScreenArgument.empty()) {
        if (!debugProfile) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter command line",
                                     "--start-screen requires --debug-profile.", nullptr);
            return 2;
        }
        debugStartScreen = parseDebugStartScreen(startScreenArgument);
        if (!debugStartScreen) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter command line",
                                     "Unknown --start-screen value. Choose settings, controller, extras, music, movies or cheats.", nullptr);
            return 2;
        }
    }
    const auto configPath = configArgument.empty()
        ? configuration::findConfigFile(std::filesystem::current_path())
        : std::filesystem::path(std::u8string(configArgument.begin(), configArgument.end()));
    const auto result = configuration::load(configPath);
    if (!result.config) return showError("RE::SpyHunter configuration", result.error);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        return showError("RE::SpyHunter", SDL_GetError());
    // Reverse destruction order: intro/audio/controllers, renderer, window, SDL.
    const SdlLifetime sdl;
    Window window(SDL_CreateWindow(debugStartScreen ? "RE::SpyHunter - Frontend" : "RE::SpyHunter - Intro",
                                   1280, 896, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    Renderer renderer(window ? SDL_CreateRenderer(window.get(), nullptr) : nullptr, SDL_DestroyRenderer);
    if (!window || !renderer) return showError("RE::SpyHunter", SDL_GetError(), window.get());
    SDL_SetRenderLogicalPresentation(renderer.get(), IntroSequence::logicalWidth, IntroSequence::logicalHeight,
                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);

    runtime::GamepadSet gamepads;
    gamepads.discover();
    std::string error;
    std::unique_ptr<IntroSequence> intro;
    std::unique_ptr<FrontendMenu> frontend;
    if (!debugStartScreen) {
        intro = std::make_unique<IntroSequence>(renderer.get(), result.config->extractedBinariesPath,
                                               result.config->autosaveEnabled);
        if (!intro->load(error)) return showError("RE::SpyHunter intro assets", error, window.get());
    }
    const auto createFrontend = [&]() {
        const auto assetRoot = result.config->extractedBinariesPath;
        const auto localAssets = configPath.parent_path() / "local-assets";
        const auto profileFile = localAssets / (debugProfile ? "debug-profiles.txt" : "profiles.txt");
        auto menu = std::make_unique<FrontendMenu>(renderer.get(), assetRoot, profileFile,
                                                   localAssets / "MBACK.mp4");
        if (!menu->load(error, debugProfile, debugStartScreen)) return false;
        frontend = std::move(menu);
        SDL_SetWindowTitle(window.get(), "RE::SpyHunter - Frontend");
        return true;
    };
    if (debugStartScreen && !createFrontend())
        return showError("RE::SpyHunter frontend assets", error, window.get());
    bool displayedPreMenuLoading = false;
    auto previous = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (!frontend && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE))
                running = false;
            gamepads.handleEvent(event);
            if (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN || gamepads.contains(event.gbutton.which)) {
                if (frontend) frontend->handleEvent(event);
                else if (intro) intro->handleEvent(event);
            }
        }
        if (!running) break;
        const auto now = SDL_GetTicksNS();
        const double delta = static_cast<double>(now - previous) / 1'000'000'000.0;
        previous = now;
        if (frontend) {
            frontend->update();
            frontend->render();
        } else {
            if (!intro->update(delta, error)) return showError("RE::SpyHunter movie", error, window.get());
            intro->render();
            if (intro->readyForMenu()) {
                if (displayedPreMenuLoading) {
                    if (!createFrontend()) return showError("RE::SpyHunter frontend assets", error, window.get());
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
