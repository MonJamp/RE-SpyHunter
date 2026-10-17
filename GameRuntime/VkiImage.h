#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
struct DecodedStill
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string details;
};

std::optional<DecodedStill> decodeVki(const std::vector<std::uint8_t>& bytes, std::string& error);
std::optional<DecodedStill> decodeVki(const std::filesystem::path& path, std::string& error);
}
