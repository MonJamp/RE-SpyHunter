#include "SpyHunterLog.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>

namespace spyhunter::logging
{
namespace
{
std::mutex mutex;
Settings activeSettings;
std::ofstream output;

Level threshold(const std::string_view module)
{
    const auto specific = activeSettings.modules.find(std::string(module));
    return specific == activeSettings.modules.end() ? activeSettings.defaultLevel : specific->second;
}

const char* levelName(const Level level)
{
    switch (level) {
    case Level::error: return "error";
    case Level::warning: return "warning";
    case Level::info: return "info";
    case Level::debug: return "debug";
    case Level::trace: return "trace";
    default: return "off";
    }
}
}

bool parseLevel(const std::string_view value, Level& level)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (normalized == "off") level = Level::off;
    else if (normalized == "error") level = Level::error;
    else if (normalized == "warning" || normalized == "warn") level = Level::warning;
    else if (normalized == "info") level = Level::info;
    else if (normalized == "debug") level = Level::debug;
    else if (normalized == "trace") level = Level::trace;
    else return false;
    return true;
}

bool configure(const Settings& settings, std::string& error)
{
    std::scoped_lock lock(mutex);
    output.close();
    activeSettings = settings;
    bool anyEnabled = settings.defaultLevel != Level::off;
    for (const auto& [module, level] : settings.modules) anyEnabled |= level != Level::off;
    if (!anyEnabled) return true;
    if (settings.filePath.empty()) { error = "Logging is enabled but LogFilePath is empty."; return false; }
    std::error_code fileError;
    std::filesystem::create_directories(settings.filePath.parent_path(), fileError);
    if (fileError) { error = "Could not create log directory: " + fileError.message(); return false; }
    output.open(settings.filePath, std::ios::app);
    if (!output) { error = "Could not open log file: " + settings.filePath.string(); return false; }
    return true;
}

bool enabled(const std::string_view module, const Level level)
{
    std::scoped_lock lock(mutex);
    return output.is_open() && level != Level::off && level <= threshold(module);
}

void write(const std::string_view module, const Level level, const std::string_view message)
{
    std::scoped_lock lock(mutex);
    if (!output.is_open() || level == Level::off || level > threshold(module)) return;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    output << milliseconds << '\t' << module << '\t' << levelName(level) << '\t' << message << '\n';
    output.flush();
}
}
