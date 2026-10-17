#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../Configuration/SpyHunterConfig.h"
#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/FrontendSound.h"
#include "../GameRuntime/PckArchive.h"
#include "../GameRuntime/Ps2DevConsoleFont.h"
#include "../GameRuntime/VkiImage.h"
#include "../GameRuntime/VkfTextureFont.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 448;

enum class IntroState { loading, autosaveNotice, autosaveFade, legal, movies,
                        pressStart, startFade, attractMovie, preMenuLoading };

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

void drawLoading(SDL_Renderer* renderer, const spyhunter::runtime::Ps2DevConsoleFont& font)
{
    // sceDevCons uses row 22 and 55 leading spaces; captured boot frame refines its screen mapping.
    font.draw(renderer, "LOADING...", 440.0f, 382.0f, 6.5f, 16.0f, 0x40);
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

    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> gamepads;
    const auto openGamepad = [&](SDL_JoystickID id) {
        if (gamepads.contains(id) || !SDL_IsGamepad(id)) return;
        if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
            gamepads.emplace(id, pad);
            SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(pad));
        } else SDL_Log("Could not open gamepad: %s", SDL_GetError());
    };
    int gamepadCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&gamepadCount)) {
        for (int i = 0; i < gamepadCount; ++i) openGamepad(ids[i]);
        SDL_free(ids);
    }

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

    spyhunter::runtime::VkfTextureFont introFont;
    if (!introFont.loadFromPackedArchive(root / "DATA" / "INTRO.PCK", "euroStile14.vkf", renderer, error)) {
        SDL_Log("Intro font load failed: %s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter intro font", error.c_str(), window);
        SDL_DestroyTexture(legalTexture);
        SDL_DestroyTexture(logoTexture);
        if (loadingTexture) SDL_DestroyTexture(loadingTexture);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_Log("Loaded euroStile14.vkf from INTRO.PCK for intro text.");

    spyhunter::runtime::Ps2DevConsoleFont consoleFont;
    if (!consoleFont.load(renderer, root / "SLUS_200.56", error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter console font", error.c_str(), window);
        introFont.unload();
        SDL_DestroyTexture(legalTexture); SDL_DestroyTexture(logoTexture);
        if (loadingTexture) SDL_DestroyTexture(loadingTexture);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    const std::vector<std::filesystem::path> movies{
        root / "MOVIES" / "M_LOGO.PSS",
        root / "MOVIES" / "PEILOGO.PSS",
        root / "MOVIES" / "INTRO.PSS"
    };
    const auto attractMovie = root / "MOVIES" / "SPY_DEMO.PSS";
    spyhunter::runtime::FrontendSound frontendSound;
    if (!frontendSound.loadAccept(root / "DATA" / "AUDIO.MPK", error)) {
        SDL_Log("Frontend accept sound unavailable: %s", error.c_str());
    }
    spyhunter::runtime::CutsceneSession movie;
    std::size_t movieIndex = 0;
    IntroState state = IntroState::loading;
    double stateSeconds = 0.0;
    double fadeDuration = 0.5;
    auto previous = SDL_GetTicksNS();
    bool running = true;
    const auto acceptTitle = [&] {
        if (state != IntroState::pressStart) return;
        frontendSound.playAccept();
        state = IntroState::startFade;
        stateSeconds = 0.0;
        fadeDuration = 0.5;
        SDL_Log("Start pressed: fading toward MLOAD.VKI.");
    };
    const auto acceptAutosave = [&] {
        if (state != IntroState::autosaveNotice) return;
        frontendSound.playAccept();
        state = IntroState::autosaveFade;
        stateSeconds = 0.0;
        fadeDuration = 0.25;
    };
    const auto skipMovie = [&] {
        if (state == IntroState::movies || state == IntroState::attractMovie) movie.requestSkip();
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) openGamepad(event.gdevice.which);
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (const auto found = gamepads.find(event.gdevice.which); found != gamepads.end()) {
                    SDL_CloseGamepad(found->second);
                    gamepads.erase(found);
                    SDL_Log("Gamepad disconnected.");
                }
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && gamepads.contains(event.gbutton.which)) {
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    if (state == IntroState::attractMovie) skipMovie();
                    else acceptTitle();
                } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    if (state == IntroState::autosaveNotice) acceptAutosave();
                    else skipMovie();
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (state == IntroState::pressStart &&
                    (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE || event.key.key == SDLK_S)) acceptTitle();
                if ((state == IntroState::movies && event.key.key == SDLK_X) ||
                    (state == IntroState::attractMovie &&
                     (event.key.key == SDLK_X || event.key.key == SDLK_RETURN))) skipMovie();
                if (state == IntroState::autosaveNotice &&
                    (event.key.key == SDLK_X || event.key.key == SDLK_RETURN)) acceptAutosave();
            }
            if (state == IntroState::pressStart && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) acceptTitle();
        }

        const auto now = SDL_GetTicksNS();
        const double delta = static_cast<double>(now - previous) / 1'000'000'000.0;
        previous = now;
        // The original uses wall time for still screens, but clamps idle and fade updates to 0.05 s.
        stateSeconds += (state == IntroState::pressStart || state == IntroState::startFade ||
                         state == IntroState::autosaveFade) ? (std::min)(delta, 0.05) : delta;

        if (state == IntroState::loading && stateSeconds >= 0.5) {
            state = IntroState::autosaveNotice; stateSeconds = 0.0;
        } else if (state == IntroState::autosaveNotice && stateSeconds >= 6.5) {
            state = IntroState::autosaveFade; stateSeconds = 0.0; fadeDuration = 0.5;
        } else if (state == IntroState::autosaveFade && stateSeconds >= fadeDuration) {
            state = IntroState::legal; stateSeconds = 0.0;
        } else if (state == IntroState::legal && stateSeconds >= 4.0) {
            movieIndex = 0;
            if (!movie.load(renderer, {movies[movieIndex], true, true}, error)) {
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
                } else if (!movie.load(renderer, {movies[movieIndex], true, true}, error)) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter movie", error.c_str(), window);
                    running = false;
                }
            }
        } else if (state == IntroState::pressStart && stateSeconds > 30.0) {
            if (!movie.load(renderer, {attractMovie, true, true}, error)) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RE::SpyHunter attract movie", error.c_str(), window);
                running = false;
            } else { state = IntroState::attractMovie; stateSeconds = 0.0; }
        } else if (state == IntroState::attractMovie) {
            movie.update();
            if (movie.hasFinished()) {
                movie.stop();
                state = IntroState::pressStart; stateSeconds = 0.0;
            }
        } else if (state == IntroState::startFade && stateSeconds >= fadeDuration) {
            state = IntroState::preMenuLoading; stateSeconds = 0.0;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (state == IntroState::loading) {
            drawLoading(renderer, consoleFont);
        } else if (state == IntroState::autosaveNotice || state == IntroState::autosaveFade) {
            if (configResult.config->autosaveEnabled) {
                introFont.drawText(renderer, "A U T O S A V E   I S   E N A B L E D", 100.0f, 200.0f,
                                   static_cast<float>(kLogicalHeight), 0.8f);
            } else {
                introFont.drawText(renderer, "A U T O S A V E   I S   D I S A B L E D", 100.0f, 200.0f,
                                   static_cast<float>(kLogicalHeight), 0.8f);
            }
        } else if (state == IntroState::legal) {
            drawCentered(renderer, legalTexture);
        } else if (state == IntroState::movies || state == IntroState::attractMovie) {
            const SDL_FRect viewport{0.0f, 0.0f, static_cast<float>(kLogicalWidth), static_cast<float>(kLogicalHeight)};
            movie.render(renderer, viewport);
        } else if (state == IntroState::pressStart || state == IntroState::startFade) {
            drawCentered(renderer, logoTexture);
            introFont.drawText(renderer, "P R E S S  S T A R T  B U T T O N", 130.0f, 65.0f,
                               static_cast<float>(kLogicalHeight), 0.8f);
        } else if (state == IntroState::preMenuLoading) {
            if (loadingTexture) drawCentered(renderer, loadingTexture);
            else drawLoading(renderer, consoleFont);
        }
        if (state == IntroState::autosaveFade || state == IntroState::startFade) {
            // ShGraph's (0,1), (0.5,0.5), (1,0) points evaluate 2 * remaining time.
            const auto opacity = static_cast<Uint8>(255.0 * (std::clamp)(
                1.0 - 2.0 * (fadeDuration - stateSeconds), 0.0, 1.0));
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, opacity);
            SDL_RenderFillRect(renderer, nullptr);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    movie.stop();
    for (auto& [id, pad] : gamepads) SDL_CloseGamepad(pad);
    if (legalTexture) SDL_DestroyTexture(legalTexture);
    if (logoTexture) SDL_DestroyTexture(logoTexture);
    if (loadingTexture) SDL_DestroyTexture(loadingTexture);
    introFont.unload();
    consoleFont.unload();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
