#include "pm25/ui.hpp"

#include "fonts_generated.hpp"

#include <algorithm>
#include <cstdio>

namespace pm25 {
namespace {

constexpr auto kBackground = rgb565(8, 13, 22);
constexpr auto kPanel = rgb565(18, 27, 41);
constexpr auto kPanelLight = rgb565(27, 39, 57);
constexpr auto kText = rgb565(235, 241, 248);
constexpr auto kMuted = rgb565(125, 140, 158);
constexpr auto kGrid = rgb565(47, 61, 78);
constexpr auto kDisconnected = rgb565(105, 111, 119);

std::uint16_t quality_color(QualityLevel level) {
    switch (level) {
        case QualityLevel::Excellent: return rgb565(41, 196, 120);
        case QualityLevel::Good: return rgb565(239, 204, 54);
        case QualityLevel::Light: return rgb565(245, 139, 46);
        case QualityLevel::Moderate: return rgb565(232, 65, 79);
        case QualityLevel::Heavy: return rgb565(150, 75, 190);
        case QualityLevel::Severe: return rgb565(126, 38, 68);
    }
    return kDisconnected;
}

const char* page_title(Page page) {
    switch (page) {
        case Page::Home: return "空气";
        case Page::Details: return "颗粒物明细";
        case Page::Trend: return "PM2.5 趋势";
        case Page::Status: return "设备状态";
    }
    return "";
}

void draw_header(Canvas& canvas, const UiState& state) {
    canvas.draw_text(10, 9, page_title(state.page), font18, kText);
    const auto dot_color = state.connected ? rgb565(47, 210, 136) : rgb565(233, 76, 86);
    canvas.fill_circle(218, 17, 5, dot_color);
    if (state.warming_up) {
        canvas.fill_circle(202, 17, 3, rgb565(244, 166, 47));
    }
    canvas.line(10, 34, 230, 34, kGrid);
}

void draw_footer(Canvas& canvas, Page page) {
    constexpr int first_x = 102;
    const int selected = static_cast<int>(page);
    for (int index = 0; index < 4; ++index) {
        canvas.fill_circle(first_x + index * 12, 230, index == selected ? 4 : 2,
                           index == selected ? kText : kMuted);
    }
}

void draw_connection_text(Canvas& canvas, const UiState& state, int y) {
    char text[48]{};
    std::uint16_t color = kMuted;
    if (state.warming_up) {
        std::snprintf(text, sizeof(text), "预热 %lu 秒", static_cast<unsigned long>(state.warmup_seconds));
        color = rgb565(244, 166, 47);
    } else if (!state.connected) {
        std::snprintf(text, sizeof(text), "传感器中断");
        color = rgb565(233, 76, 86);
    } else {
        std::snprintf(text, sizeof(text), "传感器已连接");
        color = rgb565(47, 210, 136);
    }
    canvas.draw_text_centered(120, y, text, font14, color);
}

void draw_home(Canvas& canvas, const UiState& state) {
    const auto accent = state.connected && state.has_reading ? quality_color(state.quality.level) : kDisconnected;
    canvas.draw_text_centered(120, 43, "PM2.5", font18, kMuted);

    char number[12]{};
    if (state.has_reading) {
        std::snprintf(number, sizeof(number), "%u", state.filtered_pm25);
    } else {
        std::snprintf(number, sizeof(number), "--");
    }
    canvas.draw_text_centered(120, 65, number, font64, accent);
    canvas.draw_text_centered(120, 126, "μg/m³", font14, kMuted);

    canvas.fill_rect(18, 151, 204, 2, kGrid);
    const int marker = 18 + std::min(204, state.quality.iaqi * 204 / 500);
    canvas.fill_rect(18, 151, std::max(2, marker - 18), 2, accent);
    canvas.fill_circle(marker, 152, 4, accent);

    char iaqi[40]{};
    if (state.hour.valid) {
        std::snprintf(iaqi, sizeof(iaqi), "%s IAQI %d",
                      state.hour.complete_hour ? "参考" : "预估", state.quality.iaqi);
    } else {
        std::snprintf(iaqi, sizeof(iaqi), "预估 IAQI --");
    }
    canvas.draw_text_centered(120, 165, iaqi, font18, kText);
    canvas.draw_text_centered(120, 191,
                              state.hour.valid ? quality_label_zh(state.quality.level) : "无数据",
                              font18, accent);
    draw_connection_text(canvas, state, 211);
}

void draw_metric_row(Canvas& canvas, int y, const char* label, bool has_current, std::uint16_t current,
                     bool average_valid, float average, std::uint16_t accent) {
    canvas.fill_rect(10, y, 220, 47, kPanel);
    canvas.fill_rect(10, y, 4, 47, accent);
    canvas.draw_text(22, y + 7, label, font18, kText);
    char value[24]{};
    if (has_current) {
        std::snprintf(value, sizeof(value), "%u", current);
    } else {
        std::snprintf(value, sizeof(value), "--");
    }
    canvas.draw_text(111, y + 4, value, font24, accent);
    if (average_valid) {
        std::snprintf(value, sizeof(value), "1h %.0f", static_cast<double>(average));
    } else {
        std::snprintf(value, sizeof(value), "1h --");
    }
    canvas.draw_text(171, y + 13, value, font14, kMuted);
}

void draw_details(Canvas& canvas, const UiState& state) {
    const auto accent = state.connected ? quality_color(state.quality.level) : kDisconnected;
    const auto pm1_color = state.connected ? rgb565(64, 180, 228) : kDisconnected;
    const auto pm10_color = state.connected ? rgb565(169, 119, 226) : kDisconnected;
    draw_metric_row(canvas, 43, "PM1.0", state.has_reading, state.latest.pm1,
                    state.hour.valid, state.hour.pm1, pm1_color);
    draw_metric_row(canvas, 96, "PM2.5", state.has_reading, state.filtered_pm25,
                    state.hour.valid, state.hour.pm25, accent);
    draw_metric_row(canvas, 149, "PM10", state.has_reading, state.latest.pm10,
                    state.hour.valid, state.hour.pm10, pm10_color);
    canvas.draw_text_centered(120, 204, "当前 / 1小时平均  μg/m³", font14, kMuted);
}

void draw_trend(Canvas& canvas, const UiState& state) {
    constexpr int left = 14;
    constexpr int top = 50;
    constexpr int width = 212;
    constexpr int height = 104;
    const auto accent = state.connected ? quality_color(state.quality.level) : kDisconnected;

    char range[24]{};
    std::snprintf(range, sizeof(range), "%u 分钟", state.trend_minutes);
    canvas.draw_text(195 - canvas.text_width(range, font14), 10, range, font14, kMuted);
    canvas.fill_rect(left, top, width, height, kPanel);
    for (int line_index = 1; line_index < 4; ++line_index) {
        const int y = top + line_index * height / 4;
        canvas.line(left, y, left + width - 1, y, kGrid);
    }

    if (!state.trend.has_data) {
        canvas.draw_text_centered(120, 91, "无数据", font18, kMuted);
    } else {
        const float scale_max = std::max(60.0F, state.trend.maximum * 1.15F);
        bool have_previous = false;
        int previous_x = 0;
        int previous_y = 0;
        for (std::size_t index = 0; index < state.trend.count; ++index) {
            if (!state.trend.valid[index]) {
                have_previous = false;
                continue;
            }
            const int x = left + static_cast<int>(index * (width - 1) / std::max<std::size_t>(1, state.trend.count - 1));
            const int y = top + height - 1 - static_cast<int>(std::min(scale_max, state.trend.values[index]) * (height - 5) / scale_max);
            if (have_previous) {
                canvas.line(previous_x, previous_y, x, y, accent, 2);
            }
            canvas.fill_circle(x, y, 2, accent);
            previous_x = x;
            previous_y = y;
            have_previous = true;
        }
    }

    char stats[32]{};
    std::snprintf(stats, sizeof(stats), "当前 %.0f", static_cast<double>(state.trend.current));
    canvas.draw_text(14, 165, stats, font14, kText);
    std::snprintf(stats, sizeof(stats), "平均 %.0f", static_cast<double>(state.trend.average));
    canvas.draw_text(128, 165, stats, font14, kText);
    std::snprintf(stats, sizeof(stats), "最低 %.0f", static_cast<double>(state.trend.minimum));
    canvas.draw_text(14, 190, stats, font14, kMuted);
    std::snprintf(stats, sizeof(stats), "最高 %.0f", static_cast<double>(state.trend.maximum));
    canvas.draw_text(128, 190, stats, font14, kMuted);
}

void draw_status_row(Canvas& canvas, int y, const char* label, const char* value, std::uint16_t value_color = kText) {
    canvas.draw_text(14, y, label, font14, kMuted);
    const int width = canvas.text_width(value, font14);
    canvas.draw_text(226 - width, y, value, font14, value_color);
    canvas.line(14, y + 21, 226, y + 21, kGrid);
}

void draw_status(Canvas& canvas, const UiState& state) {
    char value[40]{};
    if (state.warming_up) {
        std::snprintf(value, sizeof(value), "预热 %lu 秒", static_cast<unsigned long>(state.warmup_seconds));
    } else {
        std::snprintf(value, sizeof(value), "%s", state.connected ? "已连接" : "中断");
    }
    const auto sensor_color = state.warming_up ? rgb565(244, 166, 47)
        : (state.connected ? rgb565(47, 210, 136) : rgb565(233, 76, 86));
    draw_status_row(canvas, 43, "传感器", value, sensor_color);
    std::snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(state.data_age_ms));
    draw_status_row(canvas, 70, "数据时间", state.has_reading ? value : "--");
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(state.parser.valid_frames));
    draw_status_row(canvas, 97, "有效帧", value);
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(state.parser.checksum_errors));
    draw_status_row(canvas, 124, "校验错误", value);
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(state.parser.resyncs + state.rx_overflows));
    draw_status_row(canvas, 151, "重同步", value);
    const auto hours = state.uptime_seconds / 3600;
    const auto minutes = (state.uptime_seconds / 60) % 60;
    std::snprintf(value, sizeof(value), "%lluh %02llum",
                  static_cast<unsigned long long>(hours), static_cast<unsigned long long>(minutes));
    draw_status_row(canvas, 178, "运行时间", value);
    canvas.draw_text(14, 207, "固件", font14, kMuted);
    canvas.draw_text(158, 207, "v1.0.0", font14, kText);
}

}  // namespace

void render_ui(Canvas& canvas, const UiState& state) {
    canvas.clear(kBackground);
    draw_header(canvas, state);
    switch (state.page) {
        case Page::Home: draw_home(canvas, state); break;
        case Page::Details: draw_details(canvas, state); break;
        case Page::Trend: draw_trend(canvas, state); break;
        case Page::Status: draw_status(canvas, state); break;
    }
    draw_footer(canvas, state.page);
}

}  // namespace pm25
