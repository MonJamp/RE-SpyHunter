#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../../Configuration/SpyHunterConfig.h"
#include "../../Configuration/SpyHunterLog.h"
#include "../../GameRuntime/CutscenePlayer.h"
#include "../../GameRuntime/AudioMpk.h"
#include "../../GameRuntime/AudioMpkPlayer.h"
#include "../../GameRuntime/PckArchive.h"
#include "../../GameRuntime/VkiImage.h"
#include "../../GameRuntime/RgbaImage.h"
#include "../../GUI/Gui.h"
#include "../../ThirdParty/CLI11/CLI11.hpp"

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

enum class BrowserFilter { all, vki, pss, pck, mpk };

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

DecodedStill alphaImage(const DecodedStill& source)
{
    auto result = source;
    for (size_t pixel = 0; pixel < result.rgba.size(); pixel += 4) {
        const auto alpha = result.rgba[pixel + 3];
        result.rgba[pixel] = result.rgba[pixel + 1] = result.rgba[pixel + 2] = alpha;
        result.rgba[pixel + 3] = 255;
    }
    return result;
}

SDL_Texture* createTexture(SDL_Renderer* renderer, const DecodedStill& still, std::string& error, bool alphaOnly = false)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, still.width, still.height);
    if (!texture) { error = SDL_GetError(); return nullptr; }
    const auto alpha = alphaOnly ? std::optional<DecodedStill>(alphaImage(still)) : std::nullopt;
    if (!SDL_UpdateTexture(texture, nullptr, alpha ? alpha->rgba.data() : still.rgba.data(), still.width * 4)) {
        error = SDL_GetError(); SDL_DestroyTexture(texture); return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

bool supported(const GameFile& file)
{
    return file.extension == ".vki" || file.extension == ".pss" || file.extension == ".pck" || file.extension == ".mpk" ||
           file.extension == ".rgba" || file.extension == ".rgb";
}

bool supported(const spyhunter::runtime::PckEntry& entry)
{
    return entry.type == "vkDMATexture" ||
           (entry.type == "vkImage" && lowercase(std::filesystem::path(entry.name).extension().string()) == ".vki");
}

bool supported(const spyhunter::runtime::AudioMpkEntry& entry)
{
    const auto extension = lowercase(std::filesystem::path(entry.name).extension().string());
    return extension == ".vag" || extension == ".mc3";
}
}

int main(int argc, char* argv[])
{
    std::string configArgument;
    std::string openArgument, memberArgument, exportArgument, captureArgument;
    bool alphaPreview = false;
    CLI::App commandLine{"Browse and preview extracted SpyHunter game files."};
    commandLine.add_option("config", configArgument, "Optional SpyHunter.config path");
    auto* openOption = commandLine.add_option("--open", openArgument, "Open a file relative to the configured game directory");
    commandLine.add_option("--member", memberArgument, "Open a named PCK member (full name or basename)")->needs(openOption);
    auto* exportOption = commandLine.add_option("--export-image", exportArgument, "Export the selected decoded still as a native-resolution BMP and exit")->needs(openOption);
    commandLine.add_option("--capture-view", captureArgument, "Save one viewer frame as a BMP and exit")->needs(openOption)->excludes(exportOption);
    commandLine.add_flag("--alpha", alphaPreview, "Start in alpha-channel view (also applies to image export)")->needs(openOption);
    argv = commandLine.ensure_utf8(argv);
    try {
        commandLine.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        const bool requestedHelp = error.get_exit_code() == static_cast<int>(CLI::ExitCodes::Success);
        SDL_ShowSimpleMessageBox(requestedHelp ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
                                 "SH Viewer command line",
                                 requestedHelp ? commandLine.help().c_str() : error.what(), nullptr);
        return requestedHelp ? 0 : 2;
    }
    const auto configPath = configArgument.empty()
        ? spyhunter::configuration::findConfigFile(std::filesystem::current_path())
        : std::filesystem::path(std::u8string(configArgument.begin(), configArgument.end()));
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
    SDL_Log("Found %zu files under %s; viewing is supported for .VKI, .RGBA/.RGB, .PSS, .PCK, and .MPK.", gameFiles.size(),
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
                                     spyhunter::gui::defaultWindowHeight, SDL_WINDOW_RESIZABLE |
                                     (exportArgument.empty() && captureArgument.empty() ? 0 : SDL_WINDOW_HIDDEN), &window, &renderer) ||
        !spyhunter::gui::configureRenderer(renderer)) {
        SDL_Log("Window or renderer setup failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    BrowserFilter filter = BrowserFilter::all;
    std::vector<size_t> browserFiles;
    std::vector<BrowserRow> browserRows;
    size_t selectedIndex = 0, firstVisibleIndex = 0;
    size_t loadedIndex = (std::numeric_limits<size_t>::max)();
    std::optional<size_t> loadedMemberIndex;
    SDL_Texture* stillTexture = nullptr;
    std::optional<DecodedStill> decodedStill;
    bool texturePreview = false, checkerboard = true;
    int stillWidth = 0, stillHeight = 0;
    spyhunter::runtime::CutsceneSession cutscene;
    spyhunter::runtime::PckArchive archive;
    spyhunter::runtime::AudioMpk audioArchive;
    spyhunter::runtime::AudioMpkPlayer audioPlayer;
    std::optional<size_t> expandedFileIndex;
    std::string status = gameFiles.empty() ? "No files found in the configured game-files directory."
                                          : "Click a file or use Up/Down and Enter to view it.";
    bool running = true;

    const auto rebuildRows = [&] {
        browserRows.clear();
        for (const size_t fileIndex : browserFiles) {
            browserRows.push_back({fileIndex, std::nullopt});
            if (expandedFileIndex == fileIndex) {
                if (gameFiles[fileIndex].extension == ".mpk") {
                    for (size_t memberIndex = 0; memberIndex < audioArchive.entries().size(); ++memberIndex)
                        if (supported(audioArchive.entries()[memberIndex])) browserRows.push_back({fileIndex, memberIndex});
                } else {
                    for (size_t memberIndex = 0; memberIndex < archive.entries().size(); ++memberIndex)
                        browserRows.push_back({fileIndex, memberIndex});
                }
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
                (filter == BrowserFilter::pck && gameFiles[index].extension == ".pck") ||
                (filter == BrowserFilter::mpk && gameFiles[index].extension == ".mpk")) browserFiles.push_back(index);
        }
        rebuildRows();
        selectedIndex = firstVisibleIndex = 0;
        if (filter == BrowserFilter::vki) status = "Showing " + std::to_string(browserFiles.size()) + " VKI stills.";
        else if (filter == BrowserFilter::pss) status = "Showing " + std::to_string(browserFiles.size()) + " PSS cutscenes.";
        else if (filter == BrowserFilter::pck) status = "Showing " + std::to_string(browserFiles.size()) + " PCK archives.";
        else if (filter == BrowserFilter::mpk) status = "Showing " + std::to_string(browserFiles.size()) + " MPK archives.";
        else status = "Showing all " + std::to_string(browserFiles.size()) + " game files.";
    };
    setFilter(BrowserFilter::all);

    const auto activateSelected = [&] {
        if (browserRows.empty()) return;
        const BrowserRow row = browserRows[selectedIndex];
        cutscene.stop();
        audioPlayer.stop();
        SDL_DestroyTexture(stillTexture);
        stillTexture = nullptr;
        decodedStill.reset();
        texturePreview = false;
        stillWidth = stillHeight = 0;
        loadedIndex = row.fileIndex;
        loadedMemberIndex = row.memberIndex;
        const GameFile& file = gameFiles[loadedIndex];
        if (row.memberIndex) {
            if (file.extension == ".mpk") {
                const auto& entry = audioArchive.entries()[*row.memberIndex];
                spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info,
                    "select audio=" + file.displayPath + "/" + entry.name);
                std::string error, details;
                if (!audioPlayer.play(audioArchive, *row.memberIndex, details, error))
                    status = "Could not play " + entry.name + ": " + error;
                else status = "Playing " + entry.name + " (" + details + ")";
                return;
            }
            const auto& entry = archive.entries()[*row.memberIndex];
            spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info,
                "select member=" + file.displayPath + "/" + entry.name);
            if (!supported(entry)) {
                status = "Viewing this PCK member type is not supported: " + entry.type;
                return;
            }
            if (entry.size > 32ull * 1024 * 1024 || entry.size > (std::numeric_limits<size_t>::max)()) {
                status = "PCK member exceeds the 32 MiB still-image size limit.";
                return;
            }
            std::vector<std::uint8_t> bytes(static_cast<size_t>(entry.size));
            std::string error;
            if (archive.readEntry(*row.memberIndex, 0, bytes.data(), bytes.size(), error) != bytes.size()) {
                status = "Could not read PCK member: " + error;
                return;
            }
            texturePreview = entry.type == "vkDMATexture";
            if (auto decoded = texturePreview ? spyhunter::runtime::decodeRgba(std::span<const std::uint8_t>(bytes), error) : decodeVki(bytes, error)) {
                stillTexture = createTexture(renderer, *decoded, error, alphaPreview && texturePreview);
                if (stillTexture) {
                    stillWidth = decoded->width;
                    stillHeight = decoded->height;
                    status = decoded->details;
                    decodedStill = std::move(decoded);
                }
            }
            if (!stillTexture) status = "Could not display PCK image: " + error;
            return;
        }
        spyhunter::logging::write("SHViewer", spyhunter::logging::Level::info, "select file=" + file.displayPath);
        if (file.extension == ".vki" || file.extension == ".rgba" || file.extension == ".rgb") {
            std::string error;
            texturePreview = file.extension != ".vki";
            if (auto decoded = texturePreview ? spyhunter::runtime::decodeRgba(file.path, error) : decodeVki(file.path, error)) {
                stillTexture = createTexture(renderer, *decoded, error, alphaPreview && texturePreview);
                if (stillTexture) {
                    stillWidth = decoded->width;
                    stillHeight = decoded->height;
                    status = decoded->details;
                    decodedStill = std::move(decoded);
                }
            }
            if (!stillTexture) status = "Could not display image: " + error;
        } else if (file.extension == ".pss") {
            std::string error;
            const spyhunter::runtime::CutsceneOptions options{file.path, true};
            if (cutscene.load(renderer, options, error)) status = "Playing " + file.displayPath;
            else status = "Could not play PSS: " + error;
        } else if (file.extension == ".pck" || file.extension == ".mpk") {
            const bool isAudio = file.extension == ".mpk";
            if (expandedFileIndex == loadedIndex) {
                expandedFileIndex.reset();
                status = isAudio ? "MPK audio list collapsed." : "PCK subtree collapsed.";
            } else {
                std::string error;
                if (isAudio ? audioArchive.open(file.path, error) : archive.open(file.path, error)) {
                    expandedFileIndex = loadedIndex;
                    const auto count = isAudio
                        ? std::count_if(audioArchive.entries().begin(), audioArchive.entries().end(),
                            [](const spyhunter::runtime::AudioMpkEntry& entry) { return supported(entry); })
                        : static_cast<long long>(archive.entries().size());
                    status = std::to_string(count) + (isAudio ? " audio files. Select one to play." : " members. Select one in the subtree.");
                } else {
                    expandedFileIndex.reset();
                    status = "Could not open archive: " + error;
                }
            }
            rebuildRows();
            for (size_t i = 0; i < browserRows.size(); ++i)
                if (browserRows[i].fileIndex == loadedIndex && !browserRows[i].memberIndex) {
                    selectedIndex = i; break;
                }
        } else {
            const std::string extension = file.extension.empty() ? "(no extension)" : file.extension;
            status = "Viewing " + extension + " files is not supported. Current viewers: .VKI, .RGBA/.RGB, .PSS, .PCK, .MPK.";
        }
    };

    int exitCode = 0;
    if (!openArgument.empty()) {
        const auto requested = lowercase(std::filesystem::path(openArgument).lexically_normal().generic_string());
        const auto found = std::find_if(gameFiles.begin(), gameFiles.end(), [&](const GameFile& file) {
            return lowercase(file.displayPath) == requested;
        });
        if (found == gameFiles.end()) { status = "Requested file was not found: " + openArgument; exitCode = 1; }
        else {
            selectedIndex = static_cast<size_t>(found - gameFiles.begin());
            activateSelected();
            if (!memberArgument.empty()) {
                std::optional<size_t> memberRow;
                const auto requestedMember = lowercase(memberArgument);
                for (size_t row = 0; row < browserRows.size(); ++row) {
                    const auto& candidate = browserRows[row];
                    if (candidate.fileIndex != static_cast<size_t>(found - gameFiles.begin()) || !candidate.memberIndex ||
                        found->extension != ".pck") continue;
                    const auto& name = archive.entries()[*candidate.memberIndex].name;
                    if (lowercase(name) == requestedMember ||
                        lowercase(std::filesystem::path(name).filename().string()) == requestedMember) {
                        if (!memberRow || supported(archive.entries()[*candidate.memberIndex])) memberRow = row;
                        if (supported(archive.entries()[*candidate.memberIndex])) break;
                    }
                }
                if (memberRow) { selectedIndex = *memberRow; activateSelected(); }
                else { status = "Requested PCK member was not found: " + memberArgument; exitCode = 1; }
            }
        }
    }
    if (!exportArgument.empty()) {
        if (!decodedStill || exitCode) { SDL_Log("Image export failed: %s", status.c_str()); exitCode = 1; }
        else {
            auto alpha = alphaPreview && texturePreview ? std::optional<DecodedStill>(alphaImage(*decodedStill)) : std::nullopt;
            SDL_Surface* surface = SDL_CreateSurfaceFrom(stillWidth, stillHeight, SDL_PIXELFORMAT_RGBA32,
                                                        alpha ? alpha->rgba.data() : decodedStill->rgba.data(), stillWidth * 4);
            if (!surface || !SDL_SaveBMP(surface, exportArgument.c_str())) { SDL_Log("Image export failed: %s", SDL_GetError()); exitCode = 1; }
            else SDL_Log("Exported %s: %s", exportArgument.c_str(), status.c_str());
            SDL_DestroySurface(surface);
        }
        running = false;
    }

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
                    if (cutscene.isPlaying() || audioPlayer.isPlaying()) {
                        cutscene.stop(); audioPlayer.stop(); status = "Playback stopped.";
                    }
                    else running = false;
                } else if (event.key.key == SDLK_1) setFilter(BrowserFilter::all);
                else if (event.key.key == SDLK_2) setFilter(BrowserFilter::vki);
                else if (event.key.key == SDLK_3) setFilter(BrowserFilter::pss);
                else if (event.key.key == SDLK_4) setFilter(BrowserFilter::pck);
                else if (event.key.key == SDLK_5) setFilter(BrowserFilter::mpk);
                else if (event.key.key == SDLK_C) checkerboard = !checkerboard;
                else if (event.key.key == SDLK_A && texturePreview && decodedStill) {
                    std::string error;
                    if (auto* replacement = createTexture(renderer, *decodedStill, error, !alphaPreview)) {
                        SDL_DestroyTexture(stillTexture); stillTexture = replacement; alphaPreview = !alphaPreview;
                    } else status = "Could not display alpha channel: " + error;
                }
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
                    if (mouseX < 68.0f) setFilter(BrowserFilter::all);
                    else if (mouseX < 132.0f) setFilter(BrowserFilter::vki);
                    else if (mouseX < 196.0f) setFilter(BrowserFilter::pss);
                    else if (mouseX < 260.0f) setFilter(BrowserFilter::pck);
                    else setFilter(BrowserFilter::mpk);
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
        audioPlayer.update();
        if (!audioPlayer.error().empty()) status = "Audio playback failed: " + audioPlayer.error();
        if (!cutscene.error().empty()) status = "Playback failed: " + cutscene.error();
        if (cutscene.hasFinished()) status = "Playback finished. Select another file.";

        SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect preview{0.0f, 0.0f, browserLeft, height};
        spyhunter::gui::panel(renderer, preview, SDL_Color{0, 0, 0, 255});
        if (loadedIndex < gameFiles.size()) {
            SDL_SetRenderDrawColor(renderer, 225, 232, 244, 255);
            std::string title = gameFiles[loadedIndex].displayPath;
            if (loadedMemberIndex) title += " / " + (gameFiles[loadedIndex].extension == ".mpk"
                ? audioArchive.entries()[*loadedMemberIndex].name : archive.entries()[*loadedMemberIndex].name);
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
            const SDL_FRect viewport{16.0f, 72.0f, (std::max)(0.0f, browserLeft - 32.0f),
                                     (std::max)(0.0f, height - (texturePreview ? 118.0f : 88.0f))};
            const auto destination = spyhunter::gui::fittedRect(viewport, static_cast<float>(stillWidth), static_cast<float>(stillHeight));
            if (texturePreview && checkerboard) {
                // Show transparency without treating the texture's alpha as an opaque channel.
                constexpr float tile = 16.0f;
                for (int row = 0; row * tile < destination.h; ++row)
                    for (int col = 0; col * tile < destination.w; ++col) {
                        const auto shade = static_cast<Uint8>((row + col) % 2 ? 60 : 35);
                        spyhunter::gui::panel(renderer, {destination.x + col * tile, destination.y + row * tile,
                            (std::min)(tile, destination.w - col * tile), (std::min)(tile, destination.h - row * tile)},
                            {shade, shade, shade, 255});
                    }
            }
            SDL_RenderTexture(renderer, stillTexture, nullptr, &destination);
            if (texturePreview) {
                SDL_SetRenderDrawColor(renderer, 170, 180, 195, 255);
                spyhunter::gui::text(renderer, 24.0f, height - 20.0f, "C: checkerboard / black | A: alpha / color | Raw texture preview");
            }
        } else if (!loadedMemberIndex && expandedFileIndex == loadedIndex) {
            SDL_SetRenderDrawColor(renderer, 175, 190, 215, 255);
            spyhunter::gui::text(renderer, 24.0f, 78.0f, gameFiles[loadedIndex].extension == ".mpk"
                ? "Select an audio file to play it." : "Select a member from the expanded PCK subtree.");
        }

        spyhunter::gui::panel(renderer, browserRect, SDL_Color{27, 31, 42, 255});
        SDL_SetRenderDrawColor(renderer, 89, 104, 132, 255);
        SDL_RenderLine(renderer, browserLeft, 0.0f, browserLeft, height);
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, 16.0f, "SH VIEWER");
        SDL_SetRenderDrawColor(renderer, 160, 170, 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, 36.0f, std::to_string(browserFiles.size()) + " shown / " + std::to_string(gameFiles.size()) + " game files");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::all ? 255 : 155, filter == BrowserFilter::all ? 255 : 170, filter == BrowserFilter::all ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 12.0f, 58.0f, "ALL [1]");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::vki ? 255 : 155, filter == BrowserFilter::vki ? 255 : 170, filter == BrowserFilter::vki ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 76.0f, 58.0f, "VKI [2]");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::pss ? 255 : 155, filter == BrowserFilter::pss ? 255 : 170, filter == BrowserFilter::pss ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 140.0f, 58.0f, "PSS [3]");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::pck ? 255 : 155, filter == BrowserFilter::pck ? 255 : 170, filter == BrowserFilter::pck ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 204.0f, 58.0f, "PCK [4]");
        SDL_SetRenderDrawColor(renderer, filter == BrowserFilter::mpk ? 255 : 155, filter == BrowserFilter::mpk ? 255 : 170, filter == BrowserFilter::mpk ? 255 : 190, 255);
        spyhunter::gui::text(renderer, browserLeft + 268.0f, 58.0f, "MPK [5]");
        for (size_t row = 0; row < visibleRows && firstVisibleIndex + row < browserRows.size(); ++row) {
            const size_t index = firstVisibleIndex + row;
            const BrowserRow& browserRow = browserRows[index];
            const GameFile& file = gameFiles[browserRow.fileIndex];
            const SDL_FRect rowRect{browserLeft + 8.0f, kBrowser.top + row * kBrowser.rowHeight,
                                    browserRect.w - 16.0f, kBrowser.rowHeight - 2.0f};
            if (index == selectedIndex) {
                spyhunter::gui::panel(renderer, rowRect, SDL_Color{57, 95, 152, 255});
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            } else if (browserRow.memberIndex ? (file.extension == ".mpk"
                ? supported(audioArchive.entries()[*browserRow.memberIndex])
                : supported(archive.entries()[*browserRow.memberIndex])) : supported(file))
                SDL_SetRenderDrawColor(renderer, 210, 218, 230, 255);
            else SDL_SetRenderDrawColor(renderer, 115, 124, 140, 255);
            std::string label;
            if (browserRow.memberIndex) {
                if (file.extension == ".mpk") label = "    " + audioArchive.entries()[*browserRow.memberIndex].name;
                else {
                    const auto& entry = archive.entries()[*browserRow.memberIndex];
                    label = "    " + entry.name + (supported(entry) ? "" : "  [unsupported]");
                }
            } else {
                label = ((file.extension == ".pck" || file.extension == ".mpk") ?
                    (expandedFileIndex == browserRow.fileIndex ? "[-] " : "[+] ") : "") +
                    file.displayPath + (supported(file) ? "" : "  [unsupported]");
            }
            spyhunter::gui::text(renderer, rowRect.x + 6.0f, rowRect.y + 4.0f, spyhunter::gui::shorten(label, 43));
        }
        SDL_SetRenderDrawColor(renderer, 180, 190, 205, 255);
        spyhunter::gui::text(renderer, browserLeft + 16.0f, height - 48.0f, "Click / Enter: view or play | Esc: stop");
        spyhunter::gui::text(renderer, browserLeft + 16.0f, height - 28.0f, spyhunter::gui::shorten(status, 44));
        if (!captureArgument.empty()) {
            SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
            if (!pixels || !SDL_SaveBMP(pixels, captureArgument.c_str())) {
                SDL_Log("Viewer capture failed: %s", SDL_GetError()); exitCode = 1;
            }
            SDL_DestroySurface(pixels);
            running = false;
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    cutscene.stop();
    audioPlayer.stop();
    SDL_DestroyTexture(stillTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
