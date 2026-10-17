#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../../Configuration/SpyHunterConfig.h"
#include "../../Configuration/SpyHunterLog.h"
#include "../../GameRuntime/CutscenePlayer.h"
#include "../../GameRuntime/PckArchive.h"
#include "../../GameRuntime/VkiImage.h"
#include "../../GUI/Gui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
constexpr spyhunter::gui::BrowserLayout kBrowser;
struct GameFile
{
    std::filesystem::path path;
    std::string displayPath;
    std::string extension;
};

struct BrowserRow
{
    size_t fileIndex;
    std::optional<size_t> memberIndex;
};

enum class BrowserFilter { all, vki, pss, pck };

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<GameFile> findGameFiles(const std::filesystem::path& root)
{
    std::vector<GameFile> files;
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator iterator(root, options, error), end;
         iterator != end; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!iterator->is_regular_file(error) || error) { error.clear(); continue; }
        std::error_code relativeError;
        const auto relative = std::filesystem::relative(iterator->path(), root, relativeError);
        files.push_back({iterator->path(), relativeError ? iterator->path().string() : relative.generic_string(),
                         lowercase(iterator->path().extension().string())});
    }
    std::sort(files.begin(), files.end(), [](const GameFile& left, const GameFile& right) {
        return lowercase(left.displayPath) < lowercase(right.displayPath);
    });
    return files;
}

using spyhunter::runtime::DecodedStill;
using spyhunter::runtime::decodeVki;

SDL_Texture* createTexture(SDL_Renderer* renderer, const DecodedStill& still, std::string& error)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, still.width, still.height);
    if (!texture) { error = SDL_GetError(); return nullptr; }
    if (!SDL_UpdateTexture(texture, nullptr, still.rgba.data(), still.width * 4)) {
        error = SDL_GetError(); SDL_DestroyTexture(texture); return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

bool supported(const GameFile& file)
{
    return file.extension == ".vki" || file.extension == ".pss" || file.extension == ".pck";
}

bool supported(const spyhunter::runtime::PckEntry& entry)
{
    return entry.type == "vkImage" && lowercase(std::filesystem::path(entry.name).extension().string()) == ".vki";
}
}

int main(int argc, char* argv[])
{
    const std::filesystem::path configPath = argc > 1
        ? std::filesystem::path(argv[1])
        : spyhunter::configuration::findConfigFile(std::filesystem::current_path());
    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) {
        SDL_Log("Configuration error: %s", configResult.error.c_str());
        SDL_Log("Pass the path to SpyHunter.config as the first argument.");
        return 1;
    }
    std::string loggingError;
    if (!spyhunter::logging::configure(configResult.config->logging, loggingError)) {
        SDL_Log("Logging unavailable: %s", loggingError.c_str());
    }
    spyhunter::logging::write("Configuration", spyhunter::logging::Level::info,
                              "loaded path=" + configPath.string());

    const auto gameFiles = findGameFiles(configResult.config->extractedBinariesPath);
    SDL_Log("Configuration: %s", configPath.string().c_str());
    SDL_Log("Found %zu files under %s; viewing is supported for .VKI, .PSS, and .PCK.", gameFiles.size(),
            configResult.config->extractedBinariesPath.string().c_str());
    spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info,
                              "inventory files=" + std::to_string(gameFiles.size()) +
                              " root=" + configResult.config->extractedBinariesPath.string());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("SpyHunter - SH Viewer", spyhunter::gui::defaultWindowWidth,
                                     spyhunter::gui::defaultWindowHeight, SDL_WINDOW_RESIZABLE, &window, &renderer) ||
        !spyhunter::gui::configureRenderer(renderer)) {
        SDL_Log("Window or renderer setup failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    const size_t vkiCount = static_cast<size_t>(std::count_if(gameFiles.begin(), gameFiles.end(), [](const GameFile& file) { return file.extension == ".vki"; }));
    const size_t pssCount = static_cast<size_t>(std::count_if(gameFiles.begin(), gameFiles.end(), [](const GameFile& file) { return file.extension == ".pss"; }));
    const size_t pckCount = static_cast<size_t>(std::count_if(gameFiles.begin(), gameFiles.end(), [](const GameFile& file) { return file.extension == ".pck"; }));
    BrowserFilter filter = BrowserFilter::all;
    std::vector<size_t> browserFiles;
    std::vector<BrowserRow> browserRows;
    size_t selectedIndex = 0, firstVisibleIndex = 0;
    size_t loadedIndex = (std::numeric_limits<size_t>::max)();
    std::optional<size_t> loadedMemberIndex;
    SDL_Texture* stillTexture = nullptr;
    int stillWidth = 0, stillHeight = 0;
    spyhunter::runtime::CutsceneSession cutscene;
    spyhunter::runtime::PckArchive archive;
    std::optional<size_t> expandedFileIndex;
    std::string status = gameFiles.empty() ? "No files found in the configured game-files directory."
                                          : "Click a file or use Up/Down and Enter to view it.";
    bool running = true;

    const auto rebuildRows = [&] {
        browserRows.clear();
        for (const size_t fileIndex : browserFiles) {
            browserRows.push_back({fileIndex, std::nullopt});
            if (expandedFileIndex == fileIndex) {
                for (size_t memberIndex = 0; memberIndex < archive.entries().size(); ++memberIndex)
                    browserRows.push_back({fileIndex, memberIndex});
            }
        }
    };
    const auto setFilter = [&](const BrowserFilter nextFilter) {
        filter = nextFilter;
        expandedFileIndex.reset();
        browserFiles.clear();
        for (size_t index = 0; index < gameFiles.size(); ++index) {
            if (filter == BrowserFilter::all ||
                (filter == BrowserFilter::vki && gameFiles[index].extension == ".vki") ||
                (filter == BrowserFilter::pss && gameFiles[index].extension == ".pss") ||
                (filter == BrowserFilter::pck && gameFiles[index].extension == ".pck")) browserFiles.push_back(index);
        }
        rebuildRows();
        selectedIndex = firstVisibleIndex = 0;
        if (filter == BrowserFilter::vki) status = "Showing " + std::to_string(browserFiles.size()) + " VKI stills.";
        else if (filter == BrowserFilter::pss) status = "Showing " + std::to_string(browserFiles.size()) + " PSS cutscenes.";
        else if (filter == BrowserFilter::pck) status = "Showing " + std::to_string(browserFiles.size()) + " PCK archives.";
        else status = "Showing all " + std::to_string(browserFiles.size()) + " game files.";
    };
    setFilter(BrowserFilter::all);

    const auto activateSelected = [&] {
        if (browserRows.empty()) return;
        const BrowserRow row = browserRows[selectedIndex];
        cutscene.stop();
        SDL_DestroyTexture(stillTexture);
        stillTexture = nullptr;
        stillWidth = stillHeight = 0;
        loadedIndex = row.fileIndex;
        loadedMemberIndex = row.memberIndex;
        const GameFile& file = gameFiles[loadedIndex];
        if (row.memberIndex) {
            const auto& entry = archive.entries()[*row.memberIndex];
            spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info,
                "select member=" + file.displayPath + "/" + entry.name);
            if (!supported(entry)) {
                status = "Viewing this PCK member type is not supported: " + entry.type;
                return;
            }
            if (entry.size > 32ull * 1024 * 1024 || entry.size > (std::numeric_limits<size_t>::max)()) {
                status = "PCK member exceeds the VKI viewer size limit.";
                return;
            }
            std::vector<std::uint8_t> bytes(static_cast<size_t>(entry.size));
            std::string error;
            if (archive.readEntry(*row.memberIndex, 0, bytes.data(), bytes.size(), error) != bytes.size()) {
                status = "Could not read PCK member: " + error;
                return;
            }
            if (const auto decoded = decodeVki(bytes, error)) {
                stillTexture = createTexture(renderer, *decoded, error);
                if (stillTexture) {
                    stillWidth = decoded->width;
                    stillHeight = decoded->height;
                    status = decoded->details;
                }
            }
            if (!stillTexture) status = "Could not display PCK VKI: " + error;
            return;
        }
        spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info, "select file=" + file.displayPath);
        if (file.extension == ".vki") {
            std::string error;
            if (const auto decoded = decodeVki(file.path, error)) {
                stillTexture = createTexture(renderer, *decoded, error);
                if (stillTexture) {
                    stillWidth = decoded->width;
                    stillHeight = decoded->height;
                    status = decoded->details;
                }
            }
            if (!stillTexture) status = "Could not display VKI: " + error;
        } else if (file.extension == ".pss") {
            std::string error;
            const spyhunter::runtime::CutsceneOptions options{file.path, true};
            if (cutscene.load(renderer, options, error)) status = "Playing " + file.displayPath;
            else status = "Could not play PSS: " + error;
        } else if (file.extension == ".pck") {
            if (expandedFileIndex == loadedIndex) {
                expandedFileIndex.reset();
                status = "PCK subtree collapsed.";
            } else {
                std::string error;
                if (archive.open(file.path, error)) {
                    expandedFileIndex = loadedIndex;
                    status = std::to_string(archive.entries().size()) + " members. Select one in the subtree.";
                } else {
                    expandedFileIndex.reset();
                    status = "Could not open PCK: " + error;
                }
            }
            rebuildRows();
            for (size_t i = 0; i < browserRows.size(); ++i)
                if (browserRows[i].fileIndex == loadedIndex && !browserRows[i].memberIndex) {
                    selectedIndex = i; break;
                }
        } else {
            const std::string extension = file.extension.empty() ? "(no extension)" : file.extension;
            status = "Viewing " + extension + " files is not supported. Current viewers: .VKI, .PSS, .PCK.";
        }
    };

    while (running) {
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &pixelWidth, &pixelHeight);
        const float width = pixelWidth / spyhunter::gui::defaultUiScale;
        const float height = pixelHeight / spyhunter::gui::defaultUiScale;
        const float browserLeft = (std::max)(0.0f, width - kBrowser.width);
        const SDL_FRect browserRect{browserLeft, 0.0f, width - browserLeft, height};
        const size_t visibleRows = (std::max)(1, static_cast<int>((height - kBrowser.top - 44.0f) / kBrowser.rowHeight));

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    if (cutscene.isPlaying()) { cutscene.stop(); status = "Playback stopped."; }
                    else running = false;
                } else if (event.key.key == SDLK_1) setFilter(BrowserFilter::all);
                else if (event.key.key == SDLK_2) setFilter(BrowserFilter::vki);
                else if (event.key.key == SDLK_3) setFilter(BrowserFilter::pss);
                else if (event.key.key == SDLK_4) setFilter(BrowserFilter::pck);
                else if (!browserRows.empty() && event.key.key == SDLK_DOWN) {
                    selectedIndex = (std::min)(selectedIndex + 1, browserRows.size() - 1);
                } else if (!browserRows.empty() && event.key.key == SDLK_UP) {
                    selectedIndex = selectedIndex == 0 ? 0 : selectedIndex - 1;
                } else if (!browserRows.empty() && event.key.key == SDLK_PAGEDOWN) {
                    selectedIndex = (std::min)(selectedIndex + visibleRows, browserRows.size() - 1);
                } else if (!browserRows.empty() && event.key.key == SDLK_PAGEUP) {
                    selectedIndex = selectedIndex > visibleRows ? selectedIndex - visibleRows : 0;
                } else if (!browserRows.empty() && (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)) {
                    activateSelected();
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f) {
                const size_t maximumFirst = browserRows.size() > visibleRows ? browserRows.size() - visibleRows : 0;
                if (event.wheel.y < 0.0f) firstVisibleIndex = (std::min)(firstVisibleIndex + 3, maximumFirst);
                else firstVisibleIndex = firstVisibleIndex > 3 ? firstVisibleIndex - 3 : 0;
                if (!browserRows.empty()) selectedIndex = (std::clamp)(selectedIndex, firstVisibleIndex,
                    (std::min)(firstVisibleIndex + visibleRows - 1, browserRows.size() - 1));
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                       event.button.x / spyhunter::gui::defaultUiScale >= browserLeft) {
                const float mouseX = event.button.x / spyhunter::gui::defaultUiScale - browserLeft;
                const float mouseY = event.button.y / spyhunter::gui::defaultUiScale;
                if (mouseY >= 52.0f && mouseY < 78.0f) {
                    if (mouseX < 80.0f) setFilter(BrowserFilter::all);
                    else if (mouseX < 158.0f) setFilter(BrowserFilter::vki);
                    else if (mouseX < 236.0f) setFilter(BrowserFilter::pss);
                    else setFilter(BrowserFilter::pck);
                } else if (mouseY >= kBrowser.top) {
                    const size_t row = static_cast<size_t>((mouseY - kBrowser.top) / kBrowser.rowHeight);
                    const size_t index = firstVisibleIndex + row;
                    if (index < browserRows.size() && row < visibleRows) { selectedIndex = index; activateSelected(); }
                }
            }
        }

        if (!browserRows.empty()) {
            if (selectedIndex < firstVisibleIndex) firstVisibleIndex = selectedIndex;
            if (selectedIndex >= firstVisibleIndex + visibleRows) firstVisibleIndex = selectedIndex - visibleRows + 1;
            firstVisibleIndex = (std::min)(firstVisibleIndex, browserRows.size() > visibleRows ? browserRows.size() - visibleRows : size_t{0});
        }
        cutscene.update();
        if (!cutscene.error().empty()) status = "Playback failed: " + cutscene.error();
        if (cutscene.hasFinished()) status = "Playback finished. Select another file.";

        SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect preview{0.0f, 0.0f, browserLeft, height};
        spyhunter::gui::panel(renderer, preview, SDL_Color{0, 0, 0, 255});
        if (loadedIndex < gameFiles.size()) {
            SDL_SetRenderDrawColor(renderer, 225, 232, 244, 255);
            std::string title = gameFiles[loadedIndex].displayPath;
            if (loadedMemberIndex) title += " / " + archive.entries()[*loadedMemberIndex].name;
            spyhunter::gui::text(renderer, 24.0f, 20.0f, spyhunter::gui::shorten(title, 100));
            SDL_SetRenderDrawColor(renderer, 170, 180, 195, 255);
            spyhunter::gui::text(renderer, 24.0f, 42.0f, spyhunter::gui::shorten(status, 100));
        } else {
            SDL_SetRenderDrawColor(renderer, 225, 232, 244, 255);
            spyhunter::gui::text(renderer, 24.0f, 24.0f, "SH VIEWER");
            SDL_SetRenderDrawColor(renderer, 170, 180, 195, 255);
            spyhunter::gui::text(renderer, 24.0f, 50.0f, spyhunter::gui::shorten(status, 100));
        }
        if (cutscene.isPlaying()) cutscene.render(renderer, SDL_FRect{0.0f, 72.0f, browserLeft, (std::max)(0.0f, height - 72.0f)});
        else if (stillTexture) {
            const SDL_FRect viewport{16.0f, 72.0f, (std::max)(0.0f, browserLeft - 32.0f), (std::max)(0.0f, height - 88.0f)};
            const auto destination = spyhunter::gui::fittedRect(viewport, static_cast<float>(stillWidth), static_cast<float>(stillHeight));
            SDL_RenderTexture(renderer, stillTexture, nullptr, &destination);
        } else if (!loadedMemberIndex && expandedFileIndex == loadedIndex) {
            SDL_SetRenderDrawColor(renderer, 175, 190, 215, 255);
            spyhunter::gui::text(renderer, 24.0f, 78.0f, "Select a member from the expanded PCK subtree.");
        }

        spyhunter::gui::panel(renderer, browserRect, SDL_Color{27, 31, 42, 255});
        SDL_SetRenderDrawColor(renderer, 89, 104, 132, 255);
        SDL_RenderLine(renderer, browserLeft, 0.0f, browserLeft, height);
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, 16.0f, "SH VIEWER");
        SDL_SetRenderDrawColor(renderer, 160, 170, 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, 36.0f, std::to_string(browserFiles.size()) + " shown / " + std::to_string(gameFiles.size()) + " game files");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::all ? 255 : 155, filter == BrowserFilter::all ? 255 : 170, filter == BrowserFilter::all ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 12.0f, 58.0f, "ALL [1] " + std::to_string(gameFiles.size()));
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::vki ? 255 : 155, filter == BrowserFilter::vki ? 255 : 170, filter == BrowserFilter::vki ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 88.0f, 58.0f, "VKI [2] " + std::to_string(vkiCount));
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::pss ? 255 : 155, filter == BrowserFilter::pss ? 255 : 170, filter == BrowserFilter::pss ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 166.0f, 58.0f, "PSS [3] " + std::to_string(pssCount));
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::pck ? 255 : 155, filter == BrowserFilter::pck ? 255 : 170, filter == BrowserFilter::pck ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 244.0f, 58.0f, "PCK [4] " + std::to_string(pckCount));
        for (size_t row = 0; row < visibleRows && firstVisibleIndex + row < browserRows.size(); ++row) {
            const size_t index = firstVisibleIndex + row;
            const BrowserRow& browserRow = browserRows[index];
            const GameFile& file = gameFiles[browserRow.fileIndex];
            const SDL_FRect rowRect{browserLeft + 8.0f, kBrowser.top + row * kBrowser.rowHeight,
                                    browserRect.w - 16.0f, kBrowser.rowHeight - 2.0f};
            if (index == selectedIndex) {
                spyhunter::gui::panel(renderer, rowRect, SDL_Color{57, 95, 152, 255});
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            } else if (browserRow.memberIndex ? supported(archive.entries()[*browserRow.memberIndex]) : supported(file))
                SDL_SetRenderDrawColor(renderer, 210, 218, 230, 255);
            else SDL_SetRenderDrawColor(renderer, 115, 124, 140, 255);
            std::string label;
            if (browserRow.memberIndex) {
                const auto& entry = archive.entries()[*browserRow.memberIndex];
                label = "    " + entry.name + (supported(entry) ? "" : "  [unsupported]");
            } else {
                label = (file.extension == ".pck" ? (expandedFileIndex == browserRow.fileIndex ? "[-] " : "[+] ") : "") +
                    file.displayPath + (supported(file) ? "" : "  [unsupported]");
            }
            spyhunter::gui::text(renderer, rowRect.x + 6.0f, rowRect.y + 4.0f, spyhunter::gui::shorten(label, 43));
        }
        SDL_SetRenderDrawColor(renderer, 180, 190, 205, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, height - 48.0f, "Click / Enter: view | Wheel / arrows: browse");
        spyhunter::gui::text(renderer, browserLeft + 16.0f, height - 28.0f, spyhunter::gui::shorten(status, 44));
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    cutscene.stop();
    SDL_DestroyTexture(stillTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
