#include "../FrontendMenu.h"

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
    if (argc < 2 || argc > 5) { std::cerr << "Expected extracted-binaries root, optional converted MBACK.mp4, profile BMP, and main-menu BMP.\n"; return 2; }
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
    const std::filesystem::path backgroundPath = argc >= 3 ? argv[2] : "";
    spyhunter::game::FrontendMenu menu(renderer.get(), argv[1], profilePath, backgroundPath);
    if (!menu.load(error)) { std::cerr << error << '\n'; return 1; }
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::profileSelect, "Did not start at profile select.")) return 1;
    for (int frame = 0; frame < 30; ++frame) { menu.update(); SDL_Delay(10); }
    menu.render();
    const auto firstFrameHash = frameHash(renderer.get());
    if (argc >= 4) {
        SDL_RenderPresent(renderer.get());
        SDL_Surface* capture = SDL_RenderReadPixels(renderer.get(), nullptr);
        if (!capture || !SDL_SaveBMP(capture, argv[3])) { std::cerr << SDL_GetError() << '\n'; if (capture) SDL_DestroySurface(capture); return 1; }
        SDL_DestroySurface(capture);
    }
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
    if (argc == 5) {
        SDL_RenderPresent(renderer.get());
        SDL_Surface* capture = SDL_RenderReadPixels(renderer.get(), nullptr);
        if (!capture || !SDL_SaveBMP(capture, argv[4])) { std::cerr << SDL_GetError() << '\n'; if (capture) SDL_DestroySurface(capture); return 1; }
        SDL_DestroySurface(capture);
    }
    gamepadButton(menu, SDL_GAMEPAD_BUTTON_NORTH);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::profileSelect,
                 "Triangle did not return from main menu to profile select.")) return 1;
    key(menu, SDLK_RETURN);
    if (!require(menu.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu,
                 "Existing profile did not re-enter main menu after Back.")) return 1;

    spyhunter::game::FrontendMenu reloaded(renderer.get(), argv[1], profilePath, {});
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
