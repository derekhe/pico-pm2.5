#pragma once

#include "pm25/canvas.hpp"
#include "pm25/core.hpp"

#include <cstdint>

namespace pm25 {

enum class Page : std::uint8_t { Home, Details, Trend, Status };

struct UiState {
    Page page = Page::Home;
    bool screen_on = true;
    bool connected = false;
    bool warming_up = true;
    bool has_reading = false;
    std::uint32_t warmup_seconds = 30;
    std::uint32_t data_age_ms = 0;
    std::uint64_t uptime_seconds = 0;
    std::uint8_t brightness_percent = 80;
    std::uint8_t trend_minutes = 30;
    PmReading latest{};
    std::uint16_t filtered_pm25 = 0;
    AverageReading hour{};
    TrendSeries trend{};
    AirQuality quality{};
    ParserStats parser{};
    std::uint32_t rx_overflows = 0;
};

void render_ui(Canvas& canvas, const UiState& state);

}  // namespace pm25
