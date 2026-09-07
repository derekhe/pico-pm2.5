#include "pm25/canvas.hpp"
#include "pm25/core.hpp"
#include "pm25/lcd.hpp"
#include "pm25/ui.hpp"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

constexpr std::array<uint, 2> kSensorTxPins{0, 4};
constexpr std::array<uint, 2> kSensorRxPins{1, 5};
constexpr std::uint64_t kWarmupMs = 30'000;
constexpr std::uint64_t kDisconnectedMs = 3'000;
constexpr std::uint64_t kLongDisconnectMs = 10'000;
constexpr std::uint64_t kRetryMs = 5'000;

alignas(4) std::array<std::uint16_t, pm25::kDisplayWidth * pm25::kDisplayHeight> g_framebuffer{};
std::array<std::uint8_t, 256> g_uart_buffer{};
std::array<std::uint8_t, 256> g_uart_sources{};
volatile std::uint16_t g_uart_head = 0;
volatile std::uint16_t g_uart_tail = 0;
volatile std::uint32_t g_uart_overflows = 0;

void drain_uart(uart_inst_t* uart, std::uint8_t source) {
    while (uart_is_readable(uart)) {
        const auto byte = static_cast<std::uint8_t>(uart_getc(uart));
        const auto next = static_cast<std::uint16_t>((g_uart_head + 1U) % g_uart_buffer.size());
        if (next == g_uart_tail) {
            ++g_uart_overflows;
        } else {
            g_uart_buffer[g_uart_head] = byte;
            g_uart_sources[g_uart_head] = source;
            g_uart_head = next;
        }
    }
}

void on_uart0_irq() { drain_uart(uart0, 0); }
void on_uart1_irq() { drain_uart(uart1, 1); }

bool pop_uart_byte(std::uint8_t& byte, std::uint8_t& source) {
    const auto saved = save_and_disable_interrupts();
    if (g_uart_tail == g_uart_head) {
        restore_interrupts(saved);
        return false;
    }
    byte = g_uart_buffer[g_uart_tail];
    source = g_uart_sources[g_uart_tail];
    g_uart_tail = static_cast<std::uint16_t>((g_uart_tail + 1U) % g_uart_buffer.size());
    restore_interrupts(saved);
    return true;
}

void send_sensor_command(uart_inst_t* uart, std::uint8_t command, std::uint8_t data_low) {
    std::array<std::uint8_t, 7> packet{0x42, 0x4d, command, 0x00, data_low, 0x00, 0x00};
    std::uint16_t checksum = 0;
    for (std::size_t index = 0; index < 5; ++index) {
        checksum = static_cast<std::uint16_t>(checksum + packet[index]);
    }
    packet[5] = static_cast<std::uint8_t>(checksum >> 8U);
    packet[6] = static_cast<std::uint8_t>(checksum & 0xffU);
    uart_write_blocking(uart, packet.data(), packet.size());
}

void wake_and_enable_sensor() {
    send_sensor_command(uart0, 0xe4, 0x01);
    send_sensor_command(uart1, 0xe4, 0x01);
    sleep_ms(100);
    send_sensor_command(uart0, 0xe1, 0x01);
    send_sensor_command(uart1, 0xe1, 0x01);
}

pm25::ParserStats combined_stats(const std::array<pm25::PmsParser, 2>& parsers) {
    pm25::ParserStats result{};
    for (const auto& parser : parsers) {
        result.valid_frames += parser.stats().valid_frames;
        result.checksum_errors += parser.stats().checksum_errors;
        result.length_errors += parser.stats().length_errors;
        result.resyncs += parser.stats().resyncs;
    }
    return result;
}

struct Button {
    uint pin;
    bool raw_pressed = false;
    bool stable_pressed = false;
    std::uint64_t changed_ms = 0;

    bool poll(std::uint64_t now_ms) {
        const bool pressed = gpio_get(pin) == 0;
        if (pressed != raw_pressed) {
            raw_pressed = pressed;
            changed_ms = now_ms;
        }
        if (stable_pressed != raw_pressed && now_ms - changed_ms >= 30) {
            stable_pressed = raw_pressed;
            return stable_pressed;
        }
        return false;
    }
};

enum ButtonIndex : std::size_t { Up, Down, Left, Right, Center, A, B, X, Y };

void init_buttons(std::array<Button, 9>& buttons) {
    buttons = {{{2}, {18}, {16}, {20}, {3}, {15}, {17}, {19}, {21}}};
    for (auto& button : buttons) {
        gpio_init(button.pin);
        gpio_set_dir(button.pin, GPIO_IN);
        gpio_pull_up(button.pin);
    }
}

void change_page(pm25::Page& page, int delta) {
    constexpr int page_count = 4;
    int value = (static_cast<int>(page) + delta) % page_count;
    if (value < 0) {
        value += page_count;
    }
    page = static_cast<pm25::Page>(value);
}

}  // namespace

int main() {
    stdio_init_all();
    pm25::Lcd lcd;
    if (!lcd.init()) {
        // The driver falls back to blocking SPI if DMA is unavailable, so this is not fatal.
        std::printf("WARN dma_unavailable\n");
    }
    pm25::Canvas canvas(g_framebuffer.data());

    uart_init(uart0, 9600);
    uart_init(uart1, 9600);
    gpio_set_function(kSensorTxPins[0], GPIO_FUNC_UART);
    gpio_set_function(kSensorRxPins[0], GPIO_FUNC_UART);
    gpio_set_function(kSensorTxPins[1], GPIO_FUNC_UART);
    gpio_set_function(kSensorRxPins[1], GPIO_FUNC_UART);
    gpio_pull_up(kSensorRxPins[0]);
    gpio_pull_up(kSensorRxPins[1]);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    uart_set_fifo_enabled(uart1, true);
    irq_set_exclusive_handler(UART0_IRQ, on_uart0_irq);
    irq_set_exclusive_handler(UART1_IRQ, on_uart1_irq);
    irq_set_enabled(UART0_IRQ, true);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irqs_enabled(uart0, true, false);
    uart_set_irqs_enabled(uart1, true, false);

    std::array<Button, 9> buttons{};
    init_buttons(buttons);

    const auto boot_ms = time_us_64() / 1000ULL;
    std::uint64_t warmup_until_ms = boot_ms + kWarmupMs;
    std::uint64_t last_valid_ms = 0;
    std::uint64_t last_retry_ms = boot_ms;
    std::uint64_t last_render_ms = 0;
    std::uint64_t last_log_ms = 0;
    std::uint64_t last_summary_ms = 0;
    bool has_reading = false;
    bool screen_on = true;
    bool dirty = true;
    std::uint8_t brightness_index = 3;
    constexpr std::array<std::uint8_t, 5> brightness_values{20, 40, 60, 80, 100};
    std::uint8_t trend_range_index = 1;
    constexpr std::array<std::uint8_t, 3> trend_ranges{10, 30, 60};
    pm25::Page page = pm25::Page::Home;

    std::array<pm25::PmsParser, 2> parsers{};
    int active_uart = -1;
    pm25::Median5 median;
    pm25::MinuteHistory history;
    pm25::PmReading latest{};
    std::uint16_t filtered_pm25 = 0;
    pm25::AverageReading hour{};
    pm25::AirQuality quality{};

    wake_and_enable_sensor();

    for (;;) {
        const auto now_ms = time_us_64() / 1000ULL;
        if (active_uart >= 0 && last_valid_ms != 0 && now_ms - last_valid_ms > kLongDisconnectMs) {
            active_uart = -1;
        }
        std::uint8_t byte = 0;
        std::uint8_t source = 0;
        pm25::PmReading parsed{};
        while (pop_uart_byte(byte, source)) {
            if (active_uart >= 0 && source != static_cast<std::uint8_t>(active_uart)) {
                continue;
            }
            if (!parsers[source].feed(byte, parsed)) {
                continue;
            }
            active_uart = source;
            const bool returning_after_long_gap = last_valid_ms != 0 && now_ms - last_valid_ms > kLongDisconnectMs;
            if (returning_after_long_gap) {
                warmup_until_ms = now_ms + kWarmupMs;
                median.reset();
            }
            latest = parsed;
            filtered_pm25 = median.push(parsed.pm25);
            last_valid_ms = now_ms;
            has_reading = true;
            if (now_ms >= warmup_until_ms) {
                history.add(now_ms - boot_ms, parsed);
            }
            dirty = true;
        }

        const bool connected = has_reading && now_ms - last_valid_ms <= kDisconnectedMs;
        const auto parser_stats = combined_stats(parsers);
        if (!connected && now_ms - last_retry_ms >= kRetryMs) {
            wake_and_enable_sensor();
            last_retry_ms = now_ms;
            dirty = true;
        }

        for (std::size_t index = 0; index < buttons.size(); ++index) {
            if (!buttons[index].poll(now_ms)) {
                continue;
            }
            if (!screen_on) {
                screen_on = true;
                lcd.set_backlight(brightness_values[brightness_index]);
                dirty = true;
                continue;
            }
            switch (index) {
                case Left:
                case A:
                    change_page(page, -1);
                    break;
                case Right:
                case B:
                    change_page(page, 1);
                    break;
                case Up:
                    if (brightness_index + 1 < brightness_values.size()) {
                        ++brightness_index;
                        lcd.set_backlight(brightness_values[brightness_index]);
                    }
                    break;
                case Down:
                    if (brightness_index > 0) {
                        --brightness_index;
                        lcd.set_backlight(brightness_values[brightness_index]);
                    }
                    break;
                case Center:
                    page = pm25::Page::Home;
                    break;
                case X:
                    if (page == pm25::Page::Trend) {
                        trend_range_index = static_cast<std::uint8_t>((trend_range_index + 1) % trend_ranges.size());
                    }
                    break;
                case Y:
                    screen_on = false;
                    lcd.set_backlight(0);
                    break;
            }
            dirty = true;
        }

        const auto relative_ms = now_ms - boot_ms;
        if (last_summary_ms == 0 || now_ms - last_summary_ms >= 1000) {
            hour = history.hour_average(relative_ms);
            quality = pm25::calculate_pm25_iaqi(hour.valid ? hour.pm25 : filtered_pm25);
            last_summary_ms = now_ms;
        }

        if (now_ms - last_log_ms >= 1000) {
            const auto age = has_reading ? now_ms - last_valid_ms : 0;
            std::printf(
                "PM pm1=%u pm25=%u pm10=%u filtered=%u hour=%.1f iaqi=%d valid=%u age_ms=%llu frames=%lu checksum=%lu length=%lu resync=%lu overflow=%lu uart=%s rx01=%u rx45=%u\n",
                latest.pm1, latest.pm25, latest.pm10, filtered_pm25,
                static_cast<double>(hour.valid ? hour.pm25 : 0.0F), quality.iaqi, connected ? 1U : 0U,
                static_cast<unsigned long long>(age),
                static_cast<unsigned long>(parser_stats.valid_frames),
                static_cast<unsigned long>(parser_stats.checksum_errors),
                static_cast<unsigned long>(parser_stats.length_errors),
                static_cast<unsigned long>(parser_stats.resyncs),
                static_cast<unsigned long>(g_uart_overflows),
                active_uart == 0 ? "GP0/1" : (active_uart == 1 ? "GP4/5" : "?"),
                gpio_get(kSensorRxPins[0]) ? 1U : 0U,
                gpio_get(kSensorRxPins[1]) ? 1U : 0U);
            last_log_ms = now_ms;
        }

        if (now_ms - last_render_ms >= 1000) {
            dirty = true;
        }
        if (dirty && screen_on) {
            pm25::UiState state{};
            state.page = page;
            state.screen_on = screen_on;
            state.connected = connected;
            state.warming_up = now_ms < warmup_until_ms;
            state.warmup_seconds = state.warming_up
                ? static_cast<std::uint32_t>((warmup_until_ms - now_ms + 999) / 1000)
                : 0;
            state.has_reading = has_reading;
            state.data_age_ms = has_reading
                ? static_cast<std::uint32_t>(std::min<std::uint64_t>(now_ms - last_valid_ms, 0xffffffffULL))
                : 0;
            state.uptime_seconds = relative_ms / 1000;
            state.brightness_percent = brightness_values[brightness_index];
            state.trend_minutes = trend_ranges[trend_range_index];
            state.latest = latest;
            state.filtered_pm25 = filtered_pm25;
            state.hour = hour;
            state.trend = history.trend(relative_ms, state.trend_minutes);
            state.quality = quality;
            state.parser = parser_stats;
            state.rx_overflows = g_uart_overflows;
            render_ui(canvas, state);
            lcd.present(g_framebuffer.data());
            last_render_ms = now_ms;
            dirty = false;
        }

        sleep_ms(2);
    }
}
