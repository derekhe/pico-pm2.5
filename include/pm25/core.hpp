#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pm25 {

struct PmReading {
    std::uint16_t pm1 = 0;
    std::uint16_t pm25 = 0;
    std::uint16_t pm10 = 0;
};

struct ParserStats {
    std::uint32_t valid_frames = 0;
    std::uint32_t checksum_errors = 0;
    std::uint32_t length_errors = 0;
    std::uint32_t resyncs = 0;
};

class PmsParser {
public:
    bool feed(std::uint8_t byte, PmReading& reading);
    const ParserStats& stats() const { return stats_; }
    void reset();

private:
    void discard_front(std::size_t count);

    std::array<std::uint8_t, 64> buffer_{};
    std::size_t used_ = 0;
    ParserStats stats_{};
};

class Median5 {
public:
    std::uint16_t push(std::uint16_t value);
    std::uint16_t value() const;
    std::size_t size() const { return count_; }
    void reset();

private:
    std::array<std::uint16_t, 5> values_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};

struct AverageReading {
    bool valid = false;
    float pm1 = 0;
    float pm25 = 0;
    float pm10 = 0;
    std::uint32_t samples = 0;
    std::uint8_t valid_minutes = 0;
    bool complete_hour = false;
};

struct TrendSeries {
    std::array<float, 60> values{};
    std::array<bool, 60> valid{};
    std::size_t count = 0;
    bool has_data = false;
    float current = 0;
    float average = 0;
    float minimum = 0;
    float maximum = 0;
};

class MinuteHistory {
public:
    void add(std::uint64_t now_ms, const PmReading& reading);
    AverageReading hour_average(std::uint64_t now_ms) const;
    TrendSeries trend(std::uint64_t now_ms, std::size_t minutes) const;
    void clear();

private:
    struct Bucket {
        std::uint64_t minute_id = 0;
        std::uint32_t pm1_sum = 0;
        std::uint32_t pm25_sum = 0;
        std::uint32_t pm10_sum = 0;
        std::uint32_t samples = 0;
        bool occupied = false;
    };

    const Bucket* find(std::uint64_t minute_id) const;
    std::array<Bucket, 60> buckets_{};
    std::uint64_t first_sample_ms_ = 0;
    bool has_first_sample_ = false;
};

enum class QualityLevel : std::uint8_t {
    Excellent,
    Good,
    Light,
    Moderate,
    Heavy,
    Severe,
};

struct AirQuality {
    int iaqi = 0;
    QualityLevel level = QualityLevel::Excellent;
};

AirQuality calculate_pm25_iaqi(float concentration);
const char* quality_label_zh(QualityLevel level);

}  // namespace pm25
