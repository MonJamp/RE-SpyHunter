#pragma once

#include "SpyHunterLog.h"

#include <filesystem>
#include <optional>
#include <string>

namespace spyhunter::configuration
{
struct Config
{
    std::filesystem::path extractedBinariesPath;
    bool autosaveEnabled = true;
    logging::Settings logging;
};

struct LoadResult
{
    std::optional<Config> config;
    std::string error;
};

LoadResult load(const std::filesystem::path& configPath);
std::filesystem::path findConfigFile(const std::filesystem::path& startDirectory);
}
