#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace spyhunter::runtime
{
// Source triangle slots are retained, including degenerates. Winding restarts
// at each strip. Call only after validateStripLayout succeeds.
template<class Visit>
void forEachStripTriangle(std::span<const std::uint16_t> lengths, Visit&& visit)
{
    std::uint32_t base = 0;
    for (std::size_t strip = 0; strip < lengths.size(); ++strip) {
        for (std::uint32_t i = 2; i < lengths[strip]; ++i) {
            const auto a = base + i - 2, b = base + i - 1, c = base + i;
            visit(strip, i % 2 == 0 ? std::array<std::uint32_t, 3>{a, b, c}
                                    : std::array<std::uint32_t, 3>{b, a, c});
        }
        base += lengths[strip];
    }
}

inline bool validateStripLayout(std::span<const std::uint16_t> lengths,
                                std::size_t vertexCount)
{
    if (vertexCount > (std::numeric_limits<std::uint32_t>::max)()) return false;
    std::uint64_t count = 0;
    for (const auto length : lengths) {
        if (length < 3) return false;
        count += length;
        if (count > vertexCount) return false;
    }
    return count == vertexCount;
}
}
