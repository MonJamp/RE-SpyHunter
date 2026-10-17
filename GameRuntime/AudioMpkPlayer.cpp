#include "AudioMpkPlayer.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace spyhunter::runtime {
namespace {
std::uint32_t little32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint32_t big32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
        (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
#include "Mpc3StepTable.inc"
}

AudioMpkPlayer::~AudioMpkPlayer() { stop(); }
void AudioMpkPlayer::stop() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    file_.close();
    remaining_ = frameWordsLeft_ = 0;
    error_.clear();
}
bool AudioMpkPlayer::isPlaying() const {
    return stream_ && (remaining_ || SDL_GetAudioStreamQueued(stream_) > 0);
}
bool AudioMpkPlayer::play(const AudioMpk& archive, size_t index, std::string& details, std::string& error) {
    stop();
    if (index >= archive.entries().size()) { error = "MPK entry index is out of range."; return false; }
    const auto& entry = archive.entries()[index];
    if (entry.size < 28) { error = "Audio entry is truncated."; return false; }
    file_.open(archive.path(), std::ios::binary);
    if (!file_) { error = "Could not open AUDIO.MPK for streaming."; return false; }
    std::array<std::uint8_t, 48> header{};
    file_.seekg(entry.offset);
    const auto headerSize = (std::min)(size_t{48}, static_cast<size_t>(entry.size));
    if (!file_.read(reinterpret_cast<char*>(header.data()), headerSize)) {
        error = "Could not read audio header."; stop(); return false;
    }
    const bool vag = !std::memcmp(header.data(), "VAGp", 4);
    mc3_ = !vag;
    std::uint32_t encodedSize = 0;
    if (vag) {
        encodedSize = big32(header.data() + 12);
        rate_ = static_cast<int>(big32(header.data() + 16));
        channels_ = 1;
        if (entry.size < 48 || !encodedSize || encodedSize % 16 || encodedSize > entry.size - 48) {
            error = "Invalid VAG data bounds."; stop(); return false;
        }
        remaining_ = encodedSize / 16;
        cursor_ = std::uint64_t(entry.offset) + 48;
        details = "VAG PS2 ADPCM";
    } else if (!std::memcmp(header.data(), "MPC3", 4) && big32(header.data() + 4) == 0x00011400) {
        channels_ = static_cast<int>(little32(header.data() + 8));
        rate_ = static_cast<int>(little32(header.data() + 12));
        remaining_ = little32(header.data() + 16);
        wordsPerBlock_ = little32(header.data() + 20);
        encodedSize = little32(header.data() + 24);
        if ((channels_ != 1 && channels_ != 2) || !remaining_ || !wordsPerBlock_ ||
            encodedSize > entry.size - 28 || 4ull + 4ull * wordsPerBlock_ * channels_ > encodedSize) {
            error = "Invalid MPC3 data bounds."; stop(); return false;
        }
        cursor_ = std::uint64_t(entry.offset) + 28;
        details = "Paradigm MPC3 ADPCM";
    } else { error = "Unsupported MPK audio header."; stop(); return false; }
    if (rate_ < 8000 || rate_ > 96000) { error = "Invalid audio sample rate."; stop(); return false; }
    dataEnd_ = cursor_ + encodedSize;
    history_ = step_ = {};
    frameWordsLeft_ = framePadding_ = 0;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, channels_, rate_};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
        error = "Could not open audio playback: " + std::string(SDL_GetError()); stop(); return false;
    }
    details += " / " + std::to_string(rate_) + " Hz / " + std::to_string(channels_) +
        " channel(s) / streamed raw archive playback";
    error.clear();
    update();
    if (!error_.empty()) { error = error_; stop(); return false; }
    return true;
}
void AudioMpkPlayer::update() {
    if (!stream_ || !remaining_ || !error_.empty()) return;
    // Decode only when SDL's queued PCM drops below half a second.
    const int queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0) { error_ = SDL_GetError(); return; }
    if (queued < rate_ * channels_ * static_cast<int>(sizeof(float)) / 2) refill();
}
bool AudioMpkPlayer::refill() {
    std::vector<float> samples;
    samples.reserve(4096 * channels_);
    if (!mc3_) {
        constexpr int coefficient[5][2]{{0,0}, {60,0}, {115,-52}, {98,-55}, {122,-60}};
        for (int block = 0; block < 128 && remaining_; ++block) {
            std::array<std::uint8_t, 16> data{};
            file_.seekg(cursor_);
            if (cursor_ + data.size() > dataEnd_ ||
                !file_.read(reinterpret_cast<char*>(data.data()), data.size())) {
                error_ = "Truncated VAG stream."; return false;
            }
            cursor_ += data.size(); --remaining_;
            const int predictor = data[0] >> 4, shift = data[0] & 15;
            if (predictor > 4) { error_ = "Invalid VAG predictor."; return false; }
            for (int i = 0; i < 28; ++i) {
                const int packed = data[2 + i / 2];
                int nibble = (i & 1) ? packed >> 4 : packed & 15;
                if (nibble >= 8) nibble -= 16;
                int sample = (nibble * 4096) >> shift;
                sample += (history_[0] * coefficient[predictor][0] + history_[1] * coefficient[predictor][1] + 32) >> 6;
                sample = (std::clamp)(sample, -32768, 32767);
                history_[1] = history_[0]; history_[0] = sample;
                samples.push_back(static_cast<float>(sample) / 32768.0f);
            }
        }
    } else {
        constexpr int stepIndexes[4]{5, 1, -1, -3};
        while (remaining_ && samples.size() < 4096u * channels_) {
            if (!frameWordsLeft_) {
                std::array<std::uint8_t, 4> header{};
                file_.seekg(cursor_);
                if (cursor_ + 4 > dataEnd_ || !file_.read(reinterpret_cast<char*>(header.data()), 4)) {
                    error_ = "Truncated MPC3 frame header."; return false;
                }
                cursor_ += 4;
                const auto value = little32(header.data());
                for (int channel = 0; channel < channels_; ++channel) {
                    const int initial = (value >> (channel * 16)) & 0xffff;
                    history_[channel] = static_cast<std::int16_t>(initial & 0xffc0);
                    step_[channel] = initial & 63;
                }
                frameWordsLeft_ = (std::min)(remaining_, wordsPerBlock_);
                framePadding_ = wordsPerBlock_ - frameWordsLeft_;
            }
            std::array<std::uint8_t, 8> data{};
            const size_t size = 4u * channels_;
            file_.seekg(cursor_);
            if (cursor_ + size > dataEnd_ || !file_.read(reinterpret_cast<char*>(data.data()), size)) {
                error_ = "Truncated MPC3 data."; return false;
            }
            cursor_ += size;
            for (int sample = 0; sample < 10; ++sample) {
                for (int channel = 0; channel < channels_; ++channel) {
                    const auto packed = little32(data.data() + channel * 4);
                    const int mode = packed >> 30;
                    const int code = (packed >> (sample * 3)) & 7;
                    const int index = code & 3;
                    const int diff = step_sizes[mode][index][step_[channel]];
                    history_[channel] += (code & 4) ? diff : -1 - diff;
                    step_[channel] = (std::clamp)(step_[channel] + stepIndexes[index], 0, 63);
                    samples.push_back(static_cast<float>(static_cast<std::int16_t>(history_[channel])) / 32768.0f);
                }
            }
            --remaining_; --frameWordsLeft_;
            if (!frameWordsLeft_) cursor_ += 4ull * framePadding_ * channels_;
        }
    }
    if (!samples.empty() && !SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)))) {
        error_ = "Could not queue decoded audio: " + std::string(SDL_GetError()); return false;
    }
    return true;
}
}
