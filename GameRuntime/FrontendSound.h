#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
class FrontendSound
{
public:
    ~FrontendSound();
    bool loadAccept(const std::filesystem::path& audioPackage, std::string& error);
    void playAccept();

private:
    SDL_AudioStream* stream_ = nullptr;
    std::vector<float> samples_;
};
}
