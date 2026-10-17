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

// The PS2 startup notice uses tracked, squared capitals on black. A compact 5x7
// bitmap face keeps that look deterministic across hosts without a system font.
struct IntroGlyph
{
    char character;
    std::uint8_t rows[7];
};

constexpr IntroGlyph kIntroGlyphs[] = {
    {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
    {'C', {14,17,16,16,16,17,14}}, {'D', {30,17,17,17,17,17,30}},
    {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
    {'G', {14,17,16,23,17,17,15}}, {'H', {17,17,17,31,17,17,17}},
    {'I', {14,4,4,4,4,4,14}},      {'J', {7,2,2,2,18,18,12}},
    {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
    {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,16,14,1,1,30}},   {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
    {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
    {'Y', {17,17,10,4,4,4,4}},     {'Z', {31,1,2,4,8,16,31}},
    {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}},     {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}},     {'5', {31,16,16,30,1,1,30}},
    {'6', {14,16,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,1,14}},
    {'.', {0,0,0,0,0,12,12}},      {':', {0,12,12,0,12,12,0}},
    {'-', {0,0,0,31,0,0,0}},       {'\'', {12,12,8,0,0,0,0}}
};

const IntroGlyph* findIntroGlyph(char character)
{
    const auto found = std::find_if(std::begin(kIntroGlyphs), std::end(kIntroGlyphs),
        [character](const IntroGlyph& glyph) { return glyph.character == character; });
    return found == std::end(kIntroGlyphs) ? nullptr : found;
}

void drawIntroTextCentered(SDL_Renderer* renderer, float centerY, const char* text,
                           float glyphScale = 1.0f, float tracking = 3.0f)
{
    // Per-glyph rectangles allow wide tracking and a low-intensity halo at every output size.
    constexpr float glyphWidth = 5.0f;
    constexpr float glyphHeight = 7.0f;
    constexpr float advance = glyphWidth + 1.0f;
    const std::string value(text);
    const float step = (advance + tracking) * glyphScale;
    const float width = value.empty() ? 0.0f : step * value.size() - tracking * glyphScale;
    const float startX = (kLogicalWidth - width) * 0.5f;
    const float startY = centerY - glyphHeight * glyphScale * 0.5f;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character == ' ') continue;
        const auto* glyph = findIntroGlyph(character);
        if (!glyph) continue;
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph->rows[row] & (1u << (4 - column))) == 0) continue;
                const SDL_FRect pixel{
                    startX + static_cast<float>(index) * step + column * glyphScale,
                    startY + row * glyphScale,
                    glyphScale, glyphScale
                };
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 26);
                const SDL_FRect halo{pixel.x - glyphScale, pixel.y - glyphScale,
                                     pixel.w + glyphScale * 2, pixel.h + glyphScale * 2};
                SDL_RenderFillRect(renderer, &halo);
                SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
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
            drawIntroTextCentered(renderer, 224.0f, "LOADING...", 1.4f, 2.0f);
        } else if (state == IntroState::autosaveNotice) {
            if (configResult.config->autosaveEnabled) {
                drawIntroTextCentered(renderer, 244.0f, "AUTOSAVE IS ENABLED", 1.8f, 3.3f);
            } else {
                drawIntroTextCentered(renderer, 244.0f, "AUTOSAVE IS DISABLED", 1.8f, 3.3f);
            }
        } else if (state == IntroState::legal) {
            drawCentered(renderer, legalTexture);
        } else if (state == IntroState::movies) {
            const SDL_FRect viewport{0.0f, 0.0f, static_cast<float>(kLogicalWidth), static_cast<float>(kLogicalHeight)};
            movie.render(renderer, viewport);
        } else if (state == IntroState::pressStart) {
            drawCentered(renderer, logoTexture);
            drawIntroTextCentered(renderer, 382.0f, "PRESS START BUTTON", 1.2f, 2.0f);
        } else if (state == IntroState::preMenuLoading) {
            if (loadingTexture) drawCentered(renderer, loadingTexture);
            else drawIntroTextCentered(renderer, 224.0f, "LOADING...", 1.4f, 2.0f);
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
