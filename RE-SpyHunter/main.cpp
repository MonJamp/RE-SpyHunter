#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../Configuration/SpyHunterConfig.h"
#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/PckArchive.h"
#include "../GameRuntime/VkiImage.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 448;

enum class IntroState { loading, autosaveNotice, legal, movies, pressStart, preMenuLoading };

SDL_Texture* createTexture(SDL_Renderer* renderer, const spyhunter::runtime::DecodedStill& still)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                              still.width, still.height);
    if (!texture) return nullptr;
    if (!SDL_UpdateTexture(texture, nullptr, still.rgba.data(), still.width * 4)) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

bool loadPackedVki(const std::filesystem::path& archivePath, const std::string& suffix,
                   SDL_Renderer* renderer, SDL_Texture*& texture, std::string& error)
{
    spyhunter::runtime::PckArchive archive;
    if (!archive.open(archivePath, error)) return false;
    const auto& entries = archive.entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        auto name = entry.name;
        std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        auto wanted = suffix;
        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return name.ends_with(wanted) && entry.type == "vkImage";
    });
    if (found == entries.end()) { error = "Required VKI image was not found in " + archivePath.string(); return false; }

    const auto index = static_cast<std::size_t>(found - entries.begin());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(found->size));
    if (archive.readEntry(index, 0, bytes.data(), bytes.size(), error) != bytes.size()) return false;
    auto still = spyhunter::runtime::decodeVki(bytes, error);
    if (!still) return false;
    texture = createTexture(renderer, *still);
    if (!texture) { error = SDL_GetError(); return false; }
    SDL_Log("Loaded %s (%s)", found->name.c_str(), still->details.c_str());
    return true;
}

SDL_Texture* loadStandaloneVki(const std::filesystem::path& path, SDL_Renderer* renderer, std::string& error)
{
    auto still = spyhunter::runtime::decodeVki(path, error);
    if (!still) return nullptr;
    auto* texture = createTexture(renderer, *still);
    if (!texture) error = SDL_GetError();
    return texture;
}

void drawCentered(SDL_Renderer* renderer, SDL_Texture* texture)
{
    if (!texture) return;
    float width = 0, height = 0;
    SDL_GetTextureSize(texture, &width, &height);
    const float scale = std::min(kLogicalWidth / width, kLogicalHeight / height);
    const SDL_FRect destination{(kLogicalWidth - width * scale) / 2.0f,
                                (kLogicalHeight - height * scale) / 2.0f,
                                width * scale, height * scale};
    SDL_RenderTexture(renderer, texture, nullptr, &destination);
}

void drawTextCentered(SDL_Renderer* renderer, float y, const char* text, float scale = 1.0f)
{
    // SDL's built-in debug font is fixed-width; center its 8-pixel glyphs in the logical canvas.
    const float x = (kLogicalWidth - static_cast<float>(SDL_strlen(text)) * 8.0f * scale) / 2.0f;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_RenderDebugText(renderer, x / scale, y / scale, text);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}
}

int main(int argc, char** argv)
{
    const auto configPath = argc > 1 ? std::filesystem::path(argv[1])
        : spyhunter::configuration::findConfigFile(std::filesystem::current_path());
    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter configuration", configResult.error.c_str(), nullptr);
        return 1;
    }

    const auto& root = configResult.config->extractedBinariesPath;
    std::string error;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter", SDL_GetError(), nullptr);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("RE::SpyHunter - Intro", 1280, 896, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!window || !renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter", SDL_GetError(), window);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, kLogicalWidth, kLogicalHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    SDL_Texture* legalTexture = nullptr;
    SDL_Texture* logoTexture = nullptr;
    SDL_Texture* loadingTexture = nullptr;
    if (!loadPackedVki(root / "DATA" / "INTRO.PCK", "legal_screen.vki", renderer, legalTexture, error) ||
        !loadPackedVki(root / "DATA" / "INTRO.PCK", "menu_logoscreen.vki", renderer, logoTexture, error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter intro assets", error.c_str(), window);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    loadingTexture = loadStandaloneVki(root / "DATA" / "MLOAD.VKI", renderer, error);
    if (!loadingTexture) SDL_Log("MLOAD.VKI unavailable for the next state: %s", error.c_str());

    const std::vector<std::filesystem::path> movies{
        root / "MOVIES" / "M_LOGO.PSS",
        root / "MOVIES" / "PEILOGO.PSS",
        root / "MOVIES" / "INTRO.PSS"
    };
    spyhunter::runtime::CutsceneSession movie;
    std::size_t movieIndex = 0;
    IntroState state = IntroState::loading;
    double stateSeconds = 0.0;
    auto previous = SDL_GetTicksNS();
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (state == IntroState::pressStart &&
                    (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE || event.key.key == SDLK_S)) {
                    state = IntroState::preMenuLoading;
                    stateSeconds = 0.0;
                    SDL_Log("Start pressed: showing MLOAD.VKI before ShMSetupState/profile menu 7.");
                }
                if (state == IntroState::movies && event.key.key == SDLK_X) {
                    movie.requestSkip();
                }
                if (state == IntroState::autosaveNotice && event.key.key == SDLK_RETURN) {
                    state = IntroState::legal;
                    stateSeconds = 0.0;
                }
            }
            if (state == IntroState::pressStart && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                state = IntroState::preMenuLoading;
                stateSeconds = 0.0;
                SDL_Log("Start clicked: showing MLOAD.VKI before ShMSetupState/profile menu 7.");
            }
        }

        const auto now = SDL_GetTicksNS();
        const double delta = static_cast<double>(now - previous) / 1'000'000'000.0;
        previous = now;
        stateSeconds += std::min(delta, 0.05);

        if (state == IntroState::loading && stateSeconds >= 0.5) {
            state = IntroState::autosaveNotice; stateSeconds = 0.0;
        } else if (state == IntroState::autosaveNotice && stateSeconds >= 6.5) {
            state = IntroState::legal; stateSeconds = 0.0;
        } else if (state == IntroState::legal && stateSeconds >= 4.0) {
            movieIndex = 0;
            if (!movie.load(renderer, {movies[movieIndex], true}, error)) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter movie", error.c_str(), window);
                running = false;
            } else { state = IntroState::movies; stateSeconds = 0.0; }
        } else if (state == IntroState::movies) {
            movie.update();
            if (movie.hasFinished()) {
                movie.stop();
                ++movieIndex;
                if (movieIndex == movies.size()) {
                    state = IntroState::pressStart; stateSeconds = 0.0;
                } else if (!movie.load(renderer, {movies[movieIndex], true}, error)) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter movie", error.c_str(), window);
                    running = false;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (state == IntroState::loading) {
            drawTextCentered(renderer, 210.0f, "LOADING...");
        } else if (state == IntroState::autosaveNotice) {
            if (configResult.config->autosaveEnabled) {
                drawTextCentered(renderer, 165.0f, "CAUTION: SPY HUNTER USES AN AUTOSAVE FEATURE.");
                drawTextCentered(renderer, 195.0f, "A MEMORY CARD MAY BE REQUIRED TO SAVE.");
            } else {
                drawTextCentered(renderer, 165.0f, "AUTOSAVE IS DISABLED.");
                drawTextCentered(renderer, 195.0f, "GAME PROGRESS WILL NOT BE SAVED.");
            }
            drawTextCentered(renderer, 225.0f, "PRESS ENTER TO CONTINUE");
        } else if (state == IntroState::legal) {
            drawCentered(renderer, legalTexture);
        } else if (state == IntroState::movies) {
            const SDL_FRect viewport{0.0f, 0.0f, static_cast<float>(kLogicalWidth), static_cast<float>(kLogicalHeight)};
            movie.render(renderer, viewport);
        } else if (state == IntroState::pressStart) {
            drawCentered(renderer, logoTexture);
            drawTextCentered(renderer, 382.0f, "P R E S S  S T A R T  B U T T O N", 1.2f);
        } else if (state == IntroState::preMenuLoading) {
            if (loadingTexture) drawCentered(renderer, loadingTexture);
            else drawTextCentered(renderer, 210.0f, "LOADING...");
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    movie.stop();
    if (legalTexture) SDL_DestroyTexture(legalTexture);
    if (logoTexture) SDL_DestroyTexture(logoTexture);
    if (loadingTexture) SDL_DestroyTexture(loadingTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
