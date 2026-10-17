#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../../Configuration/SpyHunterConfig.h"
#include "../../GUI/Gui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr spyhunter::gui::BrowserLayout kBrowser;

struct StillFile { std::filesystem::path path; std::string displayPath; };

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool isStillExtension(const std::filesystem::path& path)
{
    const auto extension = lowercase(path.extension().string());
    return extension == ".vki" || extension == ".bmp" || extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

std::vector<StillFile> findStills(const std::filesystem::path& root)
{
    std::vector<StillFile> results;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!iterator->is_regular_file(error) || error || !isStillExtension(iterator->path())) { error.clear(); continue; }
        std::error_code relativeError;
        const auto relative = std::filesystem::relative(iterator->path(), root, relativeError);
        results.push_back({iterator->path(), relativeError ? iterator->path().string() : relative.generic_string()});
    }
    std::sort(results.begin(), results.end(), [](const StillFile& left, const StillFile& right) { return lowercase(left.displayPath) < lowercase(right.displayPath); });
    return results;
}

std::string vkiDetails(const std::filesystem::path& path)
{
    std::ifstream source(path, std::ios::binary);
    unsigned char header[100]{};
    source.read(reinterpret_cast<char*>(header), sizeof(header));
    if (source.gcount() < 100 || header[0] != 0x65 || header[1] != 0x32 || header[2] != 0x77 || header[3] != 0xba) return "Unrecognised .VKI container.";
    const auto width = static_cast<unsigned int>(header[0x5d]) | (static_cast<unsigned int>(header[0x5e]) << 8);
    const auto height = static_cast<unsigned int>(header[0x60]) | (static_cast<unsigned int>(header[0x61]) << 8);
    return "Paradigm vkImage container: " + std::to_string(width) + " x " + std::to_string(height) + ".";
}
}

int main(int argc, char* argv[])
{
    const std::filesystem::path configPath = argc > 1 ? std::filesystem::path(argv[1]) : spyhunter::configuration::findConfigFile(std::filesystem::current_path());
    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) { SDL_Log("Configuration error: %s", configResult.error.c_str()); return 1; }
    const auto stills = findStills(configResult.config->extractedBinariesPath);

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("SpyHunter - Still Viewer", spyhunter::gui::defaultWindowWidth, spyhunter::gui::defaultWindowHeight, SDL_WINDOW_RESIZABLE, &window, &renderer) || !spyhunter::gui::configureRenderer(renderer)) {
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    size_t selected = 0, firstVisible = 0;
    std::string details = stills.empty() ? "No supported still files found." : vkiDetails(stills.front().path);
    bool running = true;
    while (running) {
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &pixelWidth, &pixelHeight);
        const float width = pixelWidth / spyhunter::gui::defaultUiScale, height = pixelHeight / spyhunter::gui::defaultUiScale;
        const float browserLeft = (std::max)(0.0f, width - kBrowser.width);
        const size_t visibleRows = (std::max)(1, static_cast<int>((height - kBrowser.top - 44.0f) / kBrowser.rowHeight));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = false;
            else if (!stills.empty() && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_DOWN) selected = (std::min)(selected + 1, stills.size() - 1);
            else if (!stills.empty() && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_UP) selected = selected ? selected - 1 : 0;
            else if (!stills.empty() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && event.button.x / spyhunter::gui::defaultUiScale >= browserLeft && event.button.y / spyhunter::gui::defaultUiScale >= kBrowser.top) {
                const size_t row = static_cast<size_t>((event.button.y / spyhunter::gui::defaultUiScale - kBrowser.top) / kBrowser.rowHeight);
                if (firstVisible + row < stills.size() && row < visibleRows) selected = firstVisible + row;
            }
        }
        if (!stills.empty()) {
            if (selected < firstVisible) firstVisible = selected;
            if (selected >= firstVisible + visibleRows) firstVisible = selected - visibleRows + 1;
            details = lowercase(stills[selected].path.extension().string()) == ".vki" ? vkiDetails(stills[selected].path) : "Standard still image selected.";
        }
        SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255); SDL_RenderClear(renderer);
        const SDL_FRect preview{0, 0, browserLeft, height};
        spyhunter::gui::panel(renderer, preview, SDL_Color{0, 0, 0, 255});
        SDL_SetRenderDrawColor(renderer, 225, 232, 244, 255);
        spyhunter::gui::text(renderer, 24, 24, "STILL PREVIEW");
        if (!stills.empty()) {
            spyhunter::gui::text(renderer, 24, 52, stills[selected].displayPath);
            spyhunter::gui::text(renderer, 24, 76, details);
            SDL_SetRenderDrawColor(renderer, 180, 190, 205, 255);
            spyhunter::gui::text(renderer, 24, 112, "VKI rendering is provided by the still-image runtime decoder.");
            spyhunter::gui::text(renderer, 24, 132, "Packed INTRO.PCK images, including legal_screen.vki, will appear after PCK extraction.");
        }
        const SDL_FRect browser{browserLeft, 0, width - browserLeft, height};
        spyhunter::gui::panel(renderer, browser, SDL_Color{27, 31, 42, 255});
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, 255); spyhunter::gui::text(renderer, browserLeft + 16, 16, "STILL BROWSER");
        SDL_SetRenderDrawColor(renderer, 160, 170, 190, 255); spyhunter::gui::text(renderer, browserLeft + 16, 36, std::to_string(stills.size()) + " image assets");
        spyhunter::gui::text(renderer, browserLeft + 16, 54, "Click / arrows: select | Esc: exit");
        for (size_t row = 0; row < visibleRows && firstVisible + row < stills.size(); ++row) {
            const size_t index = firstVisible + row;
            const SDL_FRect rowRect{browserLeft + 8, kBrowser.top + row * kBrowser.rowHeight, browser.w - 16, kBrowser.rowHeight - 2};
            if (index == selected) { spyhunter::gui::panel(renderer, rowRect, SDL_Color{57, 95, 152, 255}); SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); }
            else SDL_SetRenderDrawColor(renderer, 210, 218, 230, 255);
            spyhunter::gui::text(renderer, rowRect.x + 6, rowRect.y + 4, spyhunter::gui::shorten(stills[index].displayPath, 43));
        }
        SDL_RenderPresent(renderer); SDL_Delay(1);
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
