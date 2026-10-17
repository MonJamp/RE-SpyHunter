#include "AudioMpkPlayer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
std::uint32_t little32(const std::uint8_t* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t big32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

struct DecodedAudio
{
    std::vector<float> samples;
    int channels = 0;
    int rate = 0;
    std::string codec;
};

bool decodeVag(const std::vector<std::uint8_t>& bytes, DecodedAudio& audio, std::string& error)
{
    if (bytes.size() < 48 || std::memcmp(bytes.data(), "VAGp", 4)) {
        error = "Not a VAGp sound."; return false;
    }
    const auto encodedSize = big32(bytes.data() + 12);
    const auto rate = big32(bytes.data() + 16);
    if (!encodedSize || encodedSize % 16 || encodedSize > bytes.size() - 48 || rate < 8000 || rate > 96000 ||
        encodedSize / 16ull * 28ull > 64ull * 1024 * 1024) {
        error = "Invalid VAG size or sample rate."; return false;
    }
    constexpr int coefficient[5][2]{{0,0}, {60,0}, {115,-52}, {98,-55}, {122,-60}};
    audio.samples.reserve(encodedSize / 16 * 28);
    int previous = 0, beforePrevious = 0;
    for (size_t block = 48; block < 48ull + encodedSize; block += 16) {
        const int predictor = bytes[block] >> 4;
        const int shift = bytes[block] & 15;
        if (predictor > 4) { error = "Invalid VAG ADPCM predictor."; return false; }
        for (int i = 0; i < 28; ++i) {
            const int packed = bytes[block + 2 + i / 2];
            int nibble = (i & 1) ? packed >> 4 : packed & 15;
            if (nibble >= 8) nibble -= 16;
            int sample = (nibble * 4096) >> shift;
            sample += (previous * coefficient[predictor][0] + beforePrevious * coefficient[predictor][1] + 32) >> 6;
            sample = (std::clamp)(sample, -32768, 32767);
            beforePrevious = previous;
            previous = sample;
            audio.samples.push_back(static_cast<float>(sample) / 32768.0f);
        }
    }
    audio.channels = 1;
    audio.rate = static_cast<int>(rate);
    audio.codec = "VAG PS2 ADPCM";
    return true;
}

#include "Mpc3StepTable.inc"

bool decodeMc3(const std::vector<std::uint8_t>& bytes, DecodedAudio& audio, std::string& error)
{
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "MPC3", 4) || big32(bytes.data() + 4) != 0x00011400) {
        error = "Unsupported MPC3 header."; return false;
    }
    const auto channels = little32(bytes.data() + 8);
    const auto rate = little32(bytes.data() + 12);
    const auto subblockCount = little32(bytes.data() + 16);
    const auto wordsPerBlock = little32(bytes.data() + 20);
    const auto dataSize = little32(bytes.data() + 24);
    const std::uint64_t frameBytes = 4ull + 4ull * wordsPerBlock * channels;
    if ((channels != 1 && channels != 2) || rate < 8000 || rate > 96000 ||
        !subblockCount || !wordsPerBlock || frameBytes > dataSize || dataSize > bytes.size() - 28 ||
        10ull * subblockCount * channels > 64ull * 1024 * 1024) {
        error = "Invalid MPC3 channels, rate, or data bounds."; return false;
    }
    audio.samples.reserve(static_cast<size_t>(10ull * subblockCount * channels));
    constexpr int stepIndexes[4]{5, 1, -1, -3};
    std::uint32_t remaining = subblockCount;
    size_t frameOffset = 28;
    while (remaining) {
        const auto words = (std::min)(remaining, wordsPerBlock);
        if (frameOffset + 4ull + 4ull * words * channels > 28ull + dataSize) {
            error = "Truncated MPC3 block."; return false;
        }
        const auto header = little32(bytes.data() + frameOffset);
        std::array<int, 2> history{}, step{};
        for (size_t channel = 0; channel < channels; ++channel) {
            const int value = (header >> (channel * 16)) & 0xffff;
            history[channel] = static_cast<std::int16_t>(value & 0xffc0);
            step[channel] = value & 63;
        }
        for (std::uint32_t word = 0; word < words; ++word) {
            std::array<std::uint32_t, 2> packed{};
            for (size_t channel = 0; channel < channels; ++channel)
                packed[channel] = little32(bytes.data() + frameOffset + 4ull + 4ull * (word * channels + channel));
            for (int sample = 0; sample < 10; ++sample) {
                for (size_t channel = 0; channel < channels; ++channel) {
                    const int mode = packed[channel] >> 30;
                    const int code = (packed[channel] >> (sample * 3)) & 7;
                    const int index = code & 3;
                    const int diff = step_sizes[mode][index][step[channel]];
                    history[channel] += (code & 4) ? diff : -1 - diff;
                    step[channel] = (std::clamp)(step[channel] + stepIndexes[index], 0, 63);
                    audio.samples.push_back(static_cast<float>(static_cast<std::int16_t>(history[channel])) / 32768.0f);
                }
            }
        }
        remaining -= words;
        frameOffset += static_cast<size_t>(frameBytes);
    }
    audio.channels = static_cast<int>(channels);
    audio.rate = static_cast<int>(rate);
    audio.codec = "Paradigm MPC3 ADPCM";
    return true;
}
}

AudioMpkPlayer::~AudioMpkPlayer() { stop(); }

void AudioMpkPlayer::stop()
{
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

bool AudioMpkPlayer::isPlaying() const
{
    return stream_ && SDL_GetAudioStreamQueued(stream_) > 0;
}

bool AudioMpkPlayer::play(const std::vector<std::uint8_t>& bytes, std::string& details, std::string& error)
{
    stop();
    DecodedAudio decoded;
    const bool okay = bytes.size() >= 4 && !std::memcmp(bytes.data(), "VAGp", 4)
        ? decodeVag(bytes, decoded, error) : decodeMc3(bytes, decoded, error);
    if (!okay) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, decoded.channels, decoded.rate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_) ||
        !SDL_PutAudioStreamData(stream_, decoded.samples.data(), static_cast<int>(decoded.samples.size() * sizeof(float)))) {
        error = "Could not play audio: " + std::string(SDL_GetError()); stop(); return false;
    }
    details = decoded.codec + " / " + std::to_string(decoded.rate) + " Hz / " +
              std::to_string(decoded.channels) + " channel(s) / raw archive playback";
    error.clear();
    return true;
}
}
