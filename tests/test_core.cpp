#include "pm25/canvas.hpp"
#include "pm25/core.hpp"
#include "pm25/ui.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (false)

std::array<std::uint8_t, 32> make_frame(std::uint16_t pm1, std::uint16_t pm25, std::uint16_t pm10) {
    std::array<std::uint8_t, 32> frame{};
    frame[0] = 0x42;
    frame[1] = 0x4d;
    frame[2] = 0;
    frame[3] = 28;
    frame[10] = static_cast<std::uint8_t>(pm1 >> 8U);
    frame[11] = static_cast<std::uint8_t>(pm1);
    frame[12] = static_cast<std::uint8_t>(pm25 >> 8U);
    frame[13] = static_cast<std::uint8_t>(pm25);
    frame[14] = static_cast<std::uint8_t>(pm10 >> 8U);
    frame[15] = static_cast<std::uint8_t>(pm10);
    std::uint16_t checksum = 0;
    for (std::size_t index = 0; index < 30; ++index) {
        checksum = static_cast<std::uint16_t>(checksum + frame[index]);
    }
    frame[30] = static_cast<std::uint8_t>(checksum >> 8U);
    frame[31] = static_cast<std::uint8_t>(checksum);
    return frame;
}

void feed_bytes(pm25::PmsParser& parser, const std::uint8_t* data, std::size_t size,
                std::vector<pm25::PmReading>& readings) {
    for (std::size_t index = 0; index < size; ++index) {
        pm25::PmReading reading{};
        if (parser.feed(data[index], reading)) {
            readings.push_back(reading);
        }
    }
}

void test_parser() {
    pm25::PmsParser parser;
    std::vector<pm25::PmReading> readings;
    const auto first = make_frame(12, 34, 56);
    feed_bytes(parser, first.data(), 7, readings);
    CHECK(readings.empty());
    feed_bytes(parser, first.data() + 7, first.size() - 7, readings);
    CHECK(readings.size() == 1);
    CHECK(readings[0].pm1 == 12 && readings[0].pm25 == 34 && readings[0].pm10 == 56);

    // Back-to-back complete frames must not lose data.
    feed_bytes(parser, first.data(), first.size(), readings);
    feed_bytes(parser, first.data(), first.size(), readings);
    CHECK(readings.size() == 3);

    const std::array<std::uint8_t, 8> garbage{0, 0x42, 0, 0x42, 0x4d, 0, 27, 0};
    feed_bytes(parser, garbage.data(), garbage.size(), readings);
    const auto second = make_frame(1, 2, 3);
    feed_bytes(parser, second.data(), second.size(), readings);
    CHECK(readings.size() == 4);
    CHECK(parser.stats().length_errors >= 1);
    CHECK(parser.stats().resyncs >= 1);

    auto bad = make_frame(100, 200, 300);
    bad[20] ^= 0x55;
    feed_bytes(parser, bad.data(), bad.size(), readings);
    feed_bytes(parser, second.data(), second.size(), readings);
    CHECK(readings.size() == 5);
    CHECK(parser.stats().checksum_errors == 1);

    // A truncated frame followed by a complete frame must recover without a reset.
    feed_bytes(parser, first.data(), 18, readings);
    feed_bytes(parser, second.data(), second.size(), readings);
    feed_bytes(parser, first.data(), first.size(), readings);
    CHECK(readings.back().pm25 == 34);
    CHECK(parser.stats().valid_frames == readings.size());
}

void test_median() {
    pm25::Median5 median;
    CHECK(median.push(10) == 10);
    CHECK(median.push(100) == 10);
    CHECK(median.push(20) == 20);
    CHECK(median.push(30) == 20);
    CHECK(median.push(40) == 30);
    CHECK(median.push(5) == 30);
}

void test_iaqi() {
    const std::array<float, 8> concentrations{0, 30, 60, 115, 150, 250, 350, 500};
    const std::array<int, 8> expected{0, 50, 100, 150, 200, 300, 400, 500};
    for (std::size_t index = 0; index < concentrations.size(); ++index) {
        CHECK(pm25::calculate_pm25_iaqi(concentrations[index]).iaqi == expected[index]);
    }
    CHECK(pm25::calculate_pm25_iaqi(45).iaqi == 75);
    CHECK(pm25::calculate_pm25_iaqi(61).iaqi == 101);
    CHECK(pm25::calculate_pm25_iaqi(900).iaqi == 500);
    CHECK(pm25::calculate_pm25_iaqi(-10).iaqi == 0);
    CHECK(pm25::calculate_pm25_iaqi(30.1F).level == pm25::QualityLevel::Good);
}

void test_history() {
    pm25::MinuteHistory history;
    for (std::uint64_t minute = 0; minute < 60; ++minute) {
        history.add(minute * 60000ULL + 1000, {10, static_cast<std::uint16_t>(minute), 30});
    }
    auto hour = history.hour_average(59 * 60000ULL + 2000);
    CHECK(hour.valid);
    CHECK(hour.valid_minutes == 60);
    CHECK(!hour.complete_hour);
    CHECK(hour.samples == 60);
    CHECK(hour.pm25 > 29.4F && hour.pm25 < 29.6F);

    auto trend = history.trend(59 * 60000ULL + 2000, 30);
    CHECK(trend.count == 30);
    CHECK(trend.has_data);
    CHECK(trend.minimum == 30);
    CHECK(trend.maximum == 59);
    CHECK(trend.current == 59);

    const auto trend10 = history.trend(59 * 60000ULL + 2000, 10);
    const auto trend60 = history.trend(59 * 60000ULL + 2000, 60);
    CHECK(trend10.count == 10 && trend10.minimum == 50 && trend10.maximum == 59);
    CHECK(trend60.count == 60 && trend60.minimum == 0 && trend60.maximum == 59);

    history.add(60 * 60000ULL + 1000, {10, 60, 30});
    hour = history.hour_average(60 * 60000ULL + 1000);
    CHECK(hour.valid_minutes == 60);
    CHECK(hour.complete_hour);

    history.clear();
    history.add(0, {1, 10, 20});
    history.add(2 * 60000ULL, {1, 30, 20});
    hour = history.hour_average(2 * 60000ULL);
    CHECK(hour.valid_minutes == 2);
    CHECK(!hour.complete_hour);
    CHECK(hour.pm25 == 20);
    trend = history.trend(2 * 60000ULL, 3);
    CHECK(trend.valid[0]);
    CHECK(!trend.valid[1]);
    CHECK(trend.valid[2]);

    // The one-hour value averages valid minute means, so an uneven sample rate
    // cannot make one minute dominate the IAQI input.
    history.clear();
    history.add(1000, {1, 10, 1});
    history.add(2000, {1, 30, 1});
    history.add(60000 + 1000, {1, 100, 1});
    hour = history.hour_average(60000 + 1000);
    CHECK(hour.samples == 3);
    CHECK(hour.valid_minutes == 2);
    CHECK(hour.pm25 == 60);
}

void write_ppm(const std::filesystem::path& path, const std::array<std::uint16_t, 240 * 240>& pixels) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n240 240\n255\n";
    for (const auto pixel : pixels) {
        const unsigned char rgb[3]{
            static_cast<unsigned char>(((pixel >> 11U) & 0x1fU) * 255U / 31U),
            static_cast<unsigned char>(((pixel >> 5U) & 0x3fU) * 255U / 63U),
            static_cast<unsigned char>((pixel & 0x1fU) * 255U / 31U),
        };
        output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }
}

void generate_snapshots(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::array<std::uint16_t, 240 * 240> pixels{};
    pm25::Canvas canvas(pixels.data());
    pm25::UiState state{};
    state.connected = true;
    state.warming_up = false;
    state.has_reading = true;
    state.latest = {18, 42, 67};
    state.filtered_pm25 = 40;
    state.hour = {true, 17, 38, 64, 3600, 60, true};
    state.quality = pm25::calculate_pm25_iaqi(state.hour.pm25);
    state.parser.valid_frames = 12345;
    state.uptime_seconds = 3723;
    state.data_age_ms = 218;
    state.trend_minutes = 30;
    state.trend.count = 30;
    state.trend.has_data = true;
    state.trend.minimum = 19;
    state.trend.maximum = 52;
    state.trend.current = 40;
    state.trend.average = 35;
    for (std::size_t index = 0; index < 30; ++index) {
        state.trend.valid[index] = index != 13;
        state.trend.values[index] = 19.0F + static_cast<float>((index * 7) % 30);
    }

    for (int page = 0; page < 4; ++page) {
        state.page = static_cast<pm25::Page>(page);
        pm25::render_ui(canvas, state);
        write_ppm(directory / ("page-" + std::to_string(page) + ".ppm"), pixels);
    }

    state.page = pm25::Page::Home;
    state.warming_up = true;
    state.warmup_seconds = 23;
    pm25::render_ui(canvas, state);
    write_ppm(directory / "home-warmup.ppm", pixels);
    state.warming_up = false;
    state.connected = false;
    state.data_age_ms = 4200;
    pm25::render_ui(canvas, state);
    write_ppm(directory / "home-disconnected.ppm", pixels);
    state.connected = true;
    state.filtered_pm25 = 1;
    state.hour.pm25 = 1;
    state.quality = pm25::calculate_pm25_iaqi(state.hour.pm25);
    pm25::render_ui(canvas, state);
    write_ppm(directory / "home-very-low.ppm", pixels);
    state.filtered_pm25 = 388;
    state.hour.pm25 = 375;
    state.quality = pm25::calculate_pm25_iaqi(state.hour.pm25);
    pm25::render_ui(canvas, state);
    write_ppm(directory / "home-heavy.ppm", pixels);
}

}  // namespace

int main(int argc, char** argv) {
    test_parser();
    test_median();
    test_iaqi();
    test_history();
    if (argc == 3 && std::string(argv[1]) == "--snapshots") {
        generate_snapshots(argv[2]);
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All core tests passed");
    return EXIT_SUCCESS;
}
