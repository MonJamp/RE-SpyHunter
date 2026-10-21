#include "RgbaImage.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace spyhunter::runtime
{
namespace
{
constexpr std::size_t maximumResource = 32 * 1024 * 1024;

class Reader
{
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::span<const std::uint8_t> take(std::size_t count)
    {
        if (count > bytes_.size() - position_) throw std::runtime_error("Truncated vkDMATexture resource.");
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }
    std::uint8_t byte() { return take(1)[0]; }
    std::uint16_t shortValue() { const auto b = take(2); return b[0] | (std::uint16_t(b[1]) << 8); }
    std::uint32_t integer()
    {
        const auto b = take(4);
        return b[0] | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    }
    std::string string()
    {
        const auto length = integer();
        if (!length || length > 4096) throw std::runtime_error("Invalid texture metadata string length.");
        const auto b = take(length);
        if (b.back() != 0) throw std::runtime_error("Unterminated texture metadata string.");
        return {reinterpret_cast<const char*>(b.data()), length - 1};
    }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

std::array<std::uint8_t, 4> color(std::span<const std::uint8_t> data, std::size_t index, unsigned format)
{
    if (format == 0) {
        const auto p = data.subspan(index * 4, 4);
        return {p[0], p[1], p[2], static_cast<std::uint8_t>((std::min)(255, int(p[3]) * 2))};
    }
    const auto p = data.subspan(index * 2, 2);
    const unsigned value = p[0] | (unsigned(p[1]) << 8);
    const auto channel = [](unsigned v) { v &= 31; return static_cast<std::uint8_t>((v << 3) | (v >> 2)); };
    // Raw RGB5A1 preview. In-game 16-bit alpha additionally depends on GS TEXA state.
    return {channel(value), channel(value >> 5), channel(value >> 10), static_cast<std::uint8_t>(value & 0x8000 ? 255 : 0)};
}
}

std::optional<DecodedStill> decodeRgba(std::span<const std::uint8_t> bytes, std::string& error)
{
    error.clear();
    try {
        if (bytes.size() > maximumResource) throw std::runtime_error("Texture resource exceeds the 32 MiB limit.");
        Reader reader(bytes);
        if (reader.integer() != 0xba773265 || (reader.integer() >> 24) != 3 || reader.integer() != 4 ||
            reader.string() != "vkPlugIns") throw std::runtime_error("Expected a serialized vkDMATexture entry, not raw RGBA pixels.");
        reader.integer(); // Plugin serialization version.
        const auto plugins = reader.integer();
        if (plugins > 32) throw std::runtime_error("Invalid texture plugin count.");
        for (unsigned i = 0; i < plugins; ++i) {
            for (int field = 0; field < 4; ++field) reader.string();
            reader.take(12);
        }
        if (reader.string() != "vkDMATexture") throw std::runtime_error("Resource is not a vkDMATexture.");
        const auto version = reader.integer();
        if (version < 0x03000036 || version > 0x03000057)
            throw std::runtime_error("Unsupported vkDMATexture serialization version.");
        // Loader resource-path field: usually empty in newer entries, but older
        // 3.0.56/61 textures store a second length-prefixed name here.
        const auto resourcePathLength = reader.integer();
        if (resourcePathLength > 4096) throw std::runtime_error("Invalid texture resource-path length.");
        if (resourcePathLength && reader.take(resourcePathLength).back() != 0)
            throw std::runtime_error("Unterminated texture resource path.");
        const auto name = reader.string();
        reader.shortValue(); // vkNamedBase flags.
        const auto references = reader.integer();
        if (references > 4096) throw std::runtime_error("Invalid named-resource reference count.");
        reader.take(references * 4);
        // read__12vkDMATexture at SLUS_200.56 VA 0x002B7388.
        if (version > 0x03000036) {
            if (version > 0x0300003b) reader.integer();
            if (reader.byte()) throw std::runtime_error("Texture references an external resource; no embedded pixels to preview.");
        }
        const unsigned width = reader.shortValue(), height = reader.shortValue();
        if (!width || !height || width > 1024 || height > 1024)
            throw std::runtime_error("Invalid texture dimensions (expected 1..1024).");
        const unsigned format = reader.byte();
        const unsigned bits = format == 0 ? 32 : (format == 2 || format == 10) ? 16 :
                              format == 0x13 ? 8 : format == 0x14 ? 4 : 0;
        if (!bits) throw std::runtime_error("Unsupported PS2 texture pixel format " + std::to_string(format) + ".");
        reader.take(3);
        if (version >= 0x03000054) reader.byte();
        reader.take(2);
        if (version > 0x0300003c) reader.take(10);
        std::span<const std::uint8_t> palette;
        unsigned paletteFormat = 0, paletteCount = 0;
        if (format == 0x13 || format == 0x14) {
            paletteCount = version >= 0x03000040 ? reader.shortValue() : 1;
            if (paletteCount > 256) throw std::runtime_error("Invalid texture color-table count.");
            if (!paletteCount) { reader.shortValue(); throw std::runtime_error("Indexed texture has no embedded color table."); }
            for (unsigned i = 0; i < paletteCount; ++i) {
                const unsigned tableFormat = reader.shortValue(), tableSize = reader.shortValue();
                if (tableFormat != 0 && tableFormat != 2 && tableFormat != 10)
                    throw std::runtime_error("Unsupported texture palette format.");
                const unsigned required = (format == 0x13 ? 256 : 16) * (tableFormat == 0 ? 4 : 2);
                if (tableSize < required) throw std::runtime_error("Texture color table is too short.");
                const auto table = reader.take(tableSize);
                if (!i) { palette = table; paletteFormat = tableFormat; }
            }
        }
        const unsigned mipCount = reader.byte();
        if (!mipCount || mipCount > 11) throw std::runtime_error("Invalid texture mip count (expected 1..11).");
        std::span<const std::uint8_t> pixels;
        unsigned mipWidth = width, mipHeight = height;
        for (unsigned level = 0; level < mipCount; ++level) {
            const auto mip = reader.take((std::size_t(mipWidth) * mipHeight * bits + 7) / 8);
            if (!level) pixels = mip;
            mipWidth = (std::max)(1u, mipWidth / 2); mipHeight = (std::max)(1u, mipHeight / 2);
        }
        DecodedStill result;
        result.width = static_cast<int>(width); result.height = static_cast<int>(height);
        result.rgba.resize(std::size_t(width) * height * 4);
        for (std::size_t index = 0; index < std::size_t(width) * height; ++index) {
            std::array<std::uint8_t, 4> value;
            if (bits >= 16) value = color(pixels, index, format);
            else {
                const unsigned packed = pixels[bits == 4 ? index / 2 : index];
                // Retail PSMT4 payloads store the left pixel in the high nibble.
                // The main stencil edge verifies this: low-first swaps adjacent pixels.
                unsigned entry = bits == 4 ? (packed >> ((1 - (index & 1)) * 4)) & 15 : packed;
                // CSM1's 256-color table swaps index bits 3/4; the 16-color table does not.
                if (bits == 8) entry = (entry & ~0x18u) | ((entry & 8) << 1) | ((entry & 16) >> 1);
                value = color(palette, entry, paletteFormat);
            }
            const std::size_t destination = ((height - 1 - index / width) * width + index % width) * 4;
            std::copy(value.begin(), value.end(), result.rgba.begin() + destination);
        }
        const char* label = format == 0 ? "PSMCT32" : format == 2 ? "PSMCT16" : format == 10 ? "PSMCT16S" :
                            format == 0x13 ? "PSMT8" : "PSMT4";
        result.details = name + " | " + std::to_string(width) + "x" + std::to_string(height) + " " + label +
            " | " + std::to_string(paletteCount) + " palettes, " + std::to_string(mipCount) + " mips (showing first)";
        return result;
    } catch (const std::runtime_error& failure) { error = failure.what(); return std::nullopt; }
}

std::optional<DecodedStill> decodeRgba(const std::filesystem::path& path, std::string& error)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) { error = "Could not open texture resource."; return std::nullopt; }
    const auto length = stream.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(maximumResource)) { error = "Texture resource is empty or exceeds 32 MiB."; return std::nullopt; }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read texture resource."; return std::nullopt;
    }
    return decodeRgba(std::span<const std::uint8_t>(bytes), error);
}

std::optional<DecodedStill> decodeRgba(std::istream& stream, const std::uint64_t offset,
                                       const std::uint64_t length, std::string& error)
{
    if (length == 0 || length > maximumResource ||
        offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) ||
        length > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)())) {
        error = "Texture stream extent is invalid or exceeds 32 MiB.";
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read packed texture stream.";
        return std::nullopt;
    }
    return decodeRgba(std::span<const std::uint8_t>(bytes), error);
}
}
