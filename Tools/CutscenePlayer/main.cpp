#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../../Configuration/SpyHunterConfig.h"
#include "../../GameRuntime/CutscenePlayer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 720;
constexpr float kBrowserWidth = 340.0f;
constexpr float kRowHeight = 22.0f;
constexpr float kBrowserTop = 88.0f;

struct CutsceneFile
{
    std::filesystem::path sourcePath;
    std::string displayPath;
};

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

std::vector<CutsceneFile> findCutscenes(const std::filesystem::path& root)
{
    std::vector<CutsceneFile> cutscenes;
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator iterator(root, options, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        if (lowercase(iterator->path().extension().string()) != ".pss") continue;

        std::error_code relativeError;
        auto relativePath = std::filesystem::relative(iterator->path(), root, relativeError);
        cutscenes.push_back({iterator->path(), relativeError ? iterator->path().string() : relativePath.generic_string()});
    }

    std::sort(cutscenes.begin(), cutscenes.end(), [](const CutsceneFile& left, const CutsceneFile& right) {
        return lowercase(left.displayPath) < lowercase(right.displayPath);
    });
    return cutscenes;
}

std::string shorten(const std::string& text, const size_t maximumCharacters)
{
    if (text.size() <= maximumCharacters) return text;
    if (maximumCharacters < 4) return text.substr(0, maximumCharacters);
    return text.substr(0, maximumCharacters - 3) + "...";
}

void drawText(SDL_Renderer* renderer, float x, float y, const std::string& text)
{
    SDL_RenderDebugText(renderer, x, y, text.c_str());
}

void drawPanel(SDL_Renderer* renderer, const SDL_FRect& rectangle, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rectangle);
}
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

    const auto cutscenes = findCutscenes(configResult.config->extractedBinariesPath);
    SDL_Log("Configuration: %s", configPath.string().c_str());
    SDL_Log("Found %zu PSS cutscenes under %s.", cutscenes.size(),
            configResult.config->extractedBinariesPath.string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("SpyHunter - Cutscene Player", kInitialWindowWidth, kInitialWindowHeight,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    spyhunter::runtime::CutsceneSession session;
    size_t selectedIndex = 0;
    size_t firstVisibleIndex = 0;
    std::string status = cutscenes.empty() ? "No .PSS cutscenes were found." : "Select a cutscene and press Enter or click it.";
    bool running = true;

    const auto startSelectedCutscene = [&] {
        if (cutscenes.empty()) return;
        std::string error;
        const spyhunter::runtime::CutsceneOptions options{
            cutscenes[selectedIndex].sourcePath,
            configPath.parent_path() / "local-assets",
            true};
        if (session.load(renderer, options, error)) {
            status = "Playing " + cutscenes[selectedIndex].displayPath;
        } else {
            status = "Could not play: " + error;
            SDL_Log("Cutscene playback failed: %s", error.c_str());
        }
    };

    while (running) {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight);
        const float browserLeft = (std::max)(0.0f, static_cast<float>(outputWidth) - kBrowserWidth);
        const SDL_FRect browserRect{browserLeft, 0.0f, static_cast<float>(outputWidth) - browserLeft, static_cast<float>(outputHeight)};
        const size_t visibleRows = (std::max)(1, static_cast<int>((browserRect.h - kBrowserTop - 44.0f) / kRowHeight));

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    if (session.isPlaying()) {
                        session.stop();
                        status = "Playback stopped.";
                    } else {
                        running = false;
                    }
                } else if (!cutscenes.empty() && event.key.key == SDLK_DOWN) {
                    selectedIndex = (std::min)(selectedIndex + 1, cutscenes.size() - 1);
                } else if (!cutscenes.empty() && event.key.key == SDLK_UP) {
                    selectedIndex = selectedIndex == 0 ? 0 : selectedIndex - 1;
                } else if (!cutscenes.empty() && (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)) {
                    startSelectedCutscene();
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f) {
                if (event.wheel.y < 0.0f) {
                    firstVisibleIndex = (std::min)(firstVisibleIndex + 1, cutscenes.empty() ? size_t{0} : cutscenes.size() - 1);
                } else if (firstVisibleIndex > 0) {
                    --firstVisibleIndex;
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                       event.button.x >= browserLeft && event.button.y >= kBrowserTop) {
                const size_t row = static_cast<size_t>((event.button.y - kBrowserTop) / kRowHeight);
                const size_t index = firstVisibleIndex + row;
                if (index < cutscenes.size() && row < visibleRows) {
                    selectedIndex = index;
                    startSelectedCutscene();
                }
            }
        }

        if (!cutscenes.empty()) {
            if (selectedIndex < firstVisibleIndex) firstVisibleIndex = selectedIndex;
            if (selectedIndex >= firstVisibleIndex + visibleRows) firstVisibleIndex = selectedIndex - visibleRows + 1;
        }
        session.update();
        if (!session.error().empty()) status = "Playback failed: " + session.error();
        if (session.hasFinished()) status = "Playback finished. Select another cutscene.";

        SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255);
        SDL_RenderClear(renderer);

        const SDL_FRect videoViewport{0.0f, 0.0f, browserLeft, static_cast<float>(outputHeight)};
        drawPanel(renderer, videoViewport, SDL_Color{0, 0, 0, 255});
        session.render(renderer, videoViewport);
        if (!session.isPlaying()) {
            SDL_SetRenderDrawColor(renderer, 170, 180, 195, 255);
            drawText(renderer, 24.0f, 24.0f, "Cutscene preview");
            drawText(renderer, 24.0f, 42.0f, "Choose a .PSS file from the browser.");
        }

        drawPanel(renderer, browserRect, SDL_Color{27, 31, 42, 255});
        SDL_SetRenderDrawColor(renderer, 89, 104, 132, 255);
        SDL_RenderLine(renderer, browserLeft, 0.0f, browserLeft, static_cast<float>(outputHeight));
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, 255);
        drawText(renderer, browserLeft + 16.0f, 16.0f, "CUTSCENE BROWSER");
        SDL_SetRenderDrawColor(renderer, 160, 170, 190, 255);
        drawText(renderer, browserLeft + 16.0f, 36.0f, std::to_string(cutscenes.size()) + " .PSS files");
        drawText(renderer, browserLeft + 16.0f, 54.0f, "Click / Enter: play | Esc: stop");

        for (size_t row = 0; row < visibleRows && firstVisibleIndex + row < cutscenes.size(); ++row) {
            const size_t index = firstVisibleIndex + row;
            const SDL_FRect rowRect{browserLeft + 8.0f, kBrowserTop + row * kRowHeight, browserRect.w - 16.0f, kRowHeight - 2.0f};
            if (index == selectedIndex) {
                drawPanel(renderer, rowRect, SDL_Color{57, 95, 152, 255});
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 210, 218, 230, 255);
            }
            drawText(renderer, rowRect.x + 6.0f, rowRect.y + 4.0f, shorten(cutscenes[index].displayPath, 43));
        }

        SDL_SetRenderDrawColor(renderer, 180, 190, 205, 255);
        drawText(renderer, browserLeft + 16.0f, static_cast<float>(outputHeight) - 28.0f, shorten(status, 45));
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
