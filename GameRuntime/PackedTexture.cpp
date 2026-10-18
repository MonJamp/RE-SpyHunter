#include "PackedTexture.h"

#include "PckArchive.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace spyhunter::runtime
{
namespace
{
bool endsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size()) return false;
    text.remove_prefix(text.size() - suffix.size());
    return std::equal(text.begin(), text.end(), suffix.begin(), suffix.end(),
        [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); });
}

std::uint16_t read16(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return std::uint16_t(bytes[offset]) | std::uint16_t(bytes[offset + 1] << 8);
}
}

PackedTexture::~PackedTexture() { unload(); }

void PackedTexture::unload()
{
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    width_ = height_ = 0;
}

bool PackedTexture::load(const std::filesystem::path& archivePath, const std::string_view memberName,
                         SDL_Renderer* renderer, std::string& error)
{
    unload();
    PckArchive archive;
    if (!archive.open(archivePath, error)) return false;
    const auto& entries = archive.entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const PckEntry& entry) {
        return entry.type == "vkDMATexture" && endsWithIgnoreCase(entry.name, memberName);
    });
    if (found == entries.end()) { error = "Packed texture was not found in " + archivePath.string(); return false; }
    if (found->size > (std::numeric_limits<std::size_t>::max)()) { error = "Packed texture is too large."; return false; }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(found->size));
    if (archive.readEntry(static_cast<std::size_t>(found - entries.begin()), 0, bytes.data(), bytes.size(), error) != bytes.size())
        return false;

    const auto name = std::search(bytes.begin(), bytes.end(), found->name.begin(), found->name.end());
    if (name == bytes.end()) { error = "Packed texture name field is missing."; return false; }
    const std::size_t payload = static_cast<std::size_t>(name - bytes.begin()) + found->name.size() + 1;
    // Observed SYSTEM.PCK 3.0.85 vkDMATexture payload: dimensions at +11,
    // PSMT8 format at +15, and a 256-entry RGBA palette at +38.
    constexpr std::size_t dimensionOffset = 11;
    constexpr std::size_t formatOffset = 15;
    constexpr std::size_t paletteOffset = 38;
    if (payload + paletteOffset + 1025 > bytes.size() || bytes[payload + formatOffset] != 0x13) {
        error = "Unsupported packed texture layout."; return false;
    }
    const std::size_t width = read16(bytes, payload + dimensionOffset);
    const std::size_t height = read16(bytes, payload + dimensionOffset + 2);
    if (!width || !height || width > 1024 || height > 1024 || width > (std::numeric_limits<std::size_t>::max)() / height) {
        error = "Packed texture dimensions are invalid."; return false;
    }
    const std::size_t pixelCount = width * height;
    const std::size_t palette = payload + paletteOffset;
    const std::size_t mipCount = palette + 256 * 4;
    const std::size_t pixels = mipCount + 1;
    if (!bytes[mipCount] || pixels + pixelCount > bytes.size()) { error = "Packed texture pixels are truncated."; return false; }

    std::vector<std::uint8_t> rgba(pixelCount * 4);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const std::uint8_t color = bytes[pixels + index];
        const std::size_t paletteIndex = (color & ~std::size_t{0x18}) |
                                         ((color & 0x08) << 1) | ((color & 0x10) >> 1);
        const std::size_t source = palette + paletteIndex * 4;
        // vkDMATexture's PSMT8 rows arrive in PS2 bottom-up texture order;
        // SDL's uploaded RGBA rows are top-down. Reverse rows for display.
        const std::size_t y = index / width;
        const std::size_t x = index % width;
        const std::size_t target = ((height - 1 - y) * width + x) * 4;
        rgba[target] = bytes[source];
        rgba[target + 1] = bytes[source + 1];
        rgba[target + 2] = bytes[source + 2];
        rgba[target + 3] = static_cast<std::uint8_t>((std::min)(255, int(bytes[source + 3]) * 2));
    }
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                 static_cast<int>(width), static_cast<int>(height));
    if (!texture_ || !SDL_UpdateTexture(texture_, nullptr, rgba.data(), static_cast<int>(width * 4))) {
        error = SDL_GetError(); unload(); return false;
    }
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    width_ = static_cast<int>(width);
    height_ = static_cast<int>(height);
    error.clear();
    return true;
}

void PackedTexture::draw(SDL_Renderer* renderer, const float x, const float y, float width, float height) const
{
    if (!texture_) return;
    if (width <= 0) width = static_cast<float>(width_);
    if (height <= 0) height = static_cast<float>(height_);
    const SDL_FRect destination{x, y, width, height};
    SDL_RenderTexture(renderer, texture_, nullptr, &destination);
}

void PackedTexture::drawRounded(SDL_Renderer* renderer, const SDL_FRect& bounds) const
{
    if (!texture_ || bounds.w <= 0.0f || bounds.h <= 0.0f) return;
    const float radius = (std::min)(bounds.h, bounds.w) * 0.5f;
    const int rows = static_cast<int>(std::ceil(bounds.h));
    for (int row = 0; row < rows; ++row) {
        const float dy = static_cast<float>(row) + 0.5f - bounds.h * 0.5f;
        const float inset = radius - std::sqrt((std::max)(0.0f, radius * radius - dy * dy));
        const float drawWidth = bounds.w - inset * 2.0f;
        if (drawWidth <= 0.0f) continue;
        const SDL_FRect source{inset / bounds.w * width_,
                               static_cast<float>(row) / bounds.h * height_,
                               drawWidth / bounds.w * width_,
                               1.0f / bounds.h * height_};
        const SDL_FRect destination{bounds.x + inset, bounds.y + static_cast<float>(row), drawWidth, 1.0f};
        SDL_RenderTexture(renderer, texture_, &source, &destination);
    }
}
}
