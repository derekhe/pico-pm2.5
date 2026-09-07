#pragma once

#include "pm25/font.hpp"

#include <cstdint>
#include <string_view>

namespace pm25 {

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return static_cast<std::uint16_t>(((red & 0xf8U) << 8U) | ((green & 0xfcU) << 3U) | (blue >> 3U));
}

class Canvas {
public:
    explicit Canvas(std::uint16_t* pixels) : pixels_(pixels) {}

    void clear(std::uint16_t color);
    void pixel(int x, int y, std::uint16_t color);
    void fill_rect(int x, int y, int width, int height, std::uint16_t color);
    void rect(int x, int y, int width, int height, std::uint16_t color, int thickness = 1);
    void line(int x0, int y0, int x1, int y1, std::uint16_t color, int thickness = 1);
    void fill_circle(int cx, int cy, int radius, std::uint16_t color);
    int text_width(std::string_view text, const Font& font) const;
    int draw_text(int x, int y, std::string_view text, const Font& font, std::uint16_t color);
    int draw_text_centered(int center_x, int y, std::string_view text, const Font& font, std::uint16_t color);

private:
    static std::uint32_t next_utf8(std::string_view text, std::size_t& offset);
    static const FontGlyph* find_glyph(const Font& font, std::uint32_t codepoint);
    std::uint16_t* pixels_;
};

}  // namespace pm25
