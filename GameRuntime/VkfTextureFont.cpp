#include "VkfTextureFont.h"

#include "PckArchive.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
constexpr std::size_t kPaletteEntries = 256;
constexpr std::size_t kGlyphCount = 99;
constexpr std::uint16_t kControlCodes[]{8, 9, 10, 13};

std::uint16_t read16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return std::uint16_t(bytes[offset]) | std::uint16_t(bytes[offset + 1] << 8);
}

float readFloat(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    float result = 0;
    std::memcpy(&result, bytes.data() + offset, sizeof(result));
    return result;
}

bool endsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size()) return false;
    text.remove_prefix(text.size() - suffix.size());
    return std::equal(text.begin(), text.end(), suffix.begin(), suffix.end(),
        [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); });
}
}

VkfTextureFont::~VkfTextureFont()
{
    unload();
}

void VkfTextureFont::unload()
{
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    glyphs_ = {};
}

bool VkfTextureFont::loadFromPackedArchive(const std::filesystem::path& archivePath,
                                           std::string_view memberName, SDL_Renderer* renderer,
                                           std::string& error)
{
    PckArchive archive;
    if (!archive.open(archivePath, error)) return false;
    const auto& entries = archive.entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const PckEntry& entry) {
        return entry.type == "vkTextureFont" && endsWithIgnoreCase(entry.name, memberName);
    });
    if (found == entries.end()) { error = "VKF font was not found in " + archivePath.string(); return false; }
    if (found->size > (std::numeric_limits<std::size_t>::max)()) {
        error = "VKF font entry is too large."; return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(found->size));
    if (archive.readEntry(static_cast<std::size_t>(found - entries.begin()), 0, bytes.data(), bytes.size(), error) != bytes.size())
        return false;
    return loadFromBytes(bytes, renderer, error);
}

bool VkfTextureFont::loadFromBytes(const std::vector<std::uint8_t>& bytes, SDL_Renderer* renderer, std::string& error)
{
    error.clear();
    if (!renderer) { error = "VKF font needs an SDL renderer."; return false; }

    // This retail vkTextureFont embeds one PSMT8 vkDMATexture with a 256-entry RGBA CLUT.
    // The offsets follow the observed 3.0.87 serialized texture header in INTRO.PCK.
    constexpr char textureType[] = "vkDMATexture";
    const auto marker = std::search(bytes.begin(), bytes.end(), std::begin(textureType), std::end(textureType));
    if (marker == bytes.end()) { error = "VKF has no embedded vkDMATexture."; return false; }
    const std::size_t version = static_cast<std::size_t>(marker - bytes.begin()) + sizeof(textureType);
    if (version + 4 > bytes.size()) { error = "VKF texture header is truncated."; return false; }
    const std::uint32_t textureVersion = std::uint32_t(bytes[version]) |
        (std::uint32_t(bytes[version + 1]) << 8) | (std::uint32_t(bytes[version + 2]) << 16) |
        (std::uint32_t(bytes[version + 3]) << 24);
    // The retail archives contain both 3.0.87 (INTRO.PCK) and the more compact
    // 3.0.56 header (MENU.PCK). Their payloads share the same PSMT8/CLUT encoding.
    const bool compactHeader = textureVersion == 0x03000038;
    const bool legacyMenuHeader = textureVersion == 0x0300004f;
    if (!compactHeader && !legacyMenuHeader && textureVersion != 0x03000057) {
        char versionText[32]{};
        std::snprintf(versionText, sizeof(versionText), "0x%08X", textureVersion);
        error = std::string("Unsupported VKF texture serialization version ") + versionText + ".";
        return false;
    }
    const std::size_t widthOffset = compactHeader ? 0x13 : 0x0c;
    const std::size_t heightOffset = compactHeader ? 0x15 : 0x11;
    const std::size_t formatOffset = compactHeader ? 0x17 : 0x1b;
    const std::size_t palette = version + (compactHeader ? 0x21 : legacyMenuHeader ? 0x31 : 0x32);
    const std::size_t mipCount = palette + kPaletteEntries * 4;
    const std::size_t pixels = mipCount + 1;
    if (palette > bytes.size() || version + formatOffset >= bytes.size()) { error = "VKF texture header is truncated."; return false; }
    const std::size_t atlasWidth = read16(bytes, version + widthOffset);
    const std::size_t atlasHeight = read16(bytes, version + heightOffset);
    if (!atlasWidth || !atlasHeight || atlasWidth > 1024 || atlasHeight > 1024 ||
        atlasWidth > (std::numeric_limits<std::size_t>::max)() / atlasHeight) {
        error = "VKF texture dimensions are invalid."; return false;
    }
    const std::size_t pixelCount = atlasWidth * atlasHeight;
    if (pixels + pixelCount > bytes.size() || bytes[version + formatOffset] != 0x13 ||
        bytes[mipCount] != 1) {
        error = "Unsupported VKF texture layout (expected PSMT8 with one mip).";
        return false;
    }

    // Both versions store four control records followed by 95 printable ASCII
    // records. 3.0.87 uses a 16-bit count/code; 3.0.56 uses 32-bit fields.
    const std::size_t countSize = compactHeader ? 4 : 2;
    const std::size_t codeSize = compactHeader ? 4 : 2;
    const std::size_t glyphRecordSize = codeSize + 10 * sizeof(float);
    std::size_t table = bytes.size();
    for (std::size_t cursor = pixels + pixelCount; cursor + countSize + kGlyphCount * glyphRecordSize <= bytes.size(); ++cursor) {
        const auto count = compactHeader
            ? (std::uint32_t(bytes[cursor]) | (std::uint32_t(bytes[cursor + 1]) << 8) |
               (std::uint32_t(bytes[cursor + 2]) << 16) | (std::uint32_t(bytes[cursor + 3]) << 24))
            : read16(bytes, cursor);
        if (count != kGlyphCount) continue;
        const std::size_t candidate = cursor + countSize;
        bool valid = true;
        for (std::size_t index = 0; index < kGlyphCount; ++index) {
            const auto expected = index < std::size(kControlCodes) ? kControlCodes[index] :
                static_cast<std::uint16_t>(index - std::size(kControlCodes) + 32);
            if (read16(bytes, candidate + index * glyphRecordSize) != expected ||
                (compactHeader && read16(bytes, candidate + index * glyphRecordSize + 2) != 0)) {
                valid = false; break;
            }
        }
        if (valid) { table = candidate; break; }
    }
    if (table == bytes.size()) { error = "VKF ASCII glyph table was not found."; return false; }

    std::array<Glyph, 128> decoded{};
    for (std::size_t index = 0; index < kGlyphCount; ++index) {
        const std::size_t record = table + index * glyphRecordSize;
        Glyph glyph;
        const float u0 = readFloat(bytes, record + codeSize);
        const float v0 = readFloat(bytes, record + codeSize + 4);
        const float u1 = readFloat(bytes, record + codeSize + 8);
        const float v1 = readFloat(bytes, record + codeSize + 12);
        glyph.width = readFloat(bytes, record + codeSize + 16);
        glyph.height = readFloat(bytes, record + codeSize + 20);
        glyph.originX = readFloat(bytes, record + codeSize + 24);
        glyph.originY = readFloat(bytes, record + codeSize + 28);
        glyph.advanceX = readFloat(bytes, record + codeSize + 32);
        glyph.advanceY = readFloat(bytes, record + codeSize + 36);
        if (!std::isfinite(u0) || !std::isfinite(v0) || !std::isfinite(u1) || !std::isfinite(v1) ||
            !std::isfinite(glyph.width) || !std::isfinite(glyph.height) ||
            !std::isfinite(glyph.originX) || !std::isfinite(glyph.originY) ||
            !std::isfinite(glyph.advanceX) || !std::isfinite(glyph.advanceY) ||
            u0 < 0 || v0 < 0 || u1 > 1 || v1 > 1 || u1 < u0 || v1 < v0 ||
            glyph.width < 0 || glyph.height < 0 || glyph.width > atlasWidth || glyph.height > atlasHeight) {
            error = "VKF glyph metrics are outside the atlas."; return false;
        }
        glyph.source = {u0 * atlasWidth, v0 * atlasHeight, glyph.width, glyph.height};
        glyph.present = true;
        decoded[read16(bytes, record)] = glyph;
    }

    std::vector<std::uint8_t> rgba(pixelCount * 4);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const std::uint8_t color = bytes[pixels + index];
        // PS2 CSM1 swaps each adjacent pair of eight-color CLUT groups.
        const std::size_t paletteIndex = (color & ~std::size_t{0x18}) |
                                         ((color & 0x08) << 1) | ((color & 0x10) >> 1);
        const std::size_t source = palette + paletteIndex * 4;
        const std::size_t target = index * 4;
        rgba[target] = bytes[source];
        rgba[target + 1] = bytes[source + 1];
        rgba[target + 2] = bytes[source + 2];
        rgba[target + 3] = static_cast<std::uint8_t>((std::min)(255, int(bytes[source + 3]) * 2));
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                              static_cast<int>(atlasWidth), static_cast<int>(atlasHeight));
    if (!texture || !SDL_UpdateTexture(texture, nullptr, rgba.data(), static_cast<int>(atlasWidth * 4)) ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        error = SDL_GetError();
        if (texture) SDL_DestroyTexture(texture);
        return false;
    }
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = texture;
    glyphs_ = decoded;
    return true;
}

void VkfTextureFont::drawText(SDL_Renderer* renderer, std::string_view text, float x, float yFromBottom,
                              float canvasHeight, float scale, SDL_Color color) const
{
    if (!texture_ || !renderer) return;
    SDL_SetTextureColorMod(texture_, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture_, color.a);
    float penX = x;
    float penY = yFromBottom;
    for (const unsigned char character : text) {
        if (character >= glyphs_.size() || !glyphs_[character].present) continue;
        const auto& glyph = glyphs_[character];
        if (glyph.width > 0 && glyph.height > 0) {
            const SDL_FRect destination{penX + glyph.originX * scale,
                                        canvasHeight - (penY + glyph.originY * scale) - glyph.height * scale,
                                        glyph.width * scale, glyph.height * scale};
            // The archived atlas scanlines are bottom-up relative to the glyph UVs.
            SDL_RenderTextureRotated(renderer, texture_, &glyph.source, &destination, 0.0, nullptr, SDL_FLIP_VERTICAL);
        }
        penX += glyph.advanceX * scale;
        penY += glyph.advanceY * scale;
        if (character == '\n' || character == '\r') penX = x;
    }
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
}

float VkfTextureFont::textAdvance(std::string_view text, float scale) const
{
    float advance = 0;
    for (const unsigned char character : text) {
        if (character < glyphs_.size() && glyphs_[character].present)
            advance += glyphs_[character].advanceX * scale;
    }
    return advance;
}
}
