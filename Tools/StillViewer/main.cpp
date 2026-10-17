#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../../Configuration/SpyHunterConfig.h"
#include "../../GUI/Gui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
constexpr spyhunter::gui::BrowserLayout kBrowser;

struct StillFile { std::filesystem::path path; std::string displayPath; };

struct DecodedStill
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string details;
};

constexpr std::array<std::uint8_t, 19> kVkiImageHeader{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80, 0x02, 0xc0,
    0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x80, 0x11, 0x00
};

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool isStillExtension(const std::filesystem::path& path)
{
    const auto extension = lowercase(path.extension().string());
    return extension == ".vki" || extension == ".bmp" || extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

std::vector<StillFile> findStills(const std::filesystem::path& root)
{
    std::vector<StillFile> results;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!iterator->is_regular_file(error) || error || !isStillExtension(iterator->path())) { error.clear(); continue; }
        std::error_code relativeError;
        const auto relative = std::filesystem::relative(iterator->path(), root, relativeError);
        results.push_back({iterator->path(), relativeError ? iterator->path().string() : relative.generic_string()});
    }
    std::sort(results.begin(), results.end(), [](const StillFile& left, const StillFile& right) { return lowercase(left.displayPath) < lowercase(right.displayPath); });
    return results;
}

std::uint16_t little16(const std::vector<std::uint8_t>& bytes, const size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t little32(const std::vector<std::uint8_t>& bytes, const size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::optional<DecodedStill> decodeVki(const std::filesystem::path& path, std::string& error)
{
    std::ifstream source(path, std::ios::binary | std::ios::ate);
    if (!source) { error = "Could not open the VKI file."; return std::nullopt; }
    const auto end = source.tellg();
    if (end <= 0 || static_cast<std::uintmax_t>(end) > (std::numeric_limits<size_t>::max)()) {
        error = "The VKI file size is invalid."; return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<size_t>(end));
    source.seekg(0);
    source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!source) { error = "Could not read the complete VKI file."; return std::nullopt; }

    const auto marker = std::search(bytes.begin(), bytes.end(), kVkiImageHeader.begin(), kVkiImageHeader.end());
    if (marker == bytes.end()) { error = "Known vkImage header was not found."; return std::nullopt; }
    const size_t header = static_cast<size_t>(marker - bytes.begin());
    const auto pixelFormat = little16(bytes, header + 5);
    const auto width = little16(bytes, header + 7);
    const auto height = little16(bytes, header + 9);
    const auto components = little16(bytes, header + 11);
    const auto componentSize = little16(bytes, header + 13);
    const auto decodedSize = little32(bytes, header + 15);
    const std::uint64_t expectedSize = static_cast<std::uint64_t>(width) * height * components * componentSize;
    if (pixelFormat != 1 || width == 0 || height == 0 || components != 4 || componentSize != 1 || decodedSize != expectedSize) {
        error = "Unsupported vkImage pixel layout."; return std::nullopt;
    }

    std::vector<std::uint8_t> planar;
    planar.reserve(decodedSize);
    size_t cursor = header + kVkiImageHeader.size();
    bool terminated = false;
    while (cursor < bytes.size()) {
        const auto count = bytes[cursor++];
        if (count == 0) { terminated = true; break; }
        if (cursor >= bytes.size() || planar.size() + count > decodedSize) {
            error = "Invalid VKI RLE stream."; return std::nullopt;
        }
        planar.insert(planar.end(), count, bytes[cursor++]);
    }
    if (!terminated || planar.size() != decodedSize) {
        error = "VKI RLE stream did not produce the declared image size."; return std::nullopt;
    }

    DecodedStill decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.rgba.resize(static_cast<size_t>(width) * height * 4);
    const size_t planeSize = static_cast<size_t>(width) * height;
    for (size_t outputY = 0; outputY < height; ++outputY) {
        const size_t storedY = height - 1 - outputY;
        for (size_t x = 0; x < width; ++x) {
            const size_t planarPixel = storedY * width + x;
            const size_t outputPixel = (outputY * width + x) * 4;
            for (size_t component = 0; component < 4; ++component)
                decoded.rgba[outputPixel + component] = planar[component * planeSize + planarPixel];
        }
    }
    decoded.details = "VKI planar RLE: " + std::to_string(width) + " x " + std::to_string(height) +
                      ", RGBA8, bottom-up storage.";
    return decoded;
}

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
}

int main(int argc, char* argv[])
{
    const std::filesystem::path configPath = argc > 1 ? std::filesystem::path(argv[1]) : spyhunter::configuration::findConfigFile(std::filesystem::current_path());
    const auto configResult = spyhunter::configuration::load(configPath);
    if (!configResult.config) { SDL_Log("Configuration error: %s", configResult.error.c_str()); return 1; }
    const auto stills = findStills(configResult.config->extractedBinariesPath);

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("SpyHunter - Still Viewer", spyhunter::gui::defaultWindowWidth, spyhunter::gui::defaultWindowHeight, SDL_WINDOW_RESIZABLE, &window, &renderer) || !spyhunter::gui::configureRenderer(renderer)) {
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    size_t selected = 0, firstVisible = 0;
    size_t loadedSelection = (std::numeric_limits<size_t>::max)();
    SDL_Texture* stillTexture = nullptr;
    int stillWidth = 0, stillHeight = 0;
    std::string details = stills.empty() ? "No supported still files found." : "Select an image.";
    bool running = true;
    while (running) {
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &pixelWidth, &pixelHeight);
        const float width = pixelWidth / spyhunter::gui::defaultUiScale, height = pixelHeight / spyhunter::gui::defaultUiScale;
        const float browserLeft = (std::max)(0.0f, width - kBrowser.width);
        const size_t visibleRows = (std::max)(1, static_cast<int>((height - kBrowser.top - 44.0f) / kBrowser.rowHeight));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = false;
            else if (!stills.empty() && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_DOWN) selected = (std::min)(selected + 1, stills.size() - 1);
            else if (!stills.empty() && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_UP) selected = selected ? selected - 1 : 0;
            else if (!stills.empty() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && event.button.x / spyhunter::gui::defaultUiScale >= browserLeft && event.button.y / spyhunter::gui::defaultUiScale >= kBrowser.top) {
                const size_t row = static_cast<size_t>((event.button.y / spyhunter::gui::defaultUiScale - kBrowser.top) / kBrowser.rowHeight);
                if (firstVisible + row < stills.size() && row < visibleRows) selected = firstVisible + row;
            }
        }
        if (!stills.empty()) {
            if (selected < firstVisible) firstVisible = selected;
            if (selected >= firstVisible + visibleRows) firstVisible = selected - visibleRows + 1;
            if (selected != loadedSelection) {
                SDL_DestroyTexture(stillTexture);
                stillTexture = nullptr;
                stillWidth = stillHeight = 0;
                std::string decodeError;
                if (lowercase(stills[selected].path.extension().string()) == ".vki") {
                    if (const auto decoded = decodeVki(stills[selected].path, decodeError)) {
                        stillTexture = createTexture(renderer, *decoded, decodeError);
                        if (stillTexture) {
                            stillWidth = decoded->width;
                            stillHeight = decoded->height;
                            details = decoded->details;
                        }
                    }
                } else {
                    decodeError = "This build currently renders VKI assets; standard image decoding is pending.";
                }
                if (!stillTexture) details = "Unable to display: " + decodeError;
                loadedSelection = selected;
            }
        }
        SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255); SDL_RenderClear(renderer);
        const SDL_FRect preview{0, 0, browserLeft, height};
        spyhunter::gui::panel(renderer, preview, SDL_Color{0, 0, 0, 255});
        SDL_SetRenderDrawColor(renderer, 225, 232, 244, 255);
        spyhunter::gui::text(renderer, 24, 24, "STILL PREVIEW");
        if (!stills.empty()) {
            spyhunter::gui::text(renderer, 24, 52, stills[selected].displayPath);
            spyhunter::gui::text(renderer, 24, 76, details);
            if (stillTexture) {
                const SDL_FRect viewport{16, 104, (std::max)(0.0f, browserLeft - 32), (std::max)(0.0f, height - 120)};
                const auto destination = spyhunter::gui::fittedRect(viewport, static_cast<float>(stillWidth), static_cast<float>(stillHeight));
                SDL_RenderTexture(renderer, stillTexture, nullptr, &destination);
            }
        }
        const SDL_FRect browser{browserLeft, 0, width - browserLeft, height};
        spyhunter::gui::panel(renderer, browser, SDL_Color{27, 31, 42, 255});
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, 255); spyhunter::gui::text(renderer, browserLeft + 16, 16, "STILL BROWSER");
        SDL_SetRenderDrawColor(renderer, 160, 170, 190, 255); spyhunter::gui::text(renderer, browserLeft + 16, 36, std::to_string(stills.size()) + " image assets");
        spyhunter::gui::text(renderer, browserLeft + 16, 54, "Click / arrows: select | Esc: exit");
        for (size_t row = 0; row < visibleRows && firstVisible + row < stills.size(); ++row) {
            const size_t index = firstVisible + row;
            const SDL_FRect rowRect{browserLeft + 8, kBrowser.top + row * kBrowser.rowHeight, browser.w - 16, kBrowser.rowHeight - 2};
            if (index == selected) { spyhunter::gui::panel(renderer, rowRect, SDL_Color{57, 95, 152, 255}); SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); }
            else SDL_SetRenderDrawColor(renderer, 210, 218, 230, 255);
            spyhunter::gui::text(renderer, rowRect.x + 6, rowRect.y + 4, spyhunter::gui::shorten(stills[index].displayPath, 43));
        }
        SDL_RenderPresent(renderer); SDL_Delay(1);
    }
    SDL_DestroyTexture(stillTexture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
