#include "VkiImage.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
constexpr std::array<std::uint8_t, 19> kVkiImageHeader{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80, 0x02, 0xc0,
    0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x80, 0x11, 0x00
};

bool little16(const std::vector<std::uint8_t>& bytes, const size_t offset, std::uint16_t& value)
{
    if (offset + 2 > bytes.size()) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    return true;
}

bool little32(const std::vector<std::uint8_t>& bytes, const size_t offset, std::uint32_t& value)
{
    if (offset + 4 > bytes.size()) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    return true;
}
}

std::optional<DecodedStill> decodeVki(const std::vector<std::uint8_t>& bytes, std::string& error)
{
    const auto marker = std::search(bytes.begin(), bytes.end(), kVkiImageHeader.begin(), kVkiImageHeader.end());
    if (marker == bytes.end()) { error = "Known vkImage header was not found."; return std::nullopt; }
    const size_t header = static_cast<size_t>(marker - bytes.begin());
    std::uint16_t pixelFormat = 0, width = 0, height = 0, components = 0, componentSize = 0;
    std::uint32_t decodedSize = 0;
    if (!little16(bytes, header + 5, pixelFormat) || !little16(bytes, header + 7, width) ||
        !little16(bytes, header + 9, height) || !little16(bytes, header + 11, components) ||
        !little16(bytes, header + 13, componentSize) || !little32(bytes, header + 15, decodedSize)) {
        error = "The VKI image header is truncated."; return std::nullopt;
    }
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
    return decodeVki(bytes, error);
}
}
