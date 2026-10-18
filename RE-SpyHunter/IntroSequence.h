#pragma once

#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/FrontendSound.h"
#include "../PS2Compatibility/DevConsoleFont.h"
#include "../GameRuntime/StillTexture.h"
#include "../GameRuntime/VkfTextureFont.h"
#include <array>

namespace spyhunter::game
{
// Game policy and recovered presentation through pre-menu loading.
// Own one instance per run; renderer and SDL must outlive it.
class IntroSequence
{
public:
    static constexpr int logicalWidth = 640;
    static constexpr int logicalHeight = 448;
    enum class State { loading, autosaveNotice, autosaveFade, legal, movies,
                       pressStart, startFade, attractMovie, preMenuLoading };

    IntroSequence(SDL_Renderer* renderer, const std::filesystem::path& root, bool autosaveEnabled);
    IntroSequence(const IntroSequence&) = delete;
    IntroSequence& operator=(const IntroSequence&) = delete;
    bool load(std::string& error);
    // Application filters disconnected gamepads and handles quit/window events.
    void handleEvent(const SDL_Event& event);
    bool update(double deltaSeconds, std::string& error);
    void render() const;
    State state() const { return state_; }
    // The current endpoint, not a claim that profile/menu is implemented.
    bool readyForMenu() const { return state_ == State::preMenuLoading; }

private:
    void enter(State state);
    void acceptTitle();
    void acceptAutosave();
    void skipMovie();
    void drawLoading() const;

    SDL_Renderer* renderer_;
    std::filesystem::path root_;
    bool autosaveEnabled_;
    runtime::StillTexture legal_, logo_, loading_;
    runtime::VkfTextureFont introFont_;
    ps2::DevConsoleFont consoleFont_;
    runtime::FrontendSound frontendSound_;
    runtime::CutsceneSession movie_;
    std::array<std::filesystem::path, 3> movies_;
    std::filesystem::path attractMovie_;
    std::size_t movieIndex_ = 0;
    State state_ = State::loading;
    double stateSeconds_ = 0.0;
    double fadeDuration_ = 0.5;
};
}
