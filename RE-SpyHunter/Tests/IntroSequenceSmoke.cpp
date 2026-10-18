// Integration smoke check with local game assets; no desktop or real audio device.
#include "../IntroSequence.h"
#include "../../GameRuntime/PckArchive.h"
#include "../../GameRuntime/GamepadSet.h"
#include "../../ThirdParty/CLI11/CLI11.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using Intro = spyhunter::game::IntroSequence;
using State = Intro::State;
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
SDL_Event key(SDL_Keycode code, bool repeat = false)
{
    SDL_Event result{};
    result.type = SDL_EVENT_KEY_DOWN;
    result.key.key = code;
    result.key.repeat = repeat;
    return result;
}
void tick(Intro& intro, double delta)
{
    std::string error;
    const bool ok = intro.update(delta, error);
    require(ok, error);
    intro.render();
}
void reachLegal(Intro& intro, bool early)
{
    require(intro.state() == State::loading, "Initial state");
    tick(intro, 0.49);
    require(intro.state() == State::loading, "Boot duration shortened");
    tick(intro, 0.02);
    require(intro.state() == State::autosaveNotice, "Boot -> autosave");
    intro.handleEvent(key(SDLK_RETURN, true));
    require(intro.state() == State::autosaveNotice, "Repeated key dismissed notice");
    if (early) intro.handleEvent(key(SDLK_RETURN));
    else tick(intro, 6.5);
    require(intro.state() == State::autosaveFade, "Autosave -> fade");
    tick(intro, 10.0);
    require(intro.state() == State::autosaveFade, "Fade delta clamp lost");
    for (int i = 0; i < (early ? 3 : 8); ++i) tick(intro, 0.05);
    require(intro.state() == State::autosaveFade, "Fade ended early");
    for (int i = 0; i < 2; ++i) tick(intro, 0.05);
    require(intro.state() == State::legal, "Fade -> legal");
}
void skipUntilTitle(Intro& intro)
{
    const auto deadline = SDL_GetTicks() + 10000;
    while (intro.state() != State::pressStart && SDL_GetTicks() < deadline) {
        intro.handleEvent(key(SDLK_X));
        tick(intro, 0.01);
        SDL_Delay(10); // Cutscene skip fade intentionally uses real elapsed time.
    }
    require(intro.state() == State::pressStart, "Movie skip/return did not reach title");
}
void run(SDL_Renderer* renderer, const std::filesystem::path& root)
{
    std::string error;
    {
        spyhunter::ps2::DevConsoleFont console;
        require(!console.load(renderer, {}, error) && !error.empty(), "Reject incomplete SDK font table");
    }
    {
        spyhunter::runtime::PckArchive archive;
        require(archive.open(root / "DATA" / "INTRO.PCK", error), "Open intro archive: " + error);
        spyhunter::runtime::StillTexture texture;
        require(texture.loadPacked(renderer, archive, "LEGAL_SCREEN.VKI", error), "Packed case-insensitive load");
        require(!texture.loadPacked(renderer, archive, "missing.vki", error) && !error.empty(), "Missing member diagnostic");
        require(texture.loaded(), "Failed replacement lost valid texture");
        texture.drawFit(renderer, {10, 20, 320, 224});
        require(texture.load(renderer, root / "DATA" / "MLOAD.VKI", error), "Standalone still load");
    }
    {
        Intro missing(renderer, root / "nonexistent-smoke-directory", true);
        require(!missing.load(error) && !error.empty(), "Missing assets should fail cleanly");
    }
    {
        Intro disabled(renderer, root, false);
        require(disabled.load(error), "Disabled intro load: " + error);
        reachLegal(disabled, false);
        // Destruction with an active audio device and loaded textures must precede renderer/SDL teardown.
    }
    {
        Intro intro(renderer, root, true);
        require(intro.load(error), "Enabled intro load: " + error);
        reachLegal(intro, true);
        tick(intro, 3.0);
        require(intro.state() == State::legal, "Legal duration shortened");
        tick(intro, 1.0);
        require(intro.state() == State::movies, "Legal -> movies");
        skipUntilTitle(intro);
        tick(intro, 100.0);
        require(intro.state() == State::pressStart, "Idle delta clamp lost");
        for (int i = 0; i < 601 && intro.state() == State::pressStart; ++i) tick(intro, 0.05);
        require(intro.state() == State::attractMovie, "Idle -> attract");
        skipUntilTitle(intro);
        SDL_Event start{};
        start.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        start.gbutton.button = SDL_GAMEPAD_BUTTON_START;
        intro.handleEvent(start);
        require(intro.state() == State::startFade, "Controller Start -> fade");
        for (int i = 0; i < 11; ++i) tick(intro, 0.05);
        require(intro.readyForMenu(), "Start fade -> MLOAD endpoint");
    }
    spyhunter::runtime::GamepadSet pads;
    pads.discover();
    SDL_Event removed{};
    removed.type = SDL_EVENT_GAMEPAD_REMOVED;
    removed.gdevice.which = 0;
    pads.handleEvent(removed); // Removing an unopened device is harmless.
}
}

int main(int argc, char** argv)
{
    std::string assetsArgument;
    CLI::App commandLine{"Run intro-sequence behavior checks using extracted game assets."};
    commandLine.add_option("assets", assetsArgument, "Extracted-binaries root")->required();
    argv = commandLine.ensure_utf8(argv);
    try {
        commandLine.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return commandLine.exit(error) == 0 ? 0 : 2;
    }
    const auto assetsPath = std::filesystem::path(std::u8string(assetsArgument.begin(), assetsArgument.end()));
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return 1;
    int result = 0;
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Intro smoke", 640, 448, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            window ? SDL_CreateRenderer(window.get(), "software") : nullptr, SDL_DestroyRenderer);
        try {
            require(renderer != nullptr, SDL_GetError());
            run(renderer.get(), assetsPath);
            std::cout << "PASS: still loading, failure cleanup, autosave branches, fades/clamps, movie skips, attract return, menu endpoint\n";
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            result = 1;
        }
    }
    SDL_Quit();
    return result;
}
