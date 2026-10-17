#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
struct AudioMpkEntry
{
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

class AudioMpk
{
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool read(size_t index, std::vector<std::uint8_t>& bytes, std::string& error) const;
    const std::vector<AudioMpkEntry>& entries() const { return entries_; }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::vector<AudioMpkEntry> entries_;
};
}
