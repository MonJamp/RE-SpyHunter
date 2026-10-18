#include "StillTexture.h"
#include "PckArchive.h"
#include "VkiImage.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
std::string lower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}
}

bool StillTexture::upload(SDL_Renderer* renderer, const DecodedStill& still, std::string& error)
{
    if (still.width <= 0 || still.height <= 0 || still.width > (std::numeric_limits<int>::max)() / 4 ||
        static_cast<std::uint64_t>(still.width) * still.height * 4 != still.rgba.size()) {
        error = "Invalid RGBA still dimensions or byte count.";
        return false;
    }
    decltype(texture_) next(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC, still.width, still.height));
    if (!next || !SDL_UpdateTexture(next.get(), nullptr, still.rgba.data(), still.width * 4) ||
        !SDL_SetTextureScaleMode(next.get(), SDL_SCALEMODE_LINEAR)) {
        error = SDL_GetError();
        return false;
    }
    texture_ = std::move(next);
    return true;
}

bool StillTexture::load(SDL_Renderer* renderer, const std::filesystem::path& path, std::string& error)
{
    const auto still = decodeVki(path, error);
    return still && upload(renderer, *still, error);
}

bool StillTexture::loadPacked(SDL_Renderer* renderer, const PckArchive& archive,
                              std::string_view suffix, std::string& error)
{
    const auto wanted = lower(suffix);
    const auto& entries = archive.entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.type == "vkImage" && lower(entry.name).ends_with(wanted);
    });
    if (wanted.empty() || found == entries.end()) {
        error = "Required packed VKI image was not found: " + std::string(suffix);
        return false;
    }
    if (found->size > (std::numeric_limits<std::size_t>::max)()) {
        error = "Packed VKI exceeds addressable memory.";
        return false;
    }
    // Whole-member decode is intentional for still images, never the full archive.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(found->size));
    const auto index = static_cast<std::size_t>(found - entries.begin());
    if (archive.readEntry(index, 0, bytes.data(), bytes.size(), error) != bytes.size()) return false;
    const auto still = decodeVki(bytes, error);
    if (!still || !upload(renderer, *still, error)) return false;
    SDL_Log("Loaded %s (%s)", found->name.c_str(), still->details.c_str());
    return true;
}

void StillTexture::drawFit(SDL_Renderer* renderer, const SDL_FRect& viewport) const
{
    if (!texture_ || viewport.w <= 0 || viewport.h <= 0) return;
    float width = 0, height = 0;
    if (!SDL_GetTextureSize(texture_.get(), &width, &height) || width <= 0 || height <= 0) return;
    const float scale = (std::min)(viewport.w / width, viewport.h / height);
    const SDL_FRect destination{viewport.x + (viewport.w - width * scale) / 2.0f,
                               viewport.y + (viewport.h - height * scale) / 2.0f,
                               width * scale, height * scale};
    SDL_RenderTexture(renderer, texture_.get(), nullptr, &destination);
}
}
