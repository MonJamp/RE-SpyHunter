#include "PackedTexture.h"

#include "PckArchive.h"
#include "RgbaImage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    if (found->size > 32ull * 1024 * 1024) { error = "Packed texture exceeds 32 MiB."; return false; }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(found->size));
    if (archive.readEntry(static_cast<std::size_t>(found - entries.begin()), 0, bytes.data(), bytes.size(), error) != bytes.size())
        return false;

    const auto decoded = decodeRgba(std::span<const std::uint8_t>(bytes), error);
    if (!decoded) return false;
    const int width = decoded->width, height = decoded->height;
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                 static_cast<int>(width), static_cast<int>(height));
    if (!texture_ || !SDL_UpdateTexture(texture_, nullptr, decoded->rgba.data(), width * 4)) {
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
