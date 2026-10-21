#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <stop_token>
#include <vector>

namespace spyhunter::runtime
{
struct PckEntry
{
    std::string name;
    // Path requested by the game's archive loader immediately before this record.
    // Some valid records have no filename-like field in their serialized header.
    std::string loadName;
    std::string type;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

class PckArchive
{
public:
    bool open(const std::filesystem::path& path, std::string& error, std::stop_token stop = {});
    const std::vector<PckEntry>& entries() const { return entries_; }
    const std::string& sourceName() const { return sourceName_; }
    // Reads at most capacity bytes from one serialized entry. Returns bytes read.
    std::size_t readEntry(std::size_t index, std::uint64_t offset, void* destination,
                          std::size_t capacity, std::string& error) const;

private:
    std::filesystem::path path_;
    std::string sourceName_;
    std::vector<PckEntry> entries_;
};
}
