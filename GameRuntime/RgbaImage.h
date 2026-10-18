#pragma once

#include "VkiImage.h"
#include <span>

namespace spyhunter::runtime
{
// Serialized vkDMATexture resource, including the entry envelope (not raw RGBA bytes).
// Decodes the first color table/base mip, using the frontend's bottom-up raster convention.
std::optional<DecodedStill> decodeRgba(std::span<const std::uint8_t> bytes, std::string& error);
std::optional<DecodedStill> decodeRgba(const std::filesystem::path& path, std::string& error);
}
