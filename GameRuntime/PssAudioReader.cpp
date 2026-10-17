#include "PssAudioReader.h"

#include <algorithm>

namespace spyhunter::runtime
{
bool PssAudioReader::open(const std::filesystem::path& source, std::string& error)
{
    input_.open(source, std::ios::binary);
    if (!input_) { error = "Could not open the PSS movie for audio."; return false; }
    return true;
}

int PssAudioReader::readByte()
{
    if (cursor_ == available_) {
        input_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        available_ = static_cast<std::size_t>(input_.gcount());
        cursor_ = 0;
        if (available_ == 0) return -1;
    }
    ++sourceBytesConsumed_;
    return static_cast<std::uint8_t>(buffer_[cursor_++]);
}

bool PssAudioReader::readPacketBytes(const std::size_t count)
{
    packet_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const int value = readByte();
        if (value < 0) return false;
        packet_[index] = static_cast<std::uint8_t>(value);
    }
    return true;
}

AudioReadResult PssAudioReader::next(std::vector<std::uint8_t>& audio, std::string& error,
                                    const std::uint64_t scanBudget)
{
    audio.clear();
    if (declaredAudioBytes_ != 0 && audioBytesRead_ == declaredAudioBytes_) return AudioReadResult::complete;

    const std::uint64_t scanStart = sourceBytesConsumed_;
    while (sourceBytesConsumed_ - scanStart < scanBudget) {
        const int value = readByte();
        if (value < 0) {
            if (input_.bad()) error = "Reading the PSS audio stream failed.";
            else if (declaredAudioBytes_ == 0) error = "Sony PCM audio header was not found.";
            else error = "Sony PCM audio extraction was incomplete.";
            return AudioReadResult::failed;
        }
        marker_ = (marker_ << 8) | static_cast<std::uint8_t>(value);
        if (marker_ != 0x000001bd) continue;
        marker_ = 0xffffffff;

        const int lengthHigh = readByte();
        const int lengthLow = readByte();
        if (lengthHigh < 0 || lengthLow < 0) {
            error = "The PSS audio packet length is truncated.";
            return AudioReadResult::failed;
        }
        const std::size_t packetLength = (static_cast<std::size_t>(lengthHigh) << 8) | static_cast<std::size_t>(lengthLow);
        if (packetLength == 0) continue;
        if (!readPacketBytes(packetLength)) {
            error = "The PSS audio packet is truncated.";
            return AudioReadResult::failed;
        }
        if (packet_.size() < 3) continue;
        std::size_t payloadStart = 3 + packet_[2];
        if (payloadStart + 4 > packet_.size()) continue;
        payloadStart += 4;

        if (declaredAudioBytes_ == 0) {
            auto soundDataHeader = payloadStart;
            while (soundDataHeader + 8 <= packet_.size() &&
                   !(packet_[soundDataHeader] == 'S' && packet_[soundDataHeader + 1] == 'S' &&
                     packet_[soundDataHeader + 2] == 'b' && packet_[soundDataHeader + 3] == 'd')) ++soundDataHeader;
            if (soundDataHeader + 8 > packet_.size()) continue;
            if (soundDataHeader < 32 ||
                packet_[soundDataHeader - 32] != 'S' || packet_[soundDataHeader - 31] != 'S' ||
                packet_[soundDataHeader - 30] != 'h' || packet_[soundDataHeader - 29] != 'd') {
                error = "Sony PCM format header is missing.";
                return AudioReadResult::failed;
            }
            const auto formatHeader = soundDataHeader - 32;
            const auto read32 = [&](const std::size_t offset) {
                return static_cast<std::uint32_t>(packet_[offset]) |
                       (static_cast<std::uint32_t>(packet_[offset + 1]) << 8) |
                       (static_cast<std::uint32_t>(packet_[offset + 2]) << 16) |
                       (static_cast<std::uint32_t>(packet_[offset + 3]) << 24);
            };
            interleaveBytes_ = read32(formatHeader + 20);
            if (read32(formatHeader + 8) != 1 || read32(formatHeader + 12) != 48000 ||
                read32(formatHeader + 16) != 2 || interleaveBytes_ == 0 ||
                interleaveBytes_ > 64 * 1024 || (interleaveBytes_ & 1) != 0) {
                error = "Unsupported Sony PCM format or channel interleave.";
                return AudioReadResult::failed;
            }
            declaredAudioBytes_ = static_cast<std::uint32_t>(packet_[soundDataHeader + 4]) |
                                  (static_cast<std::uint32_t>(packet_[soundDataHeader + 5]) << 8) |
                                  (static_cast<std::uint32_t>(packet_[soundDataHeader + 6]) << 16) |
                                  (static_cast<std::uint32_t>(packet_[soundDataHeader + 7]) << 24);
            payloadStart = soundDataHeader + 8;
        }
        const std::size_t remaining = declaredAudioBytes_ - audioBytesRead_;
        const std::size_t count = (std::min)(packet_.size() - payloadStart, remaining);
        if (count == 0) continue;
        audio.assign(packet_.begin() + static_cast<std::ptrdiff_t>(payloadStart),
                     packet_.begin() + static_cast<std::ptrdiff_t>(payloadStart + count));
        audioBytesRead_ += static_cast<std::uint32_t>(count);
        return AudioReadResult::data;
    }
    return AudioReadResult::pending;
}
}
