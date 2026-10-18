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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::cerr << SDL_GetError() << '\n'; return 1; }
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

    spyhunter::ui::MenuAxisRepeat axis(0.95f, 0.6, 0.19);
    axis.setValue(0.94f);
    if (!require(axis.poll(0) == 0, "Stick threshold ignored.")) return 1;
    axis.setValue(1);
    if (!require(axis.poll(0) == 1 && axis.poll(0.59) == 0 && axis.poll(0.61) == 1 &&
                 axis.poll(0.7) == 0 && axis.poll(0.81) == 1, "Held-stick repeat timing failed.")) return 1;
    axis.setValue(-1);
    if (!require(axis.poll(0.82) == -1, "Stick reversal was not immediate.")) return 1;
    using Screen = spyhunter::game::FrontendMenu::Screen;
    const auto expectScreen = [&](Screen screen, const char* message) {
        menu.render();
        return require(menu.screen() == screen, message);
    };
    const auto root = [&](std::size_t index) {
        while (menu.screen() != Screen::mainMenu) key(menu, SDLK_ESCAPE);
        while (menu.selectedMainItem() != index) key(menu, SDLK_DOWN);
        key(menu, SDLK_RETURN);
    };
    root(0);
    if (!expectScreen(Screen::missionSelect, "Operations must directly open Mission Select.") ||
        !require(menu.itemCount() == 14, "Missing mission entries.")) return 1;
    key(menu, SDLK_LEFT);
    if (!require(menu.selectedSubmenuItem() == 13, "Mission carousel did not wrap.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == Screen::missionSelect && menu.status() == "MISSION LOCKED",
                 "Locked mission was accepted.")) return 1;
    key(menu, SDLK_RIGHT); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::briefing, "Open mission did not enter briefing.")) return 1;
    key(menu, SDLK_LEFT);
    if (!require(menu.selectedSubmenuItem() == 1 && menu.itemCount() == 2,
                 "Training mission must cycle Objectives/Weapons but skip Enemies.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == Screen::briefing && !menu.status().empty(), "Launch boundary missing.")) return 1;
    key(menu, SDLK_ESCAPE);
    if (!require(menu.selectedSubmenuItem() == 0, "Briefing lost mission cursor.")) return 1;

    root(1);
    if (!expectScreen(Screen::mainMenu, "Two Player must require second controller.") ||
        !require(!menu.status().empty(), "Missing controller connection hint.")) return 1;
    SDL_VirtualJoystickDesc virtualPad{};
    SDL_INIT_INTERFACE(&virtualPad);
    virtualPad.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    virtualPad.naxes = SDL_GAMEPAD_AXIS_COUNT;
    virtualPad.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    virtualPad.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    virtualPad.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    const auto pad1 = SDL_AttachVirtualJoystick(&virtualPad);
    const auto pad2 = SDL_AttachVirtualJoystick(&virtualPad);
    if (!require(pad1 && pad2, "Could not attach virtual controllers.")) return 1;
    SDL_PumpEvents();
    key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::twoPlayer, "Connected controllers did not enable Two Player.") ||
        !require(menu.itemCount() == 14, "Two Player must select missions.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.status().find("LOCKED") != std::string::npos, "Fresh profile bypassed two-player lock.")) return 1;
    SDL_DetachVirtualJoystick(pad2);
    SDL_DetachVirtualJoystick(pad1);
    SDL_PumpEvents();
    root(3);
    if (!expectScreen(Screen::dossier, "Dossier did not open.") ||
        !require(menu.itemCount() == 0, "Dossier must be read-only.")) return 1;
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::dossier, "Dossier exposed invented children.")) return 1;

    root(2);
    if (!require(menu.itemCount() == 3, "System Options must contain Extras.")) return 1;
    key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::settings, "Missing Settings.") || !require(menu.itemCount() == 5, "Missing settings controls.")) return 1;
    const auto initialOptions = menu.options();
    for (int index = 0; index < 5; ++index) {
        key(menu, SDLK_LEFT); key(menu, SDLK_DOWN);
    }
    if (!require(menu.options() != initialOptions, "Settings adjustments had no effect.")) return 1;
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    if (!require(menu.options() == initialOptions, "Settings cancel did not roll back.")) return 1;
    key(menu, SDLK_RETURN); key(menu, SDLK_DOWN); key(menu, SDLK_LEFT); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::systemOptions, "Settings accept did not return.") ||
        !require(menu.options().values[1] == 9, "Settings accept did not commit.")) return 1;

    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::controller1Options, "Missing player 1 schemes.")) return 1;
    key(menu, SDLK_RIGHT); key(menu, SDLK_ESCAPE);
    if (!require(menu.options().controllerSchemes[0] == 0, "Controller cancel committed.")) return 1;
    key(menu, SDLK_RETURN); key(menu, SDLK_LEFT); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::controller, "Controller accept did not return.") ||
        !require(menu.options().controllerSchemes[0] == 2, "Scheme wrap/commit failed.")) return 1;
    key(menu, SDLK_DOWN); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::controller2Options, "Missing player 2 schemes.")) return 1;
    menu.update();
    if (!expectScreen(Screen::controller, "Disconnected player 2 did not recover.")) return 1;
    key(menu, SDLK_ESCAPE); key(menu, SDLK_DOWN); key(menu, SDLK_RETURN);
    if (!expectScreen(Screen::extras, "Extras must belong to System Options.")) return 1;

    constexpr Screen extraScreens[]{Screen::musicPlayer, Screen::moviePlayer, Screen::cheatGrid};
    constexpr std::size_t counts[]{19, 24, 16};
    for (std::size_t index = 0; index < 3; ++index) {
        key(menu, SDLK_RETURN);
        if (!expectScreen(extraScreens[index], "Missing Extras leaf.") ||
            !require(menu.itemCount() == counts[index], "Incomplete original catalogue.")) return 1;
        if (index == 2) {
            key(menu, SDLK_UP);
            if (!require(menu.selectedSubmenuItem() == 12, "Reward grid row wrap failed.")) return 1;
            key(menu, SDLK_DOWN);
        }
        key(menu, SDLK_LEFT);
        if (!require(menu.selectedSubmenuItem() == counts[index] - 1, "Catalogue wrap failed.")) return 1;
        for (std::size_t entry = 0; entry < counts[index]; ++entry) {
            menu.render(); // Every item must render without overflowing the list.
            key(menu, SDLK_RIGHT);
        }
        key(menu, SDLK_RETURN);
        if (!require(menu.screen() == extraScreens[index] && !menu.status().empty(), "Leaf action boundary missing.")) return 1;
        if (index == 0) {
            for (int layout = 0; layout < 5; ++layout) gamepadButton(menu, SDL_GAMEPAD_BUTTON_WEST);
            if (!require(menu.musicLayout() == 0, "Music layouts did not wrap.")) return 1;
        }
        key(menu, SDLK_ESCAPE);
        if (!require(menu.selectedSubmenuItem() == index, "Extras lost parent selection.")) return 1;
        key(menu, SDLK_DOWN);
    }
    root(0);
    key(menu, SDLK_ESCAPE); key(menu, SDLK_ESCAPE); // Main -> profile
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_WEST);
    if (!expectScreen(Screen::deleteProfile, "Square did not request confirmation.") ||
        !require(menu.selectedSubmenuItem() == 1, "Deletion must default to No.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.profiles()[0] == "SPY1", "No deleted the profile.")) return 1;

    spyhunter::game::FrontendMenu reloaded(renderer.get(), assetsPath, profilePath, {});
    if (!reloaded.load(error) || !require(reloaded.profiles()[0] == "SPY1", "Profile did not persist.")) {
        if (!error.empty()) std::cerr << error << '\n';
        return 1;
    }
    key(reloaded, SDLK_RETURN);
    if (!require(reloaded.options().values[1] == 9 && reloaded.options().controllerSchemes[0] == 2,
                 "Accepted profile options did not persist.")) return 1;
    if (!require(reloaded.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu,
                 "Existing profile did not reach main menu.")) return 1;
    key(reloaded, SDLK_ESCAPE);
    gamepadButton(reloaded, SDL_GAMEPAD_BUTTON_WEST);
    key(reloaded, SDLK_UP); key(reloaded, SDLK_RETURN);
    if (!require(reloaded.profiles()[0].empty() && reloaded.screen() == Screen::profileSelect,
                 "Confirmed delete did not clear the profile.")) return 1;
    std::cout << "Frontend menu smoke checks passed.\n";
    return 0;
}
