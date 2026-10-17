#include "FrontendSound.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace spyhunter::runtime
{
namespace
{
std::uint32_t little32(const unsigned char* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t big32(const unsigned char* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

bool readAt(std::ifstream& file, std::uint64_t offset, void* data, std::size_t size)
{
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(file.read(static_cast<char*>(data), static_cast<std::streamsize>(size)));
}
}

FrontendSound::~FrontendSound()
{
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool FrontendSound::loadAccept(const std::filesystem::path& audioPackage, std::string& error)
{
    error.clear();
    std::ifstream file(audioPackage, std::ios::binary | std::ios::ate);
    if (!file) { error = "Could not open AUDIO.MPK."; return false; }
    const auto fileSize = static_cast<std::uint64_t>(file.tellg());
    std::array<unsigned char, 12> package{};
    if (!readAt(file, 0, package.data(), package.size()) ||
        package[0] != 'M' || package[1] != 'P' || package[2] != 'A' || package[3] != 'K') {
        error = "Unsupported AUDIO.MPK header."; return false;
    }
    const auto count = little32(package.data() + 8);
    if (count == 0 || count > 4096 || 12ull + 12ull * count > fileSize) {
        error = "Invalid AUDIO.MPK directory."; return false;
    }
    // The serialized directory label and embedded VAG label do not agree for this entry.
    // Identify the sound by its original VAG header, which says FE_Confirm3.
    std::array<unsigned char, 12> entry{};
    std::array<unsigned char, 48> header{};
    std::uint64_t dataOffset = 0;
    std::uint32_t dataSize = 0, sampleRate = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!readAt(file, 12ull + 12ull * i, entry.data(), entry.size())) break;
        const auto offset = little32(entry.data() + 4);
        const auto size = little32(entry.data() + 8);
        if (offset > fileSize || size < header.size() || size > fileSize - offset ||
            !readAt(file, offset, header.data(), header.size())) continue;
        if (header[0] != 'V' || header[1] != 'A' || header[2] != 'G' || header[3] != 'p') continue;
        constexpr char name[] = "FE_Confirm3";
        bool matches = true;
        for (std::size_t j = 0; j < sizeof(name) - 1; ++j)
            if (header[32 + j] != static_cast<unsigned char>(name[j])) { matches = false; break; }
        if (!matches || header[32 + sizeof(name) - 1] != 0) continue;
        dataOffset = std::uint64_t(offset) + header.size();
        dataSize = big32(header.data() + 12);
        sampleRate = big32(header.data() + 16);
        if (dataSize > size - header.size() || dataSize % 16 != 0 || sampleRate < 8000 || sampleRate > 96000) {
            error = "Invalid FE_Confirm3 VAG format."; return false;
        }
        break;
    }
    if (!dataOffset) { error = "FE_Confirm3 VAG was not found in AUDIO.MPK."; return false; }

    std::vector<unsigned char> encoded(dataSize);
    if (!readAt(file, dataOffset, encoded.data(), encoded.size())) {
        error = "Could not read FE_Confirm3 VAG samples."; return false;
    }
    constexpr int coefficient[5][2]{{0,0}, {60,0}, {115,-52}, {98,-55}, {122,-60}};
    samples_.clear();
    samples_.reserve(dataSize / 16 * 28);
    int previous = 0, beforePrevious = 0;
    for (std::size_t block = 0; block < encoded.size(); block += 16) {
        const int predictor = encoded[block] >> 4;
        const int shift = encoded[block] & 15;
        if (predictor > 4) { error = "Invalid FE_Confirm3 ADPCM predictor."; samples_.clear(); return false; }
        for (int i = 0; i < 28; ++i) {
            const int packed = encoded[block + 2 + i / 2];
            int nibble = (i & 1) ? packed >> 4 : packed & 15;
            if (nibble >= 8) nibble -= 16;
            int sample = (nibble * 4096) >> shift;
            sample += (previous * coefficient[predictor][0] +
                       beforePrevious * coefficient[predictor][1] + 32) >> 6;
            sample = (std::max)(-32768, (std::min)(32767, sample));
            beforePrevious = previous;
            previous = sample;
            samples_.push_back(static_cast<float>(sample) / 65536.0f);
        }
    }
    // ShMSetupState::playSoundAccept passes 0.8 to asSndSetPitch via CreateSound2d.
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, static_cast<int>(sampleRate * 0.8f)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
        error = "Could not open frontend sound output: " + std::string(SDL_GetError()); return false;
    }
    return true;
}

void FrontendSound::playAccept()
{
    if (!stream_ || samples_.empty()) return;
    SDL_ClearAudioStream(stream_);
    SDL_PutAudioStreamData(stream_, samples_.data(), static_cast<int>(samples_.size() * sizeof(float)));
}
}
