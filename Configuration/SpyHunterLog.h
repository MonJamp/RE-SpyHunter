#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace spyhunter::logging
{
enum class Level { off, error, warning, info, debug, trace };

struct Settings
{
    std::filesystem::path filePath;
    Level defaultLevel = Level::off;
    std::unordered_map<std::string, Level> modules{
        {"SHViewer", Level::info}, {"GameRuntime", Level::info}
    };
};

bool parseLevel(std::string_view value, Level& level);
bool configure(const Settings& settings, std::string& error);
bool enabled(std::string_view module, Level level);
void write(std::string_view module, Level level, std::string_view message);
}
