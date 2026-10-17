#pragma once

#include <filesystem>
#include <string>

struct SDL_Renderer;

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
    std::filesystem::path workingDirectory;
    bool escapeSkips = true;
};

// Plays one PS2 PSS cutscene using an existing SDL renderer. The call pumps
// window events and returns when playback completes, is skipped, or fails.
CutscenePlayback playCutscene(SDL_Renderer* renderer, const CutsceneOptions& options);
}
