#include "../../RE-SpyHunter/FrontendMenu.h"
#include "../../ThirdParty/CLI11/CLI11.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
struct Options
{
    std::string assets;
    std::string background;
    std::string output;
    std::string screen = "profile";
    std::string region = "full";
    int selection = 0;
    int timeMs = 100;
    int missionPoints = 0;
    bool filled = false;
    bool allLevels = false;
    std::vector<int> completedPrimary;
    std::vector<int> rectFields;
    std::vector<int> path;
    std::optional<SDL_Rect> rect;
};

std::filesystem::path pathFromUtf8(const std::string& value)
{
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

void configureParser(CLI::App& app, Options& options)
{
    app.add_option("--assets", options.assets, "Root directory containing extracted game assets")
        ->required();
    app.add_option("--output", options.output, "Destination BMP path (directory must exist)")
        ->required();
    app.add_option("--background", options.background, "Optional prepared MBACK.mp4 background");
    app.add_option("--screen", options.screen, "Screen to render: profile, name, or main")
        ->check(CLI::IsMember(std::vector<std::string>{"profile", "name", "main"}));
    app.add_option("--selection", options.selection, "Selected profile/menu index (default: 0)")
        ->check(CLI::Range(0, 23));
    app.add_option("--path", options.path, "Comma-separated menu indices to accept from the main menu")
        ->delimiter(',')->expected(1, -1)->check(CLI::Range(0, 23));
    app.add_flag("--filled", options.filled, "Create a temporary profile before rendering");
    app.add_option("--time-ms", options.timeMs, "Background sample time in milliseconds (default: 100)")
        ->check(CLI::Range(0, 8000));
    app.add_option("--mission-points", options.missionPoints, "Objective points for the mission-select profile fixture")
        ->check(CLI::Range(0, 1000000));
    app.add_option("--completed-primary", options.completedPrimary,
                   "Completed primary mission IDs for the mission-select fixture (comma-separated)")
        ->delimiter(',')->expected(1, -1)->check(CLI::Range(1, 14));
    app.add_flag("--all-levels", options.allLevels, "Unlock all missions in the mission-select capture fixture");
    app.add_option("--region", options.region, "Named crop: full, header, table, selection, or footer")
        ->check(CLI::IsMember(std::vector<std::string>{"full", "header", "table", "selection", "footer"}));
    app.add_option("--rect", options.rectFields, "Exact crop x,y,width,height (overrides --region)")
        ->delimiter(',')->expected(4)->check(CLI::Range(0, 640));
}

bool validateOptions(Options& options, std::string& error)
{
    if (!options.path.empty() && options.screen != "main") {
        error = "--path requires --screen main."; return false;
    }
    if (options.path.empty() && options.selection >= (options.screen == "main" ? 4 : 3)) {
        error = "--selection is out of range for the selected screen.";
        return false;
    }
    if (options.screen == "name" && (options.selection != 0 || options.filled)) {
        error = "The name screen does not support --selection or --filled.";
        return false;
    }
    std::sort(options.completedPrimary.begin(), options.completedPrimary.end());
    if (std::adjacent_find(options.completedPrimary.begin(), options.completedPrimary.end()) != options.completedPrimary.end()) {
        error = "--completed-primary mission IDs must not be repeated.";
        return false;
    }
    if (!options.rectFields.empty()) {
        const auto& fields = options.rectFields;
        if (fields[2] == 0 || fields[3] == 0 || fields[2] > 640 || fields[3] > 448 ||
            fields[0] > 640 - fields[2] || fields[1] > 448 - fields[3]) {
            error = "--rect must fit inside the 640x448 canvas and have nonzero dimensions.";
            return false;
        }
        options.rect = SDL_Rect{fields[0], fields[1], fields[2], fields[3]};
    }
    return true;
}

SDL_Rect captureRegion(const Options& options)
{
    if (options.rect) return *options.rect;
    if (options.region == "header") return {0, 0, 640, 120};
    if (options.region == "footer") return {0, 390, 640, 58};
    if (options.region == "table") return {50, 90, 540, 260};
    if (options.region == "selection") {
        if (!options.path.empty()) return {50, 90, 540, 260};
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
    CLI::App app{"Render a SpyHunter frontend screen and save a native-resolution BMP crop."};
    configureParser(app, options);
    argv = app.ensure_utf8(argv);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error) == 0 ? 0 : 2;
    }
    std::string optionError;
    if (!validateOptions(options, optionError)) {
        std::cerr << optionError << "\nRun with --help to see supported options.\n";
        return 2;
    }
    const auto assetsPath = pathFromUtf8(options.assets);
    const auto backgroundPath = options.background.empty() ? std::filesystem::path{} :
                                pathFromUtf8(options.background);
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
    spyhunter::game::FrontendMenu menu(renderer.get(), assetsPath, temporaryRoot / "profiles.txt", backgroundPath);
    if (!menu.load(error)) { std::cerr << error << '\n'; return 1; }
    spyhunter::game::FrontendMenu::MissionProgress missionProgress;
    missionProgress.objectivePoints = options.missionPoints;
    missionProgress.allLevelsOverride = options.allLevels;
    for (const int missionId : options.completedPrimary)
        missionProgress.primaryObjectivesComplete[static_cast<std::size_t>(missionId - 1)] = true;
    if (!menu.setMissionProgress(0, missionProgress)) {
        std::cerr << "Invalid mission-select progress fixture.\n";
        return 2;
    }
    if (options.screen == "name") key(menu, SDLK_RETURN);
    if (options.screen == "main" || options.filled) {
        if (!createProfile(menu)) { std::cerr << "Could not create capture profile.\n"; return 1; }
        if (options.screen == "profile") key(menu, SDLK_ESCAPE);
    }
    for (const int selected : options.path) {
        if (static_cast<std::size_t>(selected) >= menu.itemCount()) { std::cerr << "Menu path index is out of range.\n"; return 2; }
        const auto direction = menu.screen() == spyhunter::game::FrontendMenu::Screen::cheatGrid ||
            menu.screen() == spyhunter::game::FrontendMenu::Screen::briefing ? SDLK_RIGHT : SDLK_DOWN;
        for (int index = 0; index < selected; ++index) key(menu, direction);
        const auto previous = menu.screen();
        key(menu, SDLK_RETURN);
        if (menu.screen() == previous) { std::cerr << "Menu path reaches a placeholder action.\n"; return 2; }
    }
    if (options.screen != "name" && static_cast<std::size_t>(options.selection) >= menu.itemCount() &&
        !(menu.itemCount() == 0 && options.selection == 0)) {
        std::cerr << "Selection is out of range for the destination screen.\n"; return 2;
    }
    const auto direction = menu.screen() == spyhunter::game::FrontendMenu::Screen::cheatGrid ||
        menu.screen() == spyhunter::game::FrontendMenu::Screen::briefing ? SDLK_RIGHT : SDLK_DOWN;
    for (int index = 0; index < options.selection; ++index) key(menu, direction);
    const int frameCount = options.timeMs / 10;
    for (int frame = 0; frame < frameCount; ++frame) { menu.update(); SDL_Delay(10); }
    menu.update();
    menu.render();

    const auto region = captureRegion(options);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
        SDL_RenderReadPixels(renderer.get(), &region), SDL_DestroySurface);
    if (!pixels || !SDL_SaveBMP(pixels.get(), options.output.c_str())) {
        std::cerr << SDL_GetError() << '\n'; return 1;
    }
    std::cout << "Captured " << options.screen << " selection " << options.selection << " region "
              << region.x << ',' << region.y << ',' << region.w << ',' << region.h
              << " -> " << options.output << '\n';
    return 0;
}
