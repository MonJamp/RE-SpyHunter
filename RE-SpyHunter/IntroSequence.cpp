#include "IntroSequence.h"
#include "../GameRuntime/PckArchive.h"
#include <algorithm>
#include <fstream>

namespace spyhunter::game
{
IntroSequence::IntroSequence(SDL_Renderer* renderer, const std::filesystem::path& root, bool autosaveEnabled)
    : renderer_(renderer), root_(root), autosaveEnabled_(autosaveEnabled),
      movies_{root / "MOVIES" / "M_LOGO.PSS", root / "MOVIES" / "PEILOGO.PSS", root / "MOVIES" / "INTRO.PSS"},
      attractMovie_(root / "MOVIES" / "SPY_DEMO.PSS") {}

bool IntroSequence::load(std::string& error)
{
    // Share one index between the two still loads instead of scanning INTRO.PCK twice.
    runtime::PckArchive archive;
    if (!archive.open(root_ / "DATA" / "INTRO.PCK", error) ||
        !legal_.loadPacked(renderer_, archive, "legal_screen.vki", error) ||
        !logo_.loadPacked(renderer_, archive, "menu_logoscreen.vki", error)) return false;
    if (!loading_.load(renderer_, root_ / "DATA" / "MLOAD.VKI", error))
        SDL_Log("MLOAD.VKI unavailable for the next state: %s", error.c_str());
    if (!introFont_.loadFromPackedArchive(root_ / "DATA" / "INTRO.PCK", "euroStile14.vkf", renderer_, error))
        return false;
    SDL_Log("Loaded euroStile14.vkf from INTRO.PCK for intro text.");
    // Target-specific location stays in the game; PS2Compatibility only consumes SDK font bytes.
    // SLUS_200.56 sceFont8 VA 0x004699C8 -> file offset 0x0036A9C8 (intro property audit).
    std::ifstream elf(root_ / "SLUS_200.56", std::ios::binary);
    std::array<std::uint8_t, 2048> consoleGlyphs{};
    if (!elf.seekg(0x0036A9C8) ||
        !elf.read(reinterpret_cast<char*>(consoleGlyphs.data()), consoleGlyphs.size())) {
        error = "Cannot read complete sceFont8 from " + (root_ / "SLUS_200.56").string();
        return false;
    }
    if (!consoleFont_.load(renderer_, consoleGlyphs, error)) return false;
    if (!frontendSound_.loadAccept(root_ / "DATA" / "AUDIO.MPK", error))
        SDL_Log("Frontend accept sound unavailable: %s", error.c_str());
    error.clear();
    return true;
}

void IntroSequence::enter(State state)
{
    state_ = state;
    stateSeconds_ = 0.0;
}

void IntroSequence::acceptTitle()
{
    if (state_ != State::pressStart) return;
    frontendSound_.playAccept();
    enter(State::startFade);
    fadeDuration_ = 0.5;
    SDL_Log("Start pressed: fading toward MLOAD.VKI.");
}

void IntroSequence::acceptAutosave()
{
    if (state_ != State::autosaveNotice) return;
    frontendSound_.playAccept();
    enter(State::autosaveFade);
    fadeDuration_ = 0.25;
}

void IntroSequence::skipMovie()
{
    if (state_ == State::movies || state_ == State::attractMovie) movie_.requestSkip();
}

void IntroSequence::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            if (state_ == State::attractMovie) skipMovie();
            else acceptTitle();
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (state_ == State::autosaveNotice) acceptAutosave();
            else skipMovie();
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (state_ == State::pressStart &&
            (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE || event.key.key == SDLK_S)) acceptTitle();
        if ((state_ == State::movies && event.key.key == SDLK_X) ||
            (state_ == State::attractMovie && (event.key.key == SDLK_X || event.key.key == SDLK_RETURN))) skipMovie();
        if (state_ == State::autosaveNotice && (event.key.key == SDLK_X || event.key.key == SDLK_RETURN)) acceptAutosave();
    }
    if (state_ == State::pressStart && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT) acceptTitle();
}

bool IntroSequence::update(double deltaSeconds, std::string& error)
{
    // Preserve the original distinction: wall-time stills, clamped idle/fade updates.
    stateSeconds_ += (state_ == State::pressStart || state_ == State::startFade || state_ == State::autosaveFade)
        ? (std::min)(deltaSeconds, 0.05) : deltaSeconds;
    if (state_ == State::loading && stateSeconds_ >= 0.5) {
        enter(State::autosaveNotice);
    } else if (state_ == State::autosaveNotice && stateSeconds_ >= 6.5) {
        enter(State::autosaveFade);
        fadeDuration_ = 0.5;
    } else if (state_ == State::autosaveFade && stateSeconds_ >= fadeDuration_) {
        enter(State::legal);
    } else if (state_ == State::legal && stateSeconds_ >= 4.0) {
        movieIndex_ = 0;
        if (!movie_.load(renderer_, {movies_[movieIndex_], true, true}, error)) return false;
        enter(State::movies);
    } else if (state_ == State::movies) {
        movie_.update();
        if (movie_.hasFinished()) {
            movie_.stop();
            if (++movieIndex_ == movies_.size()) enter(State::pressStart);
            else if (!movie_.load(renderer_, {movies_[movieIndex_], true, true}, error)) return false;
        }
    } else if (state_ == State::pressStart && stateSeconds_ > 30.0) {
        if (!movie_.load(renderer_, {attractMovie_, true, true}, error)) return false;
        enter(State::attractMovie);
    } else if (state_ == State::attractMovie) {
        movie_.update();
        if (movie_.hasFinished()) {
            movie_.stop();
            enter(State::pressStart);
        }
    } else if (state_ == State::startFade && stateSeconds_ >= fadeDuration_) {
        enter(State::preMenuLoading);
    }
    return true;
}

void IntroSequence::drawLoading() const
{
    // Original row/spaces refined by capture; exact GS transform remains unresolved.
    consoleFont_.draw(renderer_, "LOADING...", 440.0f, 382.0f, 6.5f, 16.0f, 0x40);
}

void IntroSequence::render() const
{
    const SDL_FRect viewport{0, 0, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight)};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    switch (state_) {
    case State::loading: drawLoading(); break;
    case State::autosaveNotice:
    case State::autosaveFade:
        introFont_.drawText(renderer_, autosaveEnabled_ ? "A U T O S A V E   I S   E N A B L E D"
            : "A U T O S A V E   I S   D I S A B L E D", 100.0f, 200.0f, logicalHeight, 0.8f);
        break;
    case State::legal: legal_.drawFit(renderer_, viewport); break;
    case State::movies:
    case State::attractMovie: movie_.render(renderer_, viewport); break;
    case State::pressStart:
    case State::startFade:
        logo_.drawFit(renderer_, viewport);
        introFont_.drawText(renderer_, "P R E S S  S T A R T  B U T T O N", 130.0f, 65.0f, logicalHeight, 0.8f);
        break;
    case State::preMenuLoading:
        if (loading_.loaded()) loading_.drawFit(renderer_, viewport);
        else drawLoading();
        break;
    }
    if (state_ == State::autosaveFade || state_ == State::startFade) {
        // ShGraph evaluates 2 * remaining time, so a 0.25 s fade starts half opaque.
        const auto opacity = static_cast<Uint8>(255.0 * (std::clamp)(
            1.0 - 2.0 * (fadeDuration_ - stateSeconds_), 0.0, 1.0));
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, opacity);
        SDL_RenderFillRect(renderer_, nullptr);
    }
}
}
