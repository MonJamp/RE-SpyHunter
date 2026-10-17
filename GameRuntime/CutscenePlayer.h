#pragma once

#include <filesystem>
#include <memory>
#include <string>

struct SDL_Renderer;
struct SDL_FRect;

namespace spyhunter::runtime
{
enum class CutsceneResult
{
    completed,
    skipped,
    failed
};

struct CutscenePlayback
{
    CutsceneResult result = CutsceneResult::failed;
    std::string error;
};

struct CutsceneOptions
{
    std::filesystem::path sourcePath;
    bool escapeSkips = true;
};

// Incremental PSS playback for applications that own their own event loop and
// UI. Call update once per frame and render into the supplied viewport.
class CutsceneSession
{
public:
    CutsceneSession();
    ~CutsceneSession();

    CutsceneSession(CutsceneSession&&) noexcept;
    CutsceneSession& operator=(CutsceneSession&&) noexcept;
    CutsceneSession(const CutsceneSession&) = delete;
    CutsceneSession& operator=(const CutsceneSession&) = delete;

    bool load(SDL_Renderer* renderer, const CutsceneOptions& options, std::string& error);
    void update();
    void render(SDL_Renderer* renderer, const SDL_FRect& viewport) const;
    void requestSkip();
    void stop();

    bool isPlaying() const;
    bool hasFinished() const;
    const std::string& error() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
    bool finished_ = false;
    std::string error_;
};

// Plays one PS2 PSS cutscene using an existing SDL renderer. The call pumps
// window events and returns when playback completes, is skipped, or fails.
CutscenePlayback playCutscene(SDL_Renderer* renderer, const CutsceneOptions& options);
}
