#include "pm25/canvas.hpp"

#include <algorithm>
#include <cstdlib>

namespace pm25 {

void Canvas::clear(std::uint16_t color) {
    std::fill_n(pixels_, kDisplayWidth * kDisplayHeight, color);
}

void Canvas::pixel(int x, int y, std::uint16_t color) {
    if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
        pixels_[y * kDisplayWidth + x] = color;
    }
}

void Canvas::fill_rect(int x, int y, int width, int height, std::uint16_t color) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(kDisplayWidth, x + width);
    const int bottom = std::min(kDisplayHeight, y + height);
    for (int row = top; row < bottom; ++row) {
        std::fill(pixels_ + row * kDisplayWidth + left,
                  pixels_ + row * kDisplayWidth + right, color);
    }
}

void Canvas::rect(int x, int y, int width, int height, std::uint16_t color, int thickness) {
    fill_rect(x, y, width, thickness, color);
    fill_rect(x, y + height - thickness, width, thickness, color);
    fill_rect(x, y, thickness, height, color);
    fill_rect(x + width - thickness, y, thickness, height, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, std::uint16_t color, int thickness) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        fill_rect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void Canvas::fill_circle(int cx, int cy, int radius, std::uint16_t color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                pixel(cx + x, cy + y, color);
            }
        }
    }
}

std::uint32_t Canvas::next_utf8(std::string_view text, std::size_t& offset) {
    if (offset >= text.size()) {
        return 0;
    }
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    if ((first & 0x80U) == 0) {
        return first;
    }
    int continuation = 0;
    std::uint32_t codepoint = 0;
    if ((first & 0xe0U) == 0xc0U) {
        continuation = 1;
        codepoint = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U) {
        continuation = 2;
        codepoint = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U) {
        continuation = 3;
        codepoint = first & 0x07U;
    } else {
        return '?';
    }
    while (continuation-- > 0) {
        if (offset >= text.size()) {
            return '?';
        }
        const auto byte = static_cast<std::uint8_t>(text[offset++]);
        if ((byte & 0xc0U) != 0x80U) {
            return '?';
        }
        codepoint = (codepoint << 6U) | (byte & 0x3fU);
    }
    return codepoint;
}

const FontGlyph* Canvas::find_glyph(const Font& font, std::uint32_t codepoint) {
    std::size_t low = 0;
    std::size_t high = font.glyph_count;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (font.glyphs[middle].codepoint < codepoint) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < font.glyph_count && font.glyphs[low].codepoint == codepoint) {
        return &font.glyphs[low];
    }
    return codepoint == '?' ? nullptr : find_glyph(font, '?');
}

int Canvas::text_width(std::string_view text, const Font& font) const {
    int width = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto* glyph = find_glyph(font, next_utf8(text, offset));
        width += glyph ? glyph->advance : font.nominal_height / 2;
    }
    return width;
}

int Canvas::draw_text(int x, int y, std::string_view text, const Font& font, std::uint16_t color) {
    std::size_t text_offset = 0;
    while (text_offset < text.size()) {
        const auto* glyph = find_glyph(font, next_utf8(text, text_offset));
        if (!glyph) {
            x += font.nominal_height / 2;
            continue;
        }
        const std::size_t row_bytes = (glyph->width + 7U) / 8U;
        for (std::uint8_t row = 0; row < glyph->height; ++row) {
            for (std::uint8_t column = 0; column < glyph->width; ++column) {
                const auto byte = font.bitmap[glyph->bitmap_offset + row * row_bytes + column / 8U];
                if ((byte & (0x80U >> (column % 8U))) != 0) {
                    pixel(x + glyph->x_offset + column, y + glyph->y_offset + row, color);
                }
            }
        }
        x += glyph->advance;
    }
    return x;
}

int Canvas::draw_text_centered(int center_x, int y, std::string_view text, const Font& font, std::uint16_t color) {
    return draw_text(center_x - text_width(text, font) / 2, y, text, font, color);
}

}  // namespace pm25
