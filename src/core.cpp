#include "pm25/core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace pm25 {
namespace {

std::uint16_t read_be16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

}  // namespace

void PmsParser::discard_front(std::size_t count) {
    if (count >= used_) {
        used_ = 0;
        return;
    }
    std::memmove(buffer_.data(), buffer_.data() + count, used_ - count);
    used_ -= count;
}

void PmsParser::reset() {
    used_ = 0;
    stats_ = {};
}

bool PmsParser::feed(std::uint8_t byte, PmReading& reading) {
    if (used_ == buffer_.size()) {
        discard_front(1);
        ++stats_.resyncs;
    }
    buffer_[used_++] = byte;

    for (;;) {
        while (used_ >= 2 && (buffer_[0] != 0x42 || buffer_[1] != 0x4d)) {
            discard_front(1);
            ++stats_.resyncs;
        }
        if (used_ < 4) {
            return false;
        }

        const auto frame_length = read_be16(buffer_.data() + 2);
        if (frame_length != 28) {
            discard_front(1);
            ++stats_.length_errors;
            ++stats_.resyncs;
            continue;
        }

        constexpr std::size_t total_length = 32;
        if (used_ < total_length) {
            return false;
        }

        std::uint16_t checksum = 0;
        for (std::size_t index = 0; index < 30; ++index) {
            checksum = static_cast<std::uint16_t>(checksum + buffer_[index]);
        }
        if (checksum != read_be16(buffer_.data() + 30)) {
            discard_front(1);
            ++stats_.checksum_errors;
            ++stats_.resyncs;
            continue;
        }

        // Data 4..6 are the atmospheric-environment mass concentrations.
        reading.pm1 = read_be16(buffer_.data() + 10);
        reading.pm25 = read_be16(buffer_.data() + 12);
        reading.pm10 = read_be16(buffer_.data() + 14);
        discard_front(total_length);
        ++stats_.valid_frames;
        return true;
    }
}

void Median5::reset() {
    values_.fill(0);
    count_ = 0;
    next_ = 0;
}

std::uint16_t Median5::push(std::uint16_t value_in) {
    values_[next_] = value_in;
    next_ = (next_ + 1) % values_.size();
    if (count_ < values_.size()) {
        ++count_;
    }
    return value();
}

std::uint16_t Median5::value() const {
    if (count_ == 0) {
        return 0;
    }
    std::array<std::uint16_t, 5> sorted{};
    std::copy_n(values_.begin(), count_, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count_));
    return sorted[(count_ - 1) / 2];
}

void MinuteHistory::clear() {
    buckets_ = {};
    first_sample_ms_ = 0;
    has_first_sample_ = false;
}

void MinuteHistory::add(std::uint64_t now_ms, const PmReading& reading) {
    if (!has_first_sample_) {
        first_sample_ms_ = now_ms;
        has_first_sample_ = true;
    }
    const auto minute_id = now_ms / 60000ULL;
    auto& bucket = buckets_[minute_id % buckets_.size()];
    if (!bucket.occupied || bucket.minute_id != minute_id) {
        bucket = {};
        bucket.occupied = true;
        bucket.minute_id = minute_id;
    }
    bucket.pm1_sum += reading.pm1;
    bucket.pm25_sum += reading.pm25;
    bucket.pm10_sum += reading.pm10;
    ++bucket.samples;
}

const MinuteHistory::Bucket* MinuteHistory::find(std::uint64_t minute_id) const {
    const auto& bucket = buckets_[minute_id % buckets_.size()];
    return bucket.occupied && bucket.minute_id == minute_id && bucket.samples > 0 ? &bucket : nullptr;
}

AverageReading MinuteHistory::hour_average(std::uint64_t now_ms) const {
    AverageReading result{};
    const auto current_minute = now_ms / 60000ULL;
    float pm1_minute_sum = 0;
    float pm25_minute_sum = 0;
    float pm10_minute_sum = 0;

    for (std::size_t age = 0; age < 60; ++age) {
        if (current_minute < age) {
            break;
        }
        const auto* bucket = find(current_minute - age);
        if (!bucket) {
            continue;
        }
        pm1_minute_sum += static_cast<float>(bucket->pm1_sum) / bucket->samples;
        pm25_minute_sum += static_cast<float>(bucket->pm25_sum) / bucket->samples;
        pm10_minute_sum += static_cast<float>(bucket->pm10_sum) / bucket->samples;
        result.samples += bucket->samples;
        ++result.valid_minutes;
    }

    if (result.samples == 0) {
        return result;
    }
    result.valid = true;
    result.pm1 = pm1_minute_sum / result.valid_minutes;
    result.pm25 = pm25_minute_sum / result.valid_minutes;
    result.pm10 = pm10_minute_sum / result.valid_minutes;
    result.complete_hour = result.valid_minutes == 60 && has_first_sample_ &&
        now_ms >= first_sample_ms_ && now_ms - first_sample_ms_ >= 60ULL * 60000ULL;
    return result;
}

TrendSeries MinuteHistory::trend(std::uint64_t now_ms, std::size_t minutes) const {
    TrendSeries result{};
    minutes = std::max<std::size_t>(1, std::min<std::size_t>(minutes, 60));
    result.count = minutes;
    const auto current_minute = now_ms / 60000ULL;
    float sum = 0;
    std::size_t valid_count = 0;
    result.minimum = std::numeric_limits<float>::max();
    result.maximum = std::numeric_limits<float>::lowest();

    for (std::size_t index = 0; index < minutes; ++index) {
        const auto age = minutes - 1 - index;
        if (current_minute < age) {
            continue;
        }
        const auto* bucket = find(current_minute - age);
        if (!bucket) {
            continue;
        }
        const float value_in = static_cast<float>(bucket->pm25_sum) / bucket->samples;
        result.values[index] = value_in;
        result.valid[index] = true;
        result.current = value_in;
        result.minimum = std::min(result.minimum, value_in);
        result.maximum = std::max(result.maximum, value_in);
        sum += value_in;
        ++valid_count;
    }

    result.has_data = valid_count > 0;
    if (result.has_data) {
        result.average = sum / static_cast<float>(valid_count);
    } else {
        result.minimum = 0;
        result.maximum = 0;
    }
    return result;
}

AirQuality calculate_pm25_iaqi(float concentration) {
    static constexpr std::array<float, 8> concentration_points{
        0.0F, 30.0F, 60.0F, 115.0F, 150.0F, 250.0F, 350.0F, 500.0F};
    static constexpr std::array<int, 8> index_points{0, 50, 100, 150, 200, 300, 400, 500};

    concentration = std::max(0.0F, concentration);
    int iaqi = 500;
    if (concentration < concentration_points.back()) {
        for (std::size_t index = 1; index < concentration_points.size(); ++index) {
            if (concentration <= concentration_points[index]) {
                const float low_c = concentration_points[index - 1];
                const float high_c = concentration_points[index];
                const int low_i = index_points[index - 1];
                const int high_i = index_points[index];
                const float interpolated = static_cast<float>(low_i) +
                    (static_cast<float>(high_i - low_i) * (concentration - low_c) / (high_c - low_c));
                iaqi = static_cast<int>(std::ceil(interpolated - 0.00001F));
                break;
            }
        }
    }

    QualityLevel level = QualityLevel::Severe;
    if (iaqi <= 50) {
        level = QualityLevel::Excellent;
    } else if (iaqi <= 100) {
        level = QualityLevel::Good;
    } else if (iaqi <= 150) {
        level = QualityLevel::Light;
    } else if (iaqi <= 200) {
        level = QualityLevel::Moderate;
    } else if (iaqi <= 300) {
        level = QualityLevel::Heavy;
    }
    return {iaqi, level};
}

const char* quality_label_zh(QualityLevel level) {
    switch (level) {
        case QualityLevel::Excellent: return "优";
        case QualityLevel::Good: return "良";
        case QualityLevel::Light: return "轻度污染";
        case QualityLevel::Moderate: return "中度污染";
        case QualityLevel::Heavy: return "重度污染";
        case QualityLevel::Severe: return "严重污染";
    }
    return "未知";
}

}  // namespace pm25
