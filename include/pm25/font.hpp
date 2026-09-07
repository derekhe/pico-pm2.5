#pragma once

#include <cstddef>
#include <cstdint>

namespace pm25 {

struct FontGlyph {
    std::uint32_t codepoint;
    std::uint32_t bitmap_offset;
    std::uint8_t width;
    std::uint8_t height;
    std::uint8_t advance;
    std::int8_t x_offset;
    std::int8_t y_offset;
};

struct Font {
    const FontGlyph* glyphs;
    std::size_t glyph_count;
    const std::uint8_t* bitmap;
    std::uint8_t nominal_height;
};

}  // namespace pm25
