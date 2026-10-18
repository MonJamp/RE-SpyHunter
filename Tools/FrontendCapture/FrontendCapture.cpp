#include "../../RE-SpyHunter/FrontendMenu.h"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

namespace
{
struct Options
{
    std::filesystem::path assets;
    std::filesystem::path background;
    std::filesystem::path output;
    std::string_view screen = "profile";
    std::string_view region = "full";
    int selection = 0;
    int timeMs = 100;
    bool filled = false;
    std::optional<SDL_Rect> rect;
};

void usage()
{
    std::cerr << "Usage: FrontendCapture --assets ROOT --output FILE.bmp "
                 "[--background MBACK.mp4] [--screen profile|name|main] "
                 "[--selection INDEX] [--filled] [--time-ms 0..8000] "
                 "[--region full|header|table|selection|footer] [--rect x,y,width,height]\n";
}

bool parseUnsigned(const std::string_view value, int& number)
{
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && number >= 0;
}

std::optional<SDL_Rect> parseRect(std::string_view value)
{
    int fields[4]{};
    for (int index = 0; index < 4; ++index) {
        const auto comma = value.find(',');
        if (!parseUnsigned(value.substr(0, comma), fields[index]) ||
            (index < 3 && comma == std::string_view::npos) ||
            (index == 3 && comma != std::string_view::npos)) return std::nullopt;
        if (index < 3) value.remove_prefix(comma + 1);
    }
    if (fields[2] == 0 || fields[3] == 0 || fields[2] > 640 || fields[3] > 448 ||
        fields[0] > 640 - fields[2] || fields[1] > 448 - fields[3]) return std::nullopt;
    return SDL_Rect{fields[0], fields[1], fields[2], fields[3]};
}

bool parseOptions(const int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view flag = argv[index];
        if (flag == "--filled") { options.filled = true; continue; }
        if (++index >= argc) return false;
        const std::string_view value = argv[index];
        if (flag == "--assets") options.assets = value;
        else if (flag == "--background") options.background = value;
        else if (flag == "--output") options.output = value;
        else if (flag == "--screen" && (value == "profile" || value == "name" || value == "main")) options.screen = value;
        else if (flag == "--region" && (value == "full" || value == "header" || value == "table" ||
                                         value == "selection" || value == "footer")) options.region = value;
        else if (flag == "--rect") {
            options.rect = parseRect(value);
            if (!options.rect) return false;
        } else if (flag == "--selection") {
            if (!parseUnsigned(value, options.selection)) return false;
        } else if (flag == "--time-ms") {
            if (!parseUnsigned(value, options.timeMs)) return false;
        } else return false;
    }
    return !options.assets.empty() && !options.output.empty() && options.timeMs <= 8000 &&
           options.selection < (options.screen == "main" ? 4 : 3) &&
           (options.screen != "name" || (options.selection == 0 && !options.filled));
}

SDL_Rect captureRegion(const Options& options)
{
    if (options.rect) return *options.rect;
    if (options.region == "header") return {0, 0, 640, 120};
    if (options.region == "footer") return {0, 390, 640, 58};
    if (options.region == "table") return {50, 90, 540, 260};
    if (options.region == "selection") {
        if (options.screen == "profile") return {65, 118 + options.selection * 35, 510, 32};
        if (options.screen == "main") {
            constexpr int y[]{150, 190, 235, 280};
            return {230, y[options.selection], 190, 35};
        }
        return {135, 160, 370, 65};
    }
    return {0, 0, 640, 448};
}

void key(spyhunter::game::FrontendMenu& menu, const SDL_Keycode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    menu.handleEvent(event);
}

bool createProfile(spyhunter::game::FrontendMenu& menu)
{
    key(menu, SDLK_RETURN);
    key(menu, SDLK_S); key(menu, SDLK_P); key(menu, SDLK_Y); key(menu, SDLK_1);
    key(menu, SDLK_RETURN);
    return menu.screen() == spyhunter::game::FrontendMenu::Screen::mainMenu;
}
}

int main(const int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options)) { usage(); return 2; }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    Window window(SDL_CreateWindow("frontend capture", 640, 448, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    Renderer renderer(window ? SDL_CreateRenderer(window.get(), "software") : nullptr, SDL_DestroyRenderer);
    if (!window || !renderer) { std::cerr << SDL_GetError() << '\n'; return 1; }

    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("SpyHunter-FrontendCapture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code filesystemError;
    if (!std::filesystem::create_directory(temporaryRoot, filesystemError)) {
        std::cerr << "Could not create isolated capture profile directory: " << filesystemError.message() << '\n';
        return 1;
    }
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(root / "profiles.txt", error);
            std::filesystem::remove(root, error);
        }
    } cleanup{temporaryRoot};

    std::string error;
    spyhunter::game::FrontendMenu menu(renderer.get(), options.assets, temporaryRoot / "profiles.txt", options.background);
    if (!menu.load(error)) { std::cerr << error << '\n'; return 1; }
    if (options.screen == "name") key(menu, SDLK_RETURN);
    if (options.screen == "main" || options.filled) {
        if (!createProfile(menu)) { std::cerr << "Could not create capture profile.\n"; return 1; }
        if (options.screen == "profile") key(menu, SDLK_ESCAPE);
    }
    for (int index = 0; index < options.selection; ++index) key(menu, SDLK_DOWN);
    const int frameCount = options.timeMs / 10;
    for (int frame = 0; frame < frameCount; ++frame) { menu.update(); SDL_Delay(10); }
    menu.update();
    menu.render();

    const auto region = captureRegion(options);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
        SDL_RenderReadPixels(renderer.get(), &region), SDL_DestroySurface);
    if (!pixels || !SDL_SaveBMP(pixels.get(), options.output.string().c_str())) {
        std::cerr << SDL_GetError() << '\n'; return 1;
    }
    std::cout << "Captured " << options.screen << " selection " << options.selection << " region "
              << region.x << ',' << region.y << ',' << region.w << ',' << region.h
              << " -> " << options.output.string() << '\n';
    return 0;
}
