#include "../FrontendMenu.h"
#include "../../ThirdParty/CLI11/CLI11.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <span>

namespace
{
void key(spyhunter::game::FrontendMenu& menu, SDL_Keycode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    menu.handleEvent(event);
}

void gamepadButton(spyhunter::game::FrontendMenu& menu, SDL_GamepadButton button)
{
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = button;
    menu.handleEvent(event);
}

bool require(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

std::uint64_t frameHash(SDL_Renderer* renderer)
{
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
    if (!surface) return 0;
    const auto bytes = std::span(static_cast<const std::uint8_t*>(surface->pixels),
                                 static_cast<std::size_t>(surface->pitch) * surface->h);
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}

}

int main(int argc, char** argv)
{
    std::string assetsArgument;
    std::string backgroundArgument;
    CLI::App commandLine{"Run frontend-menu behavior checks using extracted game assets."};
    commandLine.add_option("assets", assetsArgument, "Extracted-binaries root")->required();
    commandLine.add_option("-b,--background", backgroundArgument, "Converted MBACK.mp4 path");
    argv = commandLine.ensure_utf8(argv);
    try {
        commandLine.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return commandLine.exit(error) == 0 ? 0 : 2;
    }
    const auto assetsPath = std::filesystem::path(std::u8string(assetsArgument.begin(), assetsArgument.end()));
    const auto backgroundPath = backgroundArgument.empty() ? std::filesystem::path{} :
        std::filesystem::path(std::u8string(backgroundArgument.begin(), backgroundArgument.end()));
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    Window window(SDL_CreateWindow("frontend smoke", 640, 448, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    Renderer renderer(window ? SDL_CreateRenderer(window.get(), "software") : nullptr, SDL_DestroyRenderer);
    if (!window || !renderer) { std::cerr << SDL_GetError() << '\n'; return 1; }

    const auto profilePath = std::filesystem::current_path() / "FrontendMenuSmoke.profiles.tmp";
    std::filesystem::remove(profilePath);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{profilePath};
    std::string error;
    spyhunter::game::FrontendMenu menu(renderer.get(), assetsPath, profilePath, backgroundPath);
    if (!menu.load(error)) { std::cerr << error << '\n'; return 1; }
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::profileSelect, "Did not start at profile select.")) return 1;
    for (int frame = 0; frame < 30; ++frame) { menu.update(); SDL_Delay(10); }
    menu.render();
    const auto firstFrameHash = frameHash(renderer.get());
    if (!backgroundPath.empty()) {
        for (int frame = 0; frame < 20; ++frame) { menu.update(); SDL_Delay(10); }
        menu.render();
        if (!require(firstFrameHash != 0 && frameHash(renderer.get()) != firstFrameHash,
                     "Converted MBACK background did not animate.")) return 1;
    }
    key(menu, SDLK_UP);
    if (!require(menu.selectedProfile() == 2, "Profile selection did not wrap upward.")) return 1;
    key(menu, SDLK_DOWN);
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::nameEntry, "Empty slot did not open name entry.")) return 1;
    key(menu, SDLK_S); key(menu, SDLK_P); key(menu, SDLK_Y);
    key(menu, SDLK_BACKSPACE); key(menu, SDLK_Y); key(menu, SDLK_1);
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu, "Named profile did not reach main menu.") ||
        !require(menu.profiles()[0] == "SPY1", "Profile name was not edited as expected.") ||
        !require(std::filesystem::exists(profilePath), "Profile file was not created.")) return 1;
    key(menu, SDLK_UP);
    if (!require(menu.selectedMainItem() == 3, "Main-menu selection did not wrap upward.")) return 1;
    key(menu, SDLK_DOWN);
    menu.update();
    menu.render();
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::profileSelect,
                 "Triangle did not return from main menu to profile select.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu,
                 "Existing profile did not re-enter main menu after Back.")) return 1;

    constexpr std::array submenuScreens{
        spyhunter::game::FrontendMenu::Screen::operations,
        spyhunter::game::FrontendMenu::Screen::twoPlayer,
        spyhunter::game::FrontendMenu::Screen::systemOptions,
        spyhunter::game::FrontendMenu::Screen::dossier
    };
    for (std::size_t index = 0; index < submenuScreens.size(); ++index) {
        while (menu.selectedMainItem() != index) key(menu, SDLK_DOWN);
        key(menu, SDLK_RETURN);
        if (!require(menu.screen() == submenuScreens[index], "Main-menu item did not open its submenu.")) return 1;
        key(menu, SDLK_DOWN);
        if (index == 1 && !require(menu.selectedSubmenuItem() == 1,
                                   "Two Player did not expose both player entries.")) return 1;
        key(menu, SDLK_UP);
        if (!require(menu.selectedSubmenuItem() == 0, "Submenu navigation did not wrap back to its first option.")) return 1;
        menu.update();
        menu.render();
        gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
        if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu,
                     "Triangle did not return from submenu to main menu.")) return 1;
        if (!require(menu.selectedMainItem() == index, "Returning from submenu lost the main-menu selection.")) return 1;
    }

    const auto expectScreen = [&](const auto screen, const char* message) {
        return require(menu.screen() == screen, message);
    };
    key(menu, SDLK_DOWN); // Wrap from Dossier to Operations
    key(menu, SDLK_RETURN); // Operations
    key(menu, SDLK_RETURN); // Mission Select
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::missionSelect,
                      "Operations did not open Mission Select.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    key(menu, SDLK_DOWN);
    key(menu, SDLK_RETURN); // Rendezvous
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::rendezvous,
                      "Operations did not open Rendezvous.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);

    key(menu, SDLK_DOWN); key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // System Options
    key(menu, SDLK_RETURN); // Settings
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::settings,
                      "System Options did not open Settings.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Controller
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::controller,
                      "System Options did not open Controller.")) return 1;
    key(menu, SDLK_RETURN); // Controller 1
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::controller1Options,
                      "Controller did not open Controller 1 Options.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Controller 2
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::controller2Options,
                      "Controller did not open Controller 2 Options.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);

    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Dossier
    key(menu, SDLK_RETURN); // Extras
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::extras,
                      "Dossier did not open Extras.")) return 1;
    key(menu, SDLK_RETURN); // Music Player
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::musicPlayer,
                      "Extras did not open Music Player.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Movie Player
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::moviePlayer,
                      "Extras did not open Movie Player.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Cheat Grid
    if (!expectScreen(spyhunter::game::FrontendMenu::Screen::cheatGrid,
                      "Extras did not open Cheat Grid.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH); // Dossier
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); // Mission Data
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::missionData,
                 "Dossier did not open Mission Data.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);

    spyhunter::game::FrontendMenu reloaded(renderer.get(), assetsPath, profilePath, {});
    if (!reloaded.load(error) || !require(reloaded.profiles()[0] == "SPY1", "Profile did not persist.")) {
        if (!error.empty()) std::cerr << error << '\n';
        return 1;
    }
    key(reloaded, SDLK_RETURN);
    if (!require(reloaded.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu,
                 "Existing profile did not reach main menu.")) return 1;
    std::cout << "Frontend menu smoke checks passed.\n";
    return 0;
}
