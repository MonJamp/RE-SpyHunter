#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
enum class AudioReadResult { data, pending, complete, failed };

// Reads Sony PCM packets from a PSS file without retaining the movie or the
// complete audio track in memory. One returned packet is at most 65,535 bytes.
class PssAudioReader
{
public:
    bool open(const std::filesystem::path& source, std::string& error);
    AudioReadResult next(std::vector<std::uint8_t>& audio, std::string& error,
                         std::uint64_t scanBudget = 256 * 1024);

    std::uint64_t sourceBytesConsumed() const { return sourceBytesConsumed_; }
    std::uint32_t declaredAudioBytes() const { return declaredAudioBytes_; }
    std::uint32_t interleaveBytes() const { return interleaveBytes_; }
    std::uint32_t audioBytesRead() const { return audioBytesRead_; }

private:
    int readByte();
    bool readPacketBytes(std::size_t count);

    std::ifstream input_;
    std::array<char, 64 * 1024> buffer_{};
    std::size_t cursor_ = 0;
    std::size_t available_ = 0;
    std::vector<std::uint8_t> packet_;
    std::uint32_t marker_ = 0xffffffff;
    std::uint32_t declaredAudioBytes_ = 0;
    std::uint32_t interleaveBytes_ = 0;
    std::uint32_t audioBytesRead_ = 0;
    std::uint64_t sourceBytesConsumed_ = 0;
};
}
