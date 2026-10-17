#include "SpyHunterConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>

namespace
{
std::string trim(std::string value)
{
    const auto isNotSpace = [](const unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}
}

namespace spyhunter::configuration
{
LoadResult load(const std::filesystem::path& configPath)
{
    std::ifstream input(configPath);
    if (!input) {
        return {{}, "Could not open configuration file: " + configPath.string()};
    }

    std::optional<std::filesystem::path> extractedBinariesPath;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            return {{}, "Invalid configuration line " + std::to_string(lineNumber)};
        }

        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "ExtractedBinariesPath") {
            extractedBinariesPath = std::filesystem::path(value);
        }
    }

    if (!extractedBinariesPath || extractedBinariesPath->empty()) {
        return {{}, "ExtractedBinariesPath is required in " + configPath.string()};
    }

    auto resolvedPath = *extractedBinariesPath;
    if (resolvedPath.is_relative()) {
        resolvedPath = configPath.parent_path() / resolvedPath;
    }
    resolvedPath = std::filesystem::weakly_canonical(resolvedPath);

    if (!std::filesystem::is_directory(resolvedPath)) {
        return {{}, "ExtractedBinariesPath is not a directory: " + resolvedPath.string()};
    }

    return {Config{resolvedPath}, {}};
}

std::filesystem::path findConfigFile(const std::filesystem::path& startDirectory)
{
    auto directory = std::filesystem::absolute(startDirectory);
    while (!directory.empty()) {
        const auto candidate = directory / "SpyHunter.config";
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }

        const auto parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }

    return startDirectory / "SpyHunter.config";
}
}
