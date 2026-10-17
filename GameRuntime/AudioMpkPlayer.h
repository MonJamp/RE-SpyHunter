#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
class AudioMpkPlayer
{
public:
    ~AudioMpkPlayer();
    bool play(const std::vector<std::uint8_t>& bytes, std::string& details, std::string& error);
    void stop();
    bool isPlaying() const;

private:
    SDL_AudioStream* stream_ = nullptr;
};
}
