#include "AudioMpk.h"

#include <array>
#include <fstream>
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
}

bool AudioMpk::open(const std::filesystem::path& path, std::string& error)
{
    entries_.clear();
    path_.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { error = "Could not open AUDIO.MPK."; return false; }
    const auto length = static_cast<std::uint64_t>(file.tellg());
    if (length < 16) { error = "MPK header is truncated."; return false; }
    std::array<std::uint8_t, 16> header{};
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        header[0] != 'M' || header[1] != 'P' || header[2] != 'A' || header[3] != 'K' ||
        little32(header.data() + 4) != 0x10000) {
        error = "Unsupported MPK header."; return false;
    }
    const auto count = little32(header.data() + 8);
    const auto nameBytes = little32(header.data() + 12);
    const std::uint64_t nameOffset = 16ull + 12ull * count;
    if (!count || count > 4096 || nameOffset > length || nameBytes > length - nameOffset || nameBytes > 1024 * 1024) {
        error = "Invalid MPK directory size."; return false;
    }
    std::vector<std::uint8_t> directory(static_cast<size_t>(12ull * count + nameBytes));
    file.seekg(16);
    if (!file.read(reinterpret_cast<char*>(directory.data()), directory.size())) {
        error = "Could not read MPK directory."; return false;
    }
    size_t namePosition = 12ull * count;
    entries_.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* record = directory.data() + 12ull * index;
        const auto offset = little32(record);
        const auto size = little32(record + 4);
        if (offset > length || size > length - offset || namePosition >= directory.size()) {
            error = "Invalid MPK entry bounds."; entries_.clear(); return false;
        }
        const size_t first = namePosition;
        while (namePosition < directory.size() && directory[namePosition]) ++namePosition;
        if (namePosition == directory.size() || namePosition == first) {
            error = "Invalid MPK entry name."; entries_.clear(); return false;
        }
        entries_.push_back({std::string(reinterpret_cast<const char*>(directory.data() + first), namePosition - first), offset, size});
        ++namePosition;
    }
    path_ = path;
    error.clear();
    return true;
}

bool AudioMpk::read(size_t index, std::vector<std::uint8_t>& bytes, std::string& error) const
{
    bytes.clear();
    if (index >= entries_.size()) { error = "MPK entry index is out of range."; return false; }
    const auto& entry = entries_[index];
    if (entry.size > 64 * 1024 * 1024) { error = "MPK entry exceeds the playback size limit."; return false; }
    std::ifstream file(path_, std::ios::binary);
    if (!file) { error = "Could not reopen AUDIO.MPK."; return false; }
    bytes.resize(entry.size);
    file.seekg(entry.offset);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        bytes.clear(); error = "Could not read MPK entry."; return false;
    }
    error.clear();
    return true;
}
}
