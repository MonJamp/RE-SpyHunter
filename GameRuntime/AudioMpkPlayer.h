#pragma once
#include "AudioMpk.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace spyhunter::runtime {
class AudioMpkPlayer {
public:
    ~AudioMpkPlayer();
    bool play(const AudioMpk& archive, size_t index, std::string& details, std::string& error);
    void update();
    void stop();
    bool isPlaying() const;
    const std::string& error() const { return error_; }
private:
    bool refill();
    std::ifstream file_;
    SDL_AudioStream* stream_ = nullptr;
    std::uint64_t dataEnd_ = 0, cursor_ = 0;
    std::uint32_t remaining_ = 0, wordsPerBlock_ = 0, frameWordsLeft_ = 0, framePadding_ = 0;
    std::array<int, 2> history_{}, step_{};
    int channels_ = 0, rate_ = 0;
    bool mc3_ = false;
    std::string error_;
};
}
